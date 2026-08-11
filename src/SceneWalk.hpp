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
 * Walks the current program scene and returns its subscene branches.
 *
 * Pruning is asymmetric, and deliberately so. Above a subscene only items on a
 * path to one survive: an ordinary source sitting at the top level of the
 * program scene does not get a row, because the stock Sources dock already
 * shows it, and a group earns a row only if a subscene turned up beneath it.
 * At and below a subscene everything is listed, because seeing inside the
 * subscene is the entire point of the dock.
 *
 * Returns an empty vector when the program scene contains no scene sources.
 */
std::vector<Node> walk_program_scene();

} // namespace xray
