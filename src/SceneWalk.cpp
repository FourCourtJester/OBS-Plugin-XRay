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

	OBSDataAutoRelease settings = obs_sceneitem_get_private_settings(item);
	node.collapsed = obs_data_get_bool(settings, "collapsed");
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
	obs_data_set_bool(settings, "collapsed", collapsed);
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

} // namespace xray
