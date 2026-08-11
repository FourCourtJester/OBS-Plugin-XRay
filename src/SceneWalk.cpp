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

const char *safe_name(obs_source_t *source)
{
	const char *name = obs_source_get_name(source);
	return name ? name : "";
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

std::vector<Node> walk_scene(obs_scene_t *scene, bool inside_subscene, int depth, State &state);

Node build_subscene(obs_source_t *source, obs_scene_t *subscene, int depth, State &state)
{
	Node node;
	node.kind = NodeKind::SubScene;
	node.name = safe_name(source);

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

	for (const OBSSceneItem &item : snapshot_items(scene)) {
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
			node.name = safe_name(source);
			node.children = walk_scene(group, inside_subscene, depth + 1, state);

			/*
			 * Groups are walked through rather than around, but a
			 * group above a subscene only earns a row if something
			 * survived beneath it. Inside a subscene it always does.
			 */
			if (inside_subscene || !node.children.empty())
				nodes.push_back(std::move(node));

		} else if (obs_scene_t *subscene = obs_scene_from_source(source)) {
			nodes.push_back(build_subscene(source, subscene, depth, state));

		} else if (inside_subscene) {
			Node node;
			node.kind = NodeKind::Source;
			node.name = safe_name(source);
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

} // namespace xray
