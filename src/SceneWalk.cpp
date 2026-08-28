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

} // namespace xray
