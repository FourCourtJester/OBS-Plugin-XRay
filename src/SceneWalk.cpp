/*
XRay
Copyright (C) 2026 FourCourtJester <shaun@mse.gg>

This program is free software; you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation; either version 2 of the License, or
(at your option) any later version.

This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.

You should have received a copy of the GNU General Public License along
with this program. If not, see <https://www.gnu.org/licenses/>
*/

#include "SceneWalk.hpp"

#include <obs.h>
#include <obs.hpp>
#include <obs-frontend-api.h>

#include <algorithm>

namespace xray {

namespace {

/*
 * Belt and braces alongside the ancestor-path check below. OBS enforces its
 * cycle protection when a source is added to a scene, not when the scene is
 * walked, so a collection file that was hand-edited or written by an older
 * build can still present a cycle at read time.
 */
constexpr int MAX_DEPTH = 32;

/*
 * Deliberately not "collapsed", which is OBS's own key for group collapse in
 * the Sources dock. Branches here default to collapsed, and defaulting through
 * that key would collapse the operator's groups in their real Sources dock.
 */
const char *const COLLAPSED_KEY = "xray_collapsed";

struct State {
	std::vector<std::string> ancestors;
	WalkResult result;
};

const char *safe_str(const char *value)
{
	return value ? value : "";
}

/*
 * Fills in everything a row needs that comes from the scene item rather than
 * from the source: what to mutate, and the current toggle states.
 */
void describe_item(Node &node, obs_sceneitem_t *item, obs_source_t *owner, obs_source_t *source)
{
	node.name = safe_str(obs_source_get_name(source));
	node.source_id = safe_str(obs_source_get_id(source));
	node.source_uuid = safe_str(obs_source_get_uuid(source));
	node.owner_uuid = safe_str(obs_source_get_uuid(owner));
	node.item_id = obs_sceneitem_get_id(item);
	node.visible = obs_sceneitem_visible(item);
	node.locked = obs_sceneitem_locked(item);
	node.selected = obs_sceneitem_selected(item);

	/* Unset means collapsed, so a deep tree opens tidy rather than sprawling. */
	OBSDataAutoRelease settings = obs_sceneitem_get_private_settings(item);
	node.collapsed = obs_data_has_user_value(settings, COLLAPSED_KEY) ? obs_data_get_bool(settings, COLLAPSED_KEY)
									  : true;
}

/* Scene item ids in the order libobs holds them, which is the order shown. */
std::vector<int64_t> item_order(obs_scene_t *scene);

obs_sceneitem_t *resolve_item(const std::string &owner_uuid, int64_t item_id)
{
	OBSSourceAutoRelease owner = obs_get_source_by_uuid(owner_uuid.c_str());
	if (!owner)
		return nullptr;

	obs_scene_t *scene = obs_group_or_scene_from_source(owner);
	if (!scene)
		return nullptr;

	return obs_scene_find_sceneitem_by_id(scene, item_id);
}

void watch(State &state, obs_source_t *source, bool container)
{
	const char *uuid = obs_source_get_uuid(source);
	if (!uuid)
		return;

	auto same = [uuid](const Watch &w) {
		return w.uuid == uuid;
	};

	if (std::find_if(state.result.watches.begin(), state.result.watches.end(), same) != state.result.watches.end())
		return;

	state.result.watches.push_back({uuid, container});
}

bool collect_item(obs_scene_t *, obs_sceneitem_t *item, void *param)
{
	static_cast<std::vector<OBSSceneItem> *>(param)->emplace_back(item);
	return true;
}

/*
 * obs_scene_enum_items() holds the scene's mutex for the whole of the callback
 * (full_lock/full_unlock around the loop in obs-scene.c), and it drops its own
 * reference to each item as soon as the callback returns. So the callback does
 * exactly one thing -- take a reference -- and every bit of real work, above
 * all the recursion, happens after the lock is released.
 */
std::vector<OBSSceneItem> snapshot_items(obs_scene_t *scene)
{
	std::vector<OBSSceneItem> items;
	obs_scene_enum_items(scene, collect_item, &items);
	return items;
}

std::vector<int64_t> item_order(obs_scene_t *scene)
{
	std::vector<int64_t> ids;
	for (const OBSSceneItem &item : snapshot_items(scene))
		ids.push_back(obs_sceneitem_get_id(item));
	return ids;
}

obs_scene_t *resolve_scene(const std::string &owner_uuid, OBSSourceAutoRelease &owner)
{
	owner = obs_get_source_by_uuid(owner_uuid.c_str());
	return owner ? obs_group_or_scene_from_source(owner) : nullptr;
}

std::vector<Node> walk_scene(obs_scene_t *scene, bool inside_subscene, int depth, State &state);

Node build_subscene(obs_sceneitem_t *item, obs_source_t *owner, obs_source_t *source, obs_scene_t *subscene, int depth,
		    State &state)
{
	Node node;
	node.kind = NodeKind::SubScene;
	describe_item(node, item, owner, source);

	watch(state, source, true);

	const char *uuid = obs_source_get_uuid(source);
	std::string key = uuid ? uuid : node.name;

	/*
	 * Only the ancestor path is tracked, not every scene already seen. A
	 * scene referenced twice as a sibling is rendered twice by design, so a
	 * global visited set would wrongly swallow the second copy.
	 */
	if (std::find(state.ancestors.begin(), state.ancestors.end(), key) != state.ancestors.end()) {
		node.cyclic = true;
		return node;
	}

	state.ancestors.push_back(std::move(key));
	node.children = walk_scene(subscene, true, depth + 1, state);
	state.ancestors.pop_back();

	return node;
}

std::vector<Node> walk_scene(obs_scene_t *scene, bool inside_subscene, int depth, State &state)
{
	std::vector<Node> nodes;

	if (!scene || depth >= MAX_DEPTH)
		return nodes;

	/* Borrowed, not a new reference. */
	obs_source_t *owner = obs_scene_get_source(scene);

	/*
	 * Reversed, because obs_scene_enum_items() is not display order.
	 * SourceTreeModel::enumItem does items.insert(0, item) as it enumerates,
	 * so OBS's Sources dock shows the reverse of the enumeration: the
	 * scene's first_item is the bottom row, not the top. Walking forwards
	 * puts every list in this dock upside down relative to OBS.
	 */
	std::vector<OBSSceneItem> items = snapshot_items(scene);
	std::reverse(items.begin(), items.end());

	for (const OBSSceneItem &item : items) {
		obs_source_t *source = obs_sceneitem_get_source(item);
		if (!source)
			continue;

		if (obs_scene_t *group = obs_group_from_source(source)) {
			/*
			 * Watched before the prune decision, not after: a group
			 * that earns no row today still has to be watched, or
			 * dropping a scene into it would go unnoticed.
			 */
			watch(state, source, true);

			Node node;
			node.kind = NodeKind::Group;
			describe_item(node, item, owner, source);
			node.children = walk_scene(group, inside_subscene, depth + 1, state);

			/*
			 * Groups are walked through rather than around, but a
			 * group above a subscene only earns a row if something
			 * survived beneath it. Inside a subscene it always does.
			 */
			if (inside_subscene || !node.children.empty())
				nodes.push_back(std::move(node));

		} else if (obs_scene_t *subscene = obs_scene_from_source(source)) {
			nodes.push_back(build_subscene(item, owner, source, subscene, depth, state));

		} else if (inside_subscene) {
			Node node;
			node.kind = NodeKind::Source;
			describe_item(node, item, owner, source);
			nodes.push_back(std::move(node));

			watch(state, source, false);
		}
	}

	return nodes;
}

} // namespace

WalkResult walk_program_scene()
{
	/* obs_frontend_get_current_scene() returns a new reference. */
	OBSSourceAutoRelease program = obs_frontend_get_current_scene();
	if (!program)
		return {};

	State state;
	watch(state, program, true);

	if (const char *uuid = obs_source_get_uuid(program))
		state.ancestors.emplace_back(uuid);

	state.result.tree = walk_scene(obs_scene_from_source(program), false, 0, state);

	return std::move(state.result);
}

void set_item_visible(const std::string &owner_uuid, int64_t item_id, bool visible)
{
	if (obs_sceneitem_t *item = resolve_item(owner_uuid, item_id))
		obs_sceneitem_set_visible(item, visible);
}

void set_item_locked(const std::string &owner_uuid, int64_t item_id, bool locked)
{
	if (obs_sceneitem_t *item = resolve_item(owner_uuid, item_id))
		obs_sceneitem_set_locked(item, locked);
}

void set_item_collapsed(const std::string &owner_uuid, int64_t item_id, bool collapsed)
{
	obs_sceneitem_t *item = resolve_item(owner_uuid, item_id);
	if (!item)
		return;

	OBSDataAutoRelease settings = obs_sceneitem_get_private_settings(item);
	obs_data_set_bool(settings, COLLAPSED_KEY, collapsed);
}

namespace {

void collapse_branch(const std::vector<Node> &nodes, bool collapsed)
{
	for (const Node &node : nodes) {
		if (node.children.empty())
			continue;

		set_item_collapsed(node.owner_uuid, node.item_id, collapsed);
		collapse_branch(node.children, collapsed);
	}
}

} // namespace

void set_all_collapsed(bool collapsed)
{
	/*
	 * Walked rather than enumerated, so this reaches exactly the branches
	 * the dock draws -- including the ones currently hidden inside a
	 * collapsed parent, since the walk never stops at a collapsed node.
	 */
	collapse_branch(walk_program_scene().tree, collapsed);
}

void move_item_before(const std::string &owner_uuid, int64_t item_id, int64_t before_item_id)
{
	if (item_id == before_item_id)
		return;

	OBSSourceAutoRelease owner;
	obs_scene_t *scene = resolve_scene(owner_uuid, owner);
	if (!scene)
		return;

	obs_sceneitem_t *item = obs_scene_find_sceneitem_by_id(scene, item_id);
	if (!item)
		return;

	/*
	 * Worked out in display order, then converted back, rather than by index
	 * arithmetic against the scene. Display order is the reverse of the
	 * scene's own order, so "above" on screen is "after" in the scene, and
	 * doing this by hand invites exactly the off-by-one that reversing
	 * introduces.
	 */
	std::vector<int64_t> display = item_order(scene);
	std::reverse(display.begin(), display.end());

	display.erase(std::remove(display.begin(), display.end(), item_id), display.end());

	auto anchor = display.end();
	if (before_item_id >= 0)
		anchor = std::find(display.begin(), display.end(), before_item_id);

	display.insert(anchor, item_id);

	/* Back to the scene's own ordering. */
	std::reverse(display.begin(), display.end());

	const auto placed = std::find(display.begin(), display.end(), item_id);
	const size_t position = static_cast<size_t>(std::distance(display.begin(), placed));

	/*
	 * obs_sceneitem_set_order_position() detaches the item and then walks
	 * that many steps down what is left, so the position it wants is this
	 * item's index in the finished list -- inserting at that index into the
	 * list without it reproduces exactly the order computed above.
	 */
	obs_sceneitem_set_order_position(item, static_cast<int>(position));
}

bool rename_source(const std::string &source_uuid, const std::string &new_name)
{
	if (new_name.empty())
		return false;

	OBSSourceAutoRelease source = obs_get_source_by_uuid(source_uuid.c_str());
	if (!source)
		return false;

	const char *current = obs_source_get_name(source);
	if (current && new_name == current)
		return true;

	/* Names are an identity in the OBS UI, so a clash is refused. */
	OBSSourceAutoRelease clash = obs_get_source_by_name(new_name.c_str());
	if (clash)
		return false;

	obs_source_set_name(source, new_name.c_str());
	return true;
}

void open_source_properties(const std::string &source_uuid)
{
	OBSSourceAutoRelease source = obs_get_source_by_uuid(source_uuid.c_str());
	if (source && obs_source_configurable(source))
		obs_frontend_open_source_properties(source);
}

void open_source_filters(const std::string &source_uuid)
{
	OBSSourceAutoRelease source = obs_get_source_by_uuid(source_uuid.c_str());
	if (source)
		obs_frontend_open_source_filters(source);
}

void open_source_interaction(const std::string &source_uuid)
{
	OBSSourceAutoRelease source = obs_get_source_by_uuid(source_uuid.c_str());
	if (source && (obs_source_get_output_flags(source) & OBS_SOURCE_INTERACTION))
		obs_frontend_open_source_interaction(source);
}

bool source_is_configurable(const std::string &source_uuid)
{
	OBSSourceAutoRelease source = obs_get_source_by_uuid(source_uuid.c_str());
	return source && obs_source_configurable(source);
}

bool source_is_interactive(const std::string &source_uuid)
{
	OBSSourceAutoRelease source = obs_get_source_by_uuid(source_uuid.c_str());
	return source && (obs_source_get_output_flags(source) & OBS_SOURCE_INTERACTION) != 0;
}

namespace {

/*
 * Explicit both ways rather than a cast. The libobs enumerators happen to line
 * up with the mirrored ones today, but a reordering upstream would silently
 * turn Bicubic into Bilinear and nothing would fail loudly.
 */
obs_scale_type to_obs(ScaleFilter f)
{
	switch (f) {
	case ScaleFilter::Point:
		return OBS_SCALE_POINT;
	case ScaleFilter::Bicubic:
		return OBS_SCALE_BICUBIC;
	case ScaleFilter::Bilinear:
		return OBS_SCALE_BILINEAR;
	case ScaleFilter::Lanczos:
		return OBS_SCALE_LANCZOS;
	case ScaleFilter::Area:
		return OBS_SCALE_AREA;
	case ScaleFilter::Disable:
		break;
	}
	return OBS_SCALE_DISABLE;
}

ScaleFilter from_obs(obs_scale_type f)
{
	switch (f) {
	case OBS_SCALE_POINT:
		return ScaleFilter::Point;
	case OBS_SCALE_BICUBIC:
		return ScaleFilter::Bicubic;
	case OBS_SCALE_BILINEAR:
		return ScaleFilter::Bilinear;
	case OBS_SCALE_LANCZOS:
		return ScaleFilter::Lanczos;
	case OBS_SCALE_AREA:
		return ScaleFilter::Area;
	case OBS_SCALE_DISABLE:
		break;
	}
	return ScaleFilter::Disable;
}

obs_blending_type to_obs(BlendingMode m)
{
	switch (m) {
	case BlendingMode::Additive:
		return OBS_BLEND_ADDITIVE;
	case BlendingMode::Subtract:
		return OBS_BLEND_SUBTRACT;
	case BlendingMode::Screen:
		return OBS_BLEND_SCREEN;
	case BlendingMode::Multiply:
		return OBS_BLEND_MULTIPLY;
	case BlendingMode::Lighten:
		return OBS_BLEND_LIGHTEN;
	case BlendingMode::Darken:
		return OBS_BLEND_DARKEN;
	case BlendingMode::Normal:
		break;
	}
	return OBS_BLEND_NORMAL;
}

BlendingMode from_obs(obs_blending_type m)
{
	switch (m) {
	case OBS_BLEND_ADDITIVE:
		return BlendingMode::Additive;
	case OBS_BLEND_SUBTRACT:
		return BlendingMode::Subtract;
	case OBS_BLEND_SCREEN:
		return BlendingMode::Screen;
	case OBS_BLEND_MULTIPLY:
		return BlendingMode::Multiply;
	case OBS_BLEND_LIGHTEN:
		return BlendingMode::Lighten;
	case OBS_BLEND_DARKEN:
		return BlendingMode::Darken;
	case OBS_BLEND_NORMAL:
		break;
	}
	return BlendingMode::Normal;
}

obs_blending_method to_obs(BlendingMethod m)
{
	return m == BlendingMethod::SrgbOff ? OBS_BLEND_METHOD_SRGB_OFF : OBS_BLEND_METHOD_DEFAULT;
}

BlendingMethod from_obs(obs_blending_method m)
{
	return m == OBS_BLEND_METHOD_SRGB_OFF ? BlendingMethod::SrgbOff : BlendingMethod::Default;
}

obs_order_movement to_obs(OrderMovement m)
{
	switch (m) {
	case OrderMovement::Down:
		return OBS_ORDER_MOVE_DOWN;
	case OrderMovement::Top:
		return OBS_ORDER_MOVE_TOP;
	case OrderMovement::Bottom:
		return OBS_ORDER_MOVE_BOTTOM;
	case OrderMovement::Up:
		break;
	}
	return OBS_ORDER_MOVE_UP;
}

} // namespace

ItemProperties item_properties(const std::string &owner_uuid, int64_t item_id)
{
	ItemProperties props;

	obs_sceneitem_t *item = resolve_item(owner_uuid, item_id);
	if (!item)
		return props;

	obs_source_t *source = obs_sceneitem_get_source(item);
	if (!source)
		return props;

	const uint32_t flags = obs_source_get_output_flags(source);

	props.found = true;
	props.has_video = (flags & OBS_SOURCE_VIDEO) != 0;
	props.has_audio = (flags & OBS_SOURCE_AUDIO) != 0;
	props.is_async_video = (flags & OBS_SOURCE_ASYNC_VIDEO) == OBS_SOURCE_ASYNC_VIDEO;
	props.is_group = obs_sceneitem_is_group(item);

	props.scale = from_obs(obs_sceneitem_get_scale_filter(item));
	props.blending_mode = from_obs(obs_sceneitem_get_blending_mode(item));
	props.blending_method = from_obs(obs_sceneitem_get_blending_method(item));

	return props;
}

void set_scale_filter(const std::string &owner_uuid, int64_t item_id, ScaleFilter filter)
{
	if (obs_sceneitem_t *item = resolve_item(owner_uuid, item_id))
		obs_sceneitem_set_scale_filter(item, to_obs(filter));
}

void set_blending_mode(const std::string &owner_uuid, int64_t item_id, BlendingMode mode)
{
	if (obs_sceneitem_t *item = resolve_item(owner_uuid, item_id))
		obs_sceneitem_set_blending_mode(item, to_obs(mode));
}

void set_blending_method(const std::string &owner_uuid, int64_t item_id, BlendingMethod method)
{
	if (obs_sceneitem_t *item = resolve_item(owner_uuid, item_id))
		obs_sceneitem_set_blending_method(item, to_obs(method));
}

void set_order(const std::string &owner_uuid, int64_t item_id, OrderMovement movement)
{
	if (obs_sceneitem_t *item = resolve_item(owner_uuid, item_id))
		obs_sceneitem_set_order(item, to_obs(movement));
}

void remove_item(const std::string &owner_uuid, int64_t item_id)
{
	if (obs_sceneitem_t *item = resolve_item(owner_uuid, item_id))
		obs_sceneitem_remove(item);
}

void screenshot_source(const std::string &source_uuid)
{
	OBSSourceAutoRelease source = obs_get_source_by_uuid(source_uuid.c_str());
	if (source)
		obs_frontend_take_source_screenshot(source);
}

void open_source_projector(const std::string &source_uuid, int monitor)
{
	OBSSourceAutoRelease source = obs_get_source_by_uuid(source_uuid.c_str());
	if (!source)
		return;

	/* A monitor of -1 is how OBS itself asks for a window rather than a
	 * fullscreen projector; see OBSBasic::OpenSourceWindow. */
	obs_frontend_open_projector("Source", monitor, nullptr, obs_source_get_name(source));
}

namespace {

obs_deinterlace_mode to_obs(DeinterlaceMode m)
{
	switch (m) {
	case DeinterlaceMode::Discard:
		return OBS_DEINTERLACE_MODE_DISCARD;
	case DeinterlaceMode::Retro:
		return OBS_DEINTERLACE_MODE_RETRO;
	case DeinterlaceMode::Blend:
		return OBS_DEINTERLACE_MODE_BLEND;
	case DeinterlaceMode::Blend2x:
		return OBS_DEINTERLACE_MODE_BLEND_2X;
	case DeinterlaceMode::Linear:
		return OBS_DEINTERLACE_MODE_LINEAR;
	case DeinterlaceMode::Linear2x:
		return OBS_DEINTERLACE_MODE_LINEAR_2X;
	case DeinterlaceMode::Yadif:
		return OBS_DEINTERLACE_MODE_YADIF;
	case DeinterlaceMode::Yadif2x:
		return OBS_DEINTERLACE_MODE_YADIF_2X;
	case DeinterlaceMode::Disable:
		break;
	}
	return OBS_DEINTERLACE_MODE_DISABLE;
}

DeinterlaceMode from_obs(obs_deinterlace_mode m)
{
	switch (m) {
	case OBS_DEINTERLACE_MODE_DISCARD:
		return DeinterlaceMode::Discard;
	case OBS_DEINTERLACE_MODE_RETRO:
		return DeinterlaceMode::Retro;
	case OBS_DEINTERLACE_MODE_BLEND:
		return DeinterlaceMode::Blend;
	case OBS_DEINTERLACE_MODE_BLEND_2X:
		return DeinterlaceMode::Blend2x;
	case OBS_DEINTERLACE_MODE_LINEAR:
		return DeinterlaceMode::Linear;
	case OBS_DEINTERLACE_MODE_LINEAR_2X:
		return DeinterlaceMode::Linear2x;
	case OBS_DEINTERLACE_MODE_YADIF:
		return DeinterlaceMode::Yadif;
	case OBS_DEINTERLACE_MODE_YADIF_2X:
		return DeinterlaceMode::Yadif2x;
	case OBS_DEINTERLACE_MODE_DISABLE:
		break;
	}
	return DeinterlaceMode::Disable;
}

} // namespace

DeinterlaceMode deinterlace_mode(const std::string &source_uuid)
{
	OBSSourceAutoRelease source = obs_get_source_by_uuid(source_uuid.c_str());
	return source ? from_obs(obs_source_get_deinterlace_mode(source)) : DeinterlaceMode::Disable;
}

FieldOrder deinterlace_field_order(const std::string &source_uuid)
{
	OBSSourceAutoRelease source = obs_get_source_by_uuid(source_uuid.c_str());
	if (!source)
		return FieldOrder::Top;

	return obs_source_get_deinterlace_field_order(source) == OBS_DEINTERLACE_FIELD_ORDER_BOTTOM ? FieldOrder::Bottom
												    : FieldOrder::Top;
}

void set_deinterlace_mode(const std::string &source_uuid, DeinterlaceMode mode)
{
	OBSSourceAutoRelease source = obs_get_source_by_uuid(source_uuid.c_str());
	if (source)
		obs_source_set_deinterlace_mode(source, to_obs(mode));
}

void set_deinterlace_field_order(const std::string &source_uuid, FieldOrder order)
{
	OBSSourceAutoRelease source = obs_get_source_by_uuid(source_uuid.c_str());
	if (source)
		obs_source_set_deinterlace_field_order(source, order == FieldOrder::Bottom
								       ? OBS_DEINTERLACE_FIELD_ORDER_BOTTOM
								       : OBS_DEINTERLACE_FIELD_ORDER_TOP);
}

void ungroup_item(const std::string &owner_uuid, int64_t item_id)
{
	obs_sceneitem_t *item = resolve_item(owner_uuid, item_id);

	/* Only a group can be dissolved; anything else is left alone. */
	if (item && obs_sceneitem_is_group(item))
		obs_sceneitem_group_ungroup(item);
}

/* ------------------------------------------------------------ audio mixer */

bool source_mixer_hidden(const std::string &source_uuid)
{
	OBSSourceAutoRelease source = obs_get_source_by_uuid(source_uuid.c_str());
	if (!source)
		return false;

	OBSDataAutoRelease settings = obs_source_get_private_settings(source);
	return obs_data_get_bool(settings, "mixer_hidden");
}

void set_source_mixer_hidden(const std::string &source_uuid, bool hidden)
{
	OBSSourceAutoRelease source = obs_get_source_by_uuid(source_uuid.c_str());
	if (!source)
		return;

	OBSDataAutoRelease settings = obs_source_get_private_settings(source);
	obs_data_set_bool(settings, "mixer_hidden", hidden);

	/*
	 * The setting on its own changes nothing on screen. OBSBasic builds and
	 * tears down the mixer strip in ActivateAudioSource and
	 * DeactivateAudioSource, which it calls from the source's
	 * audio_activate and audio_deactivate signals; neither the methods nor
	 * the signals are reachable from a plugin.
	 *
	 * Toggling audio_active off and straight back on emits exactly that
	 * pair, so the frontend re-reads mixer_hidden and the strip goes or
	 * comes back immediately. It is safe: libobs never reads audio_active
	 * for anything but raising these two signals -- it is a UI flag, not
	 * part of the audio path -- and the value ends up exactly as it was
	 * found. A source that is not audio-active has no strip to update, so
	 * it is left alone rather than switched on.
	 */
	if (obs_source_audio_active(source)) {
		obs_source_set_audio_active(source, false);
		obs_source_set_audio_active(source, true);
	}
}

/* -------------------------------------------------------- background colour */

ItemColor item_color(const std::string &owner_uuid, int64_t item_id)
{
	ItemColor colour;

	obs_sceneitem_t *item = resolve_item(owner_uuid, item_id);
	if (!item)
		return colour;

	OBSDataAutoRelease settings = obs_sceneitem_get_private_settings(item);
	colour.preset = static_cast<int>(obs_data_get_int(settings, "color-preset"));
	colour.custom = safe_str(obs_data_get_string(settings, "color"));

	return colour;
}

void set_item_color(const std::string &owner_uuid, int64_t item_id, int preset, const std::string &custom)
{
	obs_sceneitem_t *item = resolve_item(owner_uuid, item_id);
	if (!item)
		return;

	OBSDataAutoRelease settings = obs_sceneitem_get_private_settings(item);
	obs_data_set_int(settings, "color-preset", preset);

	/* Cleared for every preset but the custom one, matching OBS -- a stale
	 * colour string left behind would be picked up by the next custom. */
	obs_data_set_string(settings, "color", preset == 1 ? custom.c_str() : "");
}

/* ----------------------------------------------------------- the clipboard */

namespace {

/*
 * Weak references throughout, and released explicitly from clear_clipboard()
 * rather than by static destruction -- which runs after obs_shutdown() has
 * already freed what they point at.
 */
struct ItemClip {
	obs_weak_source_t *source = nullptr;
	obs_transform_info transform = {};
	obs_sceneitem_crop crop = {};
	obs_blending_method blend_method = OBS_BLEND_METHOD_DEFAULT;
	obs_blending_type blend_mode = OBS_BLEND_NORMAL;
	bool visible = true;
};

ItemClip g_item_clip;
obs_weak_source_t *g_filter_clip = nullptr;
obs_weak_source_t *g_transition_clip = nullptr;
int g_transition_clip_duration = 0;

void hold(obs_weak_source_t *&slot, obs_source_t *source)
{
	/* Taken before the release, so holding a source against itself does not
	 * drop the last reference in between. */
	obs_weak_source_t *next = source ? obs_source_get_weak_source(source) : nullptr;

	if (slot)
		obs_weak_source_release(slot);

	slot = next;
}

bool still_there(obs_weak_source_t *slot)
{
	if (!slot)
		return false;

	OBSSourceAutoRelease source = obs_weak_source_get_source(slot);
	return source != nullptr;
}

struct AddData {
	obs_source_t *source = nullptr;
	bool visible = true;

