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

#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace xray {

enum class NodeKind {
	SubScene, /* a scene used as a source */
	Group,    /* a group container */
	Source,   /* an ordinary source inside a subscene */
};

struct Node {
	std::string name;
	NodeKind kind = NodeKind::Source;

	/* obs_source_get_id() of the row's source, used to pick its icon. */
	std::string source_id;

	/* Identifies the source itself, for rename. */
	std::string source_uuid;

	/*
	 * Whether this branch is drawn collapsed, and whether the scene item is
	 * selected in OBS.
	 *
	 * Collapsed is stored on the scene item's private settings under a key of
	 * this plugin's own, NOT the "collapsed" key OBS uses for groups. Sharing
	 * that key would mean defaulting branches to collapsed also collapsed the
	 * operator's groups in the real Sources dock, which is not this dock's
	 * business. The cost is that the two lists no longer agree about group
	 * collapse; that is the intended trade.
	 *
	 * Unset means collapsed, so deep trees open tidy. Once a row is toggled
	 * the choice is explicit and persists with the scene collection.
	 *
	 * Collapsing only hides rows. The subtree is still walked, because
	 * pruning depends on what is underneath.
	 */
	bool collapsed = true;
	bool selected = false;

	/*
	 * Identifies the scene item behind this row, for mutation. The pair is
	 * deliberately not a pointer: a row can outlive the item it was built
	 * from, so both are re-resolved at the moment of the toggle and the
	 * operation is dropped if either has gone.
	 *
	 * owner_uuid is the scene or group source that contains the item --
	 * groups are scenes internally, so their items belong to the group.
	 */
	std::string owner_uuid;
	int64_t item_id = -1;

	bool visible = true;
	bool locked = false;

	/*
	 * Set when this subscene already appears on its own ancestor path. The
	 * row is still rendered, but the walk does not descend through it.
	 */
	bool cyclic = false;

	std::vector<Node> children;
};

/*
 * A source whose signals have to be watched to keep the tree honest.
 *
 * Containers are scene and group sources; they carry the item_* and reorder
 * signals. Everything else is a rendered leaf, watched only so a rename or a
 * destroy redraws its row.
 */
struct Watch {
	std::string uuid;
	bool container = false;
};

struct WalkResult {
	std::vector<Node> tree;

	/*
	 * Includes every container the walk descended into, even ones that were
	 * pruned out of the tree. A group with no subscene beneath it earns no
	 * row, but it still has to be watched -- otherwise dropping a scene into
	 * that group would never make it appear.
	 */
	std::vector<Watch> watches;
};

/*
 * Walks the current program scene and returns its subscene branches.
 *
 * Pruning is asymmetric, and deliberately so. Above a subscene only items on a
 * path to one survive: an ordinary source sitting at the top level of the
 * program scene does not get a row, because the stock Sources dock already
 * shows it, and a group earns a row only if a subscene turned up beneath it.
 * At and below a subscene everything is listed, because seeing inside the
 * subscene is the entire point of the dock.
 *
 * The tree is empty when the program scene contains no scene sources; watches
 * never is, since the program scene itself is always watched.
 */
WalkResult walk_program_scene();

/*
 * Toggles for a single scene item, addressed by owning source and item id.
 *
 * Both re-resolve the item and silently do nothing if the scene or the item has
 * gone since the row was built -- which is the normal outcome of a click that
 * races a deletion, not an error.
 *
 * The change lands on the underlying scene, so it shows up everywhere that
 * scene is used. That is the intended behaviour of this dock, not a side
 * effect, so there is deliberately no guard or confirmation here.
 */
void set_item_visible(const std::string &owner_uuid, int64_t item_id, bool visible);
void set_item_locked(const std::string &owner_uuid, int64_t item_id, bool locked);

void set_item_collapsed(const std::string &owner_uuid, int64_t item_id, bool collapsed);

/* Collapses or expands every branch under the program scene, in one pass. */
void set_all_collapsed(bool collapsed);

/*
 * Moves item_id so it is drawn immediately above before_item_id; a
 * before_item_id of -1 moves it to the bottom of the list. Both are stated in
 * display order, which is what a drop actually means.
 *
 * Two things make this less obvious than it looks. Display order is the reverse
 * of the scene's own order -- OBS builds its list with items.insert(0, item),
 * so first_item is the bottom row -- and at the top level the displayed rows
 * are a pruned subset, so a row index is not an order position. Both are
 * handled by resolving against the scene's real ordering at the moment of the
 * drop rather than by arithmetic on what happens to be on screen.
 */
void move_item_before(const std::string &owner_uuid, int64_t item_id, int64_t before_item_id);

/*
 * Renames the source behind a row. Returns false without renaming if the name
 * is empty or already taken -- source names are the identity OBS uses in the
 * UI, and duplicates are rejected rather than silently uniquified.
 */
bool rename_source(const std::string &source_uuid, const std::string &new_name);

/*
 * Opens OBS's own dialogs for the source behind a row. Each is a no-op if the
 * source has gone since the row was drawn, which is the normal outcome of a
 * click that races a deletion.
 *
 * These reach the real windows OBS uses from its Sources dock, so a nested
 * source gets the same Properties, Filters and Interact it would get if it were
 * top level -- which is the point of the dock.
 */
void open_source_properties(const std::string &source_uuid);
void open_source_filters(const std::string &source_uuid);
void open_source_interaction(const std::string &source_uuid);

/* Whether the corresponding menu entry should be offered at all. */
bool source_is_configurable(const std::string &source_uuid);
bool source_is_interactive(const std::string &source_uuid);

} // namespace xray
