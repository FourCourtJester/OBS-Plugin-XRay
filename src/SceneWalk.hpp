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

} // namespace xray