	/* Null means "leave at the default", which is what a freshly created
	 * source wants and a pasted one does not. */
	const obs_transform_info *transform = nullptr;
	const obs_sceneitem_crop *crop = nullptr;
	const obs_blending_method *blend_method = nullptr;
	const obs_blending_type *blend_mode = nullptr;
};

void add_source(void *param, obs_scene_t *scene)
{
	AddData *data = static_cast<AddData *>(param);

	obs_sceneitem_t *item = obs_scene_add(scene, data->source);
	if (!item)
		return;

	if (data->transform)
		obs_sceneitem_set_info2(item, data->transform);
	if (data->crop)
		obs_sceneitem_set_crop(item, data->crop);
	if (data->blend_method)
		obs_sceneitem_set_blending_method(item, *data->blend_method);
	if (data->blend_mode)
		obs_sceneitem_set_blending_mode(item, *data->blend_mode);

	obs_sceneitem_set_visible(item, data->visible);
}

/*
 * Adding to a group works without any special handling: a group is a scene
 * internally, and the new item comes in with update_transform set, which makes
 * libobs recompute the group's bounds on the next tick.
 */
void add_to_scene(obs_scene_t *scene, AddData &data)
{
	obs_enter_graphics();
	obs_scene_atomic_update(scene, add_source, &data);
	obs_leave_graphics();
}

/* Mirrors get_new_source_name(): the bare name if free, then " 2", " 3"... */
std::string unique_source_name(const std::string &base)
{
	std::string name = base;

	for (int suffix = 2;; suffix++) {
		OBSSourceAutoRelease taken = obs_get_source_by_name(name.c_str());
		if (!taken)
			return name;

		name = base + " " + std::to_string(suffix);
	}
}

} // namespace

void copy_item(const std::string &owner_uuid, int64_t item_id)
{
	obs_sceneitem_t *item = resolve_item(owner_uuid, item_id);
	if (!item)
		return;

	obs_source_t *source = obs_sceneitem_get_source(item);
	if (!source)
		return;

	hold(g_item_clip.source, source);

	obs_sceneitem_get_info2(item, &g_item_clip.transform);
	obs_sceneitem_get_crop(item, &g_item_clip.crop);
	g_item_clip.blend_method = obs_sceneitem_get_blending_method(item);
	g_item_clip.blend_mode = obs_sceneitem_get_blending_mode(item);
	g_item_clip.visible = obs_sceneitem_visible(item);
}

bool clipboard_has_item()
{
	return still_there(g_item_clip.source);
}

void paste_item(const std::string &owner_uuid, bool duplicate)
{
	OBSSourceAutoRelease source = g_item_clip.source ? obs_weak_source_get_source(g_item_clip.source) : nullptr;
	if (!source)
		return;

	OBSSourceAutoRelease owner;
	obs_scene_t *scene = resolve_scene(owner_uuid, owner);
	if (!scene)
		return;

	const std::string name = safe_str(obs_source_get_name(source));

	OBSSourceAutoRelease copy;
	obs_source_t *adding = source;

	if (duplicate) {
		copy = obs_source_duplicate(source, unique_source_name(name).c_str(), false);
		if (!copy)
			return;

		adding = copy;

	} else if (!name.empty() && obs_scene_get_group(scene, name.c_str())) {
		/* libobs will not hold two references to one group in a scene,
		 * so a paste that would do that is dropped rather than half
		 * done. OBS's own paste skips it the same way. */
		return;
	}

	AddData data;
	data.source = adding;
	data.visible = g_item_clip.visible;
	data.transform = &g_item_clip.transform;
	data.crop = &g_item_clip.crop;
	data.blend_method = &g_item_clip.blend_method;
	data.blend_mode = &g_item_clip.blend_mode;

	add_to_scene(scene, data);
}

void copy_filters(const std::string &source_uuid)
{
	OBSSourceAutoRelease source = obs_get_source_by_uuid(source_uuid.c_str());
	if (source)
		hold(g_filter_clip, source);
}

bool clipboard_has_filters()
{
	return still_there(g_filter_clip);
}

void paste_filters(const std::string &source_uuid)
{
	OBSSourceAutoRelease from = g_filter_clip ? obs_weak_source_get_source(g_filter_clip) : nullptr;
	if (!from)
		return;

	OBSSourceAutoRelease to = obs_get_source_by_uuid(source_uuid.c_str());
	if (!to || to.Get() == from.Get())
		return;

	/* Replaces the destination's filters wholesale, which is what OBS's
	 * Paste Filters does -- it is not additive. */
	obs_source_copy_filters(to, from);
}

void clear_clipboard()
{
	hold(g_item_clip.source, nullptr);
	hold(g_filter_clip, nullptr);
	hold(g_transition_clip, nullptr);
	g_transition_clip_duration = 0;
}

/* ------------------------------------------------ show and hide transitions */

std::vector<SourceType> transition_types()
{
	std::vector<SourceType> types;

	const char *id = nullptr;
	for (size_t idx = 0; obs_enum_transition_types(idx, &id); idx++) {
		if (!id)
			continue;

		types.push_back({id, safe_str(obs_source_get_display_name(id)), false});
	}

	return types;
}

ItemTransition item_transition(const std::string &owner_uuid, int64_t item_id, bool show)
{
	ItemTransition current;

	obs_sceneitem_t *item = resolve_item(owner_uuid, item_id);
	if (!item)
		return current;

	/* Borrowed: obs_sceneitem_get_transition does not add a reference. */
	obs_source_t *transition = obs_sceneitem_get_transition(item, show);
	if (transition) {
		current.id = safe_str(obs_source_get_id(transition));
		current.configurable = obs_source_configurable(transition);
	}

	current.duration_ms = static_cast<int>(obs_sceneitem_get_transition_duration(item, show));

	/* Unset shows the frontend's own transition duration, which is what the
	 * item would actually use, rather than a misleading zero. */
	if (current.duration_ms <= 0)
		current.duration_ms = obs_frontend_get_transition_duration();

	return current;
}

void set_item_transition(const std::string &owner_uuid, int64_t item_id, bool show, const std::string &type_id,
			 const std::string &new_name)
{
	obs_sceneitem_t *item = resolve_item(owner_uuid, item_id);
	if (!item)
		return;

	if (type_id.empty()) {
		obs_sceneitem_set_transition(item, show, nullptr);
		obs_sceneitem_set_transition_duration(item, show, 0);
		return;
	}

	/*
	 * Re-picking the type already in place leaves it alone. Replacing it
	 * would throw away whatever the operator configured on the existing
	 * transition, which is not what picking the same entry twice means.
	 */
	obs_source_t *current = obs_sceneitem_get_transition(item, show);
	if (current && type_id == safe_str(obs_source_get_id(current)))
		return;

	/* Private: the transition belongs to this scene item, and has no
	 * business turning up in the frontend's transition list. */
	OBSSourceAutoRelease created = obs_source_create_private(type_id.c_str(), new_name.c_str(), nullptr);
	if (!created)
		return;

	obs_sceneitem_set_transition(item, show, created);

	if (obs_sceneitem_get_transition_duration(item, show) == 0)
		obs_sceneitem_set_transition_duration(item, show,
						      static_cast<uint32_t>(obs_frontend_get_transition_duration()));
}

void set_item_transition_duration(const std::string &owner_uuid, int64_t item_id, bool show, int duration_ms)
{
	if (duration_ms <= 0)
		return;

	if (obs_sceneitem_t *item = resolve_item(owner_uuid, item_id))
		obs_sceneitem_set_transition_duration(item, show, static_cast<uint32_t>(duration_ms));
}

void open_item_transition_properties(const std::string &owner_uuid, int64_t item_id, bool show)
{
	obs_sceneitem_t *item = resolve_item(owner_uuid, item_id);
	if (!item)
		return;

	obs_source_t *transition = obs_sceneitem_get_transition(item, show);
	if (transition && obs_source_configurable(transition))
		obs_frontend_open_source_properties(transition);
}

void copy_item_transition(const std::string &owner_uuid, int64_t item_id, bool show)
{
	obs_sceneitem_t *item = resolve_item(owner_uuid, item_id);
	if (!item)
		return;

	obs_source_t *transition = obs_sceneitem_get_transition(item, show);
	if (!transition)
		return;

	hold(g_transition_clip, transition);
	g_transition_clip_duration = static_cast<int>(obs_sceneitem_get_transition_duration(item, show));
}

bool clipboard_has_transition()
{
	return still_there(g_transition_clip);
}

void paste_item_transition(const std::string &owner_uuid, int64_t item_id, bool show)
{
	OBSSourceAutoRelease source = g_transition_clip ? obs_weak_source_get_source(g_transition_clip) : nullptr;
	if (!source)
		return;

	obs_sceneitem_t *item = resolve_item(owner_uuid, item_id);
	if (!item)
		return;

	/*
	 * Duplicated, not shared. Two items pointing at one transition source
	 * would share its settings and its playback state; OBS duplicates here
	 * for the same reason.
	 */
	OBSSourceAutoRelease copy = obs_source_duplicate(source, obs_source_get_name(source), true);
	if (!copy)
		return;

	obs_sceneitem_set_transition(item, show, copy);
	obs_sceneitem_set_transition_duration(item, show, static_cast<uint32_t>(g_transition_clip_duration));
}

/* ------------------------------------------------------------ adding sources */

std::vector<SourceType> input_types()
{
	std::vector<SourceType> types;

	const char *id = nullptr;
	const char *unversioned = nullptr;

	for (size_t idx = 0; obs_enum_input_types2(idx, &id, &unversioned); idx++) {
		if (!id || !unversioned)
			continue;

		const uint32_t caps = obs_get_source_output_flags(id);

		/* CAP_DISABLED is how a plugin says "loaded, but not usable
		 * here" -- offering it would produce a source that cannot run. */
		if ((caps & OBS_SOURCE_CAP_DISABLED) != 0)
			continue;

		/*
		 * The unversioned id is what gets stored and what
		 * obs_get_latest_input_type_id resolves at creation time, so a
		 * later version of the plugin picks up the newer type.
		 */
		types.push_back(
			{unversioned, safe_str(obs_source_get_display_name(id)), (caps & OBS_SOURCE_DEPRECATED) != 0});
	}

	return types;
}

namespace {

struct SourceScan {
	std::string type_id;
	obs_scene_t *target = nullptr;
	std::vector<std::string> names;
};

bool collect_source(void *param, obs_source_t *source)
{
	SourceScan *scan = static_cast<SourceScan *>(param);

	/* Hidden sources are OBS's internal ones; its own dialog skips them. */
	if (obs_source_is_hidden(source))
		return true;

	const char *id = obs_source_get_unversioned_id(source);
	if (!id || scan->type_id != id)
		return true;

	const char *name = obs_source_get_name(source);
	if (!name)
		return true;

	/* A group cannot be referenced twice in one scene, so one already in
	 * the target is not offered. */
	if (scan->target && obs_scene_get_group(scan->target, name))
		return true;

	scan->names.emplace_back(name);
	return true;
}

} // namespace

std::vector<std::string> addable_sources(const std::string &owner_uuid, const std::string &type_id)
{
	OBSSourceAutoRelease owner;

	SourceScan scan;
	scan.type_id = type_id;
	scan.target = resolve_scene(owner_uuid, owner);

	if (!scan.target)
		return {};

	if (type_id == "scene") {
		/*
		 * Scenes are not in obs_enum_sources -- it walks inputs and
		 * groups only -- so they come from the frontend's own list,
		 * which is also the order the operator sees them in.
		 *
		 * The target is left out so a scene cannot be added to itself.
		 * Deeper cycles are libobs's to refuse, and it does: obs_scene_add
		 * rejects a child that already has this scene beneath it, so a
		 * bad pick adds nothing rather than building a loop. OBS's own
		 * dialog filters exactly this much.
		 */
		obs_frontend_source_list scenes = {};
		obs_frontend_get_scenes(&scenes);

		for (size_t i = 0; i < scenes.sources.num; i++) {
			obs_source_t *scene_source = scenes.sources.array[i];
			if (!scene_source || scene_source == owner.Get())
				continue;

			if (const char *name = obs_source_get_name(scene_source))
				scan.names.emplace_back(name);
		}

		obs_frontend_source_list_free(&scenes);

	} else {
		obs_enum_sources(collect_source, &scan);
		std::sort(scan.names.begin(), scan.names.end());
	}

	return std::move(scan.names);
}

bool source_name_taken(const std::string &name)
{
	OBSSourceAutoRelease taken = obs_get_source_by_name(name.c_str());
	return taken != nullptr;
}

bool add_new_source(const std::string &owner_uuid, const std::string &type_id, const std::string &name, bool visible)
{
	if (name.empty() || source_name_taken(name))
		return false;

	OBSSourceAutoRelease owner;
	obs_scene_t *scene = resolve_scene(owner_uuid, owner);
	if (!scene)
		return false;

	/* Resolves the unversioned id to whatever version is installed. */
	const char *versioned = obs_get_latest_input_type_id(type_id.c_str());

	OBSSourceAutoRelease source =
		obs_source_create(versioned ? versioned : type_id.c_str(), name.c_str(), nullptr, nullptr);
	if (!source)
		return false;

	AddData data;
	data.source = source;
	data.visible = visible;

	add_to_scene(scene, data);

	/* Matches OBS: a source that asks to be monitored by default gets
	 * monitoring turned on at creation, not left silent. */
	if ((obs_source_get_output_flags(source) & OBS_SOURCE_MONITOR_BY_DEFAULT) != 0)
		obs_source_set_monitoring_type(source, OBS_MONITORING_TYPE_MONITOR_ONLY);

	return true;
}

bool add_existing_source(const std::string &owner_uuid, const std::string &source_name, bool duplicate, bool visible)
{
	OBSSourceAutoRelease source = obs_get_source_by_name(source_name.c_str());
	if (!source)
		return false;

	OBSSourceAutoRelease owner;
	obs_scene_t *scene = resolve_scene(owner_uuid, owner);
	if (!scene)
		return false;

	OBSSourceAutoRelease copy;
	obs_source_t *adding = source;

	if (duplicate) {
		copy = obs_source_duplicate(source, unique_source_name(source_name).c_str(), false);
		if (!copy)
			return false;

		adding = copy;

	} else if (obs_scene_get_group(scene, source_name.c_str())) {
		return false;
	}

	AddData data;
	data.source = adding;
	data.visible = visible;

	add_to_scene(scene, data);
	return true;
}

} // namespace xray
