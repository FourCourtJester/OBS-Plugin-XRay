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

/*
 * Tests for SceneWalk against a stand-in libobs.
 *
 * SceneWalk is the only part of the plugin with no Qt in it, which is what
 * makes this possible: the scene graph it walks is reachable through a couple
 * of dozen libobs entry points, so this file defines those itself and links
 * SceneWalk.cpp against them. No OBS installation is needed and nothing has to
 * be running -- the scene graph is built by hand, per case.
 *
 * What that buys is coverage of the parts that are easy to get quietly wrong
 * and hard to notice in the UI: asymmetric pruning, which containers get
 * watched, the ownership of items inside groups, and the ordering arithmetic
 * behind a drag.
 *
 * What it does not buy is a check on the model itself. Every assumption the
 * plugin makes about libobs is also baked into these stubs, so a wrong
 * assumption passes here and fails in OBS -- which is exactly how the
 * display-order bug survived until it was seen on screen. When a case here
 * disagrees with OBS, the stub is what is wrong.
 *
 * Build with -DENABLE_TESTS=ON and run via ctest.
 */

#include <cstdint>
#include <cstring>
#include <iostream>
#include <memory>
#include <string>
#include <algorithm>
#include <vector>

struct obs_scene;
struct obs_source;
struct obs_sceneitem;

struct obs_source {
	std::string name;
	std::string uuid;
	std::string id; /* "scene", "group", or anything else */
	obs_scene *scene = nullptr;
};

struct obs_sceneitem {
	obs_source *source = nullptr;
	int64_t id = 0;
	bool visible = true;
	bool locked = false;
	bool selected = false;
	/* Tri-state, matching obs_data: unset until something writes it. */
	bool collapsedSet = false;
	bool collapsed = false;
	long refs = 1;
};

struct obs_scene {
	obs_source *source = nullptr;
	std::vector<obs_sceneitem *> items;
};

/* Minimal obs_data stand-in: one bool per scene item, which is all the walk
 * reads and all set_item_collapsed writes. */
struct obs_data {
	obs_sceneitem *item = nullptr;
};

typedef struct obs_scene obs_scene_t;
typedef struct obs_source obs_source_t;
typedef struct obs_sceneitem obs_sceneitem_t;
typedef struct obs_data obs_data_t;

#include "SceneWalk.hpp"

/* ---- stub libobs ---- */

static obs_source *g_program = nullptr;
static long g_live_item_refs = 0;
static std::vector<std::unique_ptr<obs_source>> g_sources;
static std::vector<std::unique_ptr<obs_scene>> g_scenes;
static std::vector<std::unique_ptr<obs_sceneitem>> g_items;

extern "C" {

void obs_scene_enum_items(obs_scene_t *scene, bool (*cb)(obs_scene_t *, obs_sceneitem_t *, void *), void *param)
{
	if (!scene)
		return;
	for (obs_sceneitem *item : scene->items)
		if (!cb(scene, item, param))
			break;
}

obs_source_t *obs_sceneitem_get_source(const obs_sceneitem_t *item)
{
	return item ? item->source : nullptr;
}

void obs_sceneitem_addref(obs_sceneitem_t *item)
{
	if (item) {
		item->refs++;
		g_live_item_refs++;
	}
}

void obs_sceneitem_release(obs_sceneitem_t *item)
{
	if (item) {
		item->refs--;
		g_live_item_refs--;
	}
}

int64_t obs_sceneitem_get_id(const obs_sceneitem_t *item)
{
	return item ? item->id : -1;
}

bool obs_sceneitem_visible(const obs_sceneitem_t *item)
{
	return item ? item->visible : false;
}

bool obs_sceneitem_locked(const obs_sceneitem_t *item)
{
	return item ? item->locked : false;
}

bool obs_sceneitem_set_visible(obs_sceneitem_t *item, bool v)
{
	if (!item)
		return false;
	item->visible = v;
	return true;
}

bool obs_sceneitem_set_locked(obs_sceneitem_t *item, bool v)
{
	if (!item)
		return false;
	item->locked = v;
	return true;
}

obs_sceneitem_t *obs_scene_find_sceneitem_by_id(obs_scene_t *scene, int64_t id)
{
	if (!scene)
		return nullptr;
	for (obs_sceneitem *item : scene->items)
		if (item->id == id)
			return item;
	return nullptr;
}

obs_scene_t *obs_scene_from_source(const obs_source_t *s)
{
	return (s && s->id == "scene") ? s->scene : nullptr;
}

obs_scene_t *obs_group_from_source(const obs_source_t *s)
{
	return (s && s->id == "group") ? s->scene : nullptr;
}

obs_source_t *obs_scene_get_source(const obs_scene_t *scene)
{
	return scene ? scene->source : nullptr;
}

const char *obs_source_get_name(const obs_source_t *s)
{
	return s ? s->name.c_str() : nullptr;
}

const char *obs_source_get_uuid(const obs_source_t *s)
{
	return s ? s->uuid.c_str() : nullptr;
}

const char *obs_source_get_id(const obs_source_t *s)
{
	return s ? s->id.c_str() : nullptr;
}

obs_source_t *obs_get_source_by_uuid(const char *uuid)
{
	if (!uuid)
		return nullptr;
	for (auto &src : g_sources)
		if (src->uuid == uuid)
			return src.get();
	return nullptr;
}

void obs_source_release(obs_source_t *) {}

obs_source_t *obs_frontend_get_current_scene(void)
{
	return g_program;
}

obs_data_t *obs_sceneitem_get_private_settings(obs_sceneitem_t *item)
{
	obs_data *d = new obs_data;
	d->item = item;
	return d;
}

void obs_data_release(obs_data_t *d)
{
	delete d;
}

bool obs_data_get_bool(obs_data_t *d, const char *key)
{
	return (d && d->item && std::string(key) == "xray_collapsed") ? d->item->collapsed : false;
}

void obs_data_set_bool(obs_data_t *d, const char *key, bool v)
{
	if (d && d->item && std::string(key) == "xray_collapsed") {
		d->item->collapsed = v;
		d->item->collapsedSet = true;
	}
}

bool obs_data_has_user_value(obs_data_t *d, const char *key)
{
	return d && d->item && std::string(key) == "xray_collapsed" && d->item->collapsedSet;
}

bool obs_sceneitem_selected(const obs_sceneitem_t *item)
{
	return item ? item->selected : false;
}

void obs_sceneitem_select(obs_sceneitem_t *item, bool select)
{
	if (item)
		item->selected = select;
}

void obs_sceneitem_set_order_position(obs_sceneitem_t *item, int position)
{
	if (!item)
		return;
	obs_scene *scene = nullptr;
	for (auto &sc : g_scenes)
		for (obs_sceneitem *i : sc->items)
			if (i == item)
				scene = sc.get();
	if (!scene)
		return;

	auto &v = scene->items;
	v.erase(std::remove(v.begin(), v.end(), item), v.end());
	if (position < 0)
		position = 0;
	if (position > (int)v.size())
		position = (int)v.size();
	v.insert(v.begin() + position, item);
}

obs_source_t *obs_get_source_by_name(const char *name)
{
	if (!name)
		return nullptr;
	for (auto &src : g_sources)
		if (src->name == name)
			return src.get();
	return nullptr;
}

void obs_source_set_name(obs_source_t *s, const char *name)
{
	if (s && name)
		s->name = name;
}

/*
 * The frontend dialogs cannot be opened here, so the stubs record what was
 * asked for instead. That is enough to check the guards: a source with no
 * properties, or no interaction, must not be passed on.
 */
static std::string g_opened;

bool obs_source_configurable(const obs_source_t *s)
{
	/* Mirrors libobs: sources with no settings UI are not configurable. */
	return s && s->id != "scene" && s->id != "group";
}

uint32_t obs_source_get_output_flags(const obs_source_t *s)
{
	/* OBS_SOURCE_INTERACTION is 1 << 5. */
	return (s && s->id == "browser_source") ? (1u << 5) : 0u;
}

void obs_frontend_open_source_properties(obs_source_t *s)
{
	g_opened += "props:" + (s ? s->name : std::string("?")) + " ";
}

void obs_frontend_open_source_filters(obs_source_t *s)
{
	g_opened += "filters:" + (s ? s->name : std::string("?")) + " ";
}

void obs_frontend_open_source_interaction(obs_source_t *s)
{
	g_opened += "interact:" + (s ? s->name : std::string("?")) + " ";
}

} /* extern "C" */

/* ---- graph builders ---- */

static obs_source *make(const std::string &name, const std::string &id)
{
	auto src = std::make_unique<obs_source>();
	src->name = name;
	src->uuid = "uuid-" + name;
	src->id = id;
	obs_source *raw = src.get();
	if (id == "scene" || id == "group") {
		auto sc = std::make_unique<obs_scene>();
		sc->source = raw;
		src->scene = sc.get();
		g_scenes.push_back(std::move(sc));
	}
	g_sources.push_back(std::move(src));
	return raw;
}

static int64_t g_next_id = 1;

static obs_sceneitem *add(obs_source *parent, obs_source *child)
{
	auto item = std::make_unique<obs_sceneitem>();
	item->source = child;
	item->id = g_next_id++;
	obs_sceneitem *raw = item.get();
	parent->scene->items.push_back(raw);
	g_items.push_back(std::move(item));
	return raw;
}

/* ---- assertions ---- */

static int g_failures = 0;

static void render(const std::vector<xray::Node> &nodes, int depth, std::string &out)
{
	for (const xray::Node &n : nodes) {
		out += std::string(depth * 2, ' ');
		out += (n.kind == xray::NodeKind::SubScene) ? "S:" : (n.kind == xray::NodeKind::Group) ? "G:" : "-:";
		out += n.name;
		if (n.cyclic)
			out += "*";
		out += "\n";
		render(n.children, depth + 1, out);
	}
}

static void check(const char *label, const std::string &got, const std::string &want)
{
	if (got == want) {
		std::cout << "PASS  " << label << "\n";
	} else {
		g_failures++;
		std::cout << "FAIL  " << label << "\n--- got ---\n" << got << "--- want ---\n" << want;
	}
}

static void reset()
{
	g_items.clear();
	g_scenes.clear();
	g_sources.clear();
	g_program = nullptr;
}

static std::string run()
{
	std::string out;
	render(xray::walk_program_scene().tree, 0, out);
	return out;
}

/* Watched uuids, sorted, with a "!" suffix marking containers. */
static std::string watched()
{
	std::vector<std::string> names;
	for (const xray::Watch &w : xray::walk_program_scene().watches)
		names.push_back(w.uuid.substr(5) + (w.container ? "!" : ""));
	std::sort(names.begin(), names.end());
	std::string out;
	for (const std::string &n : names)
		out += n + " ";
	return out;
}

int main()
{
	/* 1. Plain sources at the top level are pruned away entirely. */
	reset();
	g_program = make("Program", "scene");
	add(g_program, make("Webcam", "input"));
	add(g_program, make("Mic", "input"));
	check("top-level plain sources pruned", run(), "");

	/* 2. A subscene shows, and everything inside it shows. */
	reset();
	g_program = make("Program", "scene");
	obs_source *lower = make("Lower Third", "scene");
	add(lower, make("Name Text", "text"));
	add(lower, make("Logo", "image"));
	add(g_program, make("Webcam", "input"));
	add(g_program, lower);
	check("subscene lists all children", run(),
	      "S:Lower Third\n"
	      "  -:Logo\n"
	      "  -:Name Text\n");

	/* 3. A group on the path to a subscene is kept as a container. */
	reset();
	g_program = make("Program", "scene");
	obs_source *grp = make("Overlays", "group");
	obs_source *bug = make("Bug", "scene");
	add(bug, make("Bug Image", "image"));
	add(grp, bug);
	add(g_program, grp);
	check("group on path to subscene kept", run(),
	      "G:Overlays\n"
	      "  S:Bug\n"
	      "    -:Bug Image\n");

	/* 4. A group with no subscene beneath it is pruned. */
	reset();
	g_program = make("Program", "scene");
	obs_source *plain = make("Plain Group", "group");
	add(plain, make("Box", "color_source"));
	add(g_program, plain);
	check("group with no subscene pruned", run(), "");

	/* 5. Inside a subscene, a plain group is kept -- pruning stops there. */
	reset();
	g_program = make("Program", "scene");
	obs_source *sub = make("Sub", "scene");
	obs_source *inner = make("Inner Group", "group");
	add(inner, make("Box", "color_source"));
	add(sub, inner);
	add(g_program, sub);
	check("plain group inside subscene kept", run(),
	      "S:Sub\n"
	      "  G:Inner Group\n"
	      "    -:Box\n");

	/* 6. The same subscene referenced twice renders twice. */
	reset();
	g_program = make("Program", "scene");
	obs_source *dup = make("Dup", "scene");
	add(dup, make("Thing", "image"));
	add(g_program, dup);
	add(g_program, dup);
	check("duplicate subscene rendered twice", run(),
	      "S:Dup\n"
	      "  -:Thing\n"
	      "S:Dup\n"
	      "  -:Thing\n");

	/* 7. A cycle is flagged and not descended. */
	reset();
	g_program = make("Program", "scene");
	obs_source *a = make("A", "scene");
	obs_source *b = make("B", "scene");
	add(a, b);
	add(b, a); /* A -> B -> A */
	add(g_program, a);
	check("cycle flagged, not descended", run(),
	      "S:A\n"
	      "  S:B\n"
	      "    S:A*\n");

	/* 8. A scene referencing the program scene is caught too. */
	reset();
	g_program = make("Program", "scene");
	obs_source *self = make("Self", "scene");
	add(self, g_program);
	add(g_program, self);
	check("program scene on ancestor path caught", run(),
	      "S:Self\n"
	      "  S:Program*\n");

	/* 8b. An empty program scene still watches itself, so the first
	 * subscene added to it wakes the dock up. */
	reset();
	g_program = make("Program", "scene");
	check("empty program scene still watched", watched(), "Program! ");

	/* 8c. A pruned group is still watched -- otherwise dropping a scene
	 * into it would never make it appear. */
	reset();
	g_program = make("Program", "scene");
	obs_source *pruned = make("Pruned Group", "group");
	add(pruned, make("Box", "color_source"));
	add(g_program, pruned);
	check("pruned group still watched", watched(), "Program! Pruned Group! ");

	/* 8d. Leaf sources inside a subscene are watched non-container, so a
	 * rename redraws them; pruned top-level leaves are not watched at all. */
	reset();
	g_program = make("Program", "scene");
	obs_source *s8 = make("Sub", "scene");
	add(s8, make("Text", "text"));
	add(g_program, make("Ignored", "input"));
	add(g_program, s8);
	check("leaves inside subscene watched, pruned leaves not", watched(), "Program! Sub! Text ");

	/* 8e. A scene referenced twice yields one watch, not two. */
	reset();
	g_program = make("Program", "scene");
	obs_source *twice = make("Twice", "scene");
	add(g_program, twice);
	add(g_program, twice);
	check("duplicate subscene watched once", watched(), "Program! Twice! ");

	/* --- phase 4: mutation addressing --- */

	/* 8f. An item inside a group is owned by the group, not by the scene
	 * that contains the group. Getting this wrong makes the toggle silently
	 * hit the wrong scene or nothing at all. */
	reset();
	g_program = make("Program", "scene");
	obs_source *g4 = make("G", "group");
	obs_source *s4 = make("S", "scene");
	obs_sceneitem *inGroup = add(g4, s4);
	add(g_program, g4);
	{
		const xray::WalkResult r = xray::walk_program_scene();
		const xray::Node &groupNode = r.tree.at(0);
		const xray::Node &sceneNode = groupNode.children.at(0);
		check("item inside group owned by group",
		      sceneNode.owner_uuid + "/" + std::to_string(sceneNode.item_id),
		      std::string("uuid-G/") + std::to_string(inGroup->id));
	}

	/* 8g. Toggling visibility reaches the real scene item. */
	reset();
	g_program = make("Program", "scene");
	obs_source *s5 = make("S", "scene");
	obs_sceneitem *leaf = add(s5, make("Text", "text"));
	add(g_program, s5);
	{
		const xray::WalkResult r = xray::walk_program_scene();
		const xray::Node &node = r.tree.at(0).children.at(0);
		xray::set_item_visible(node.owner_uuid, node.item_id, false);
		xray::set_item_locked(node.owner_uuid, node.item_id, true);
	}
	check("toggles reach the scene item", std::string(leaf->visible ? "v" : "-") + (leaf->locked ? "l" : "-"),
	      "-l");

	/* 8h. Toggle states are read back into the tree. */
	{
		const xray::WalkResult r = xray::walk_program_scene();
		const xray::Node &node = r.tree.at(0).children.at(0);
		check("toggle state reflected in tree",
		      std::string(node.visible ? "v" : "-") + (node.locked ? "l" : "-"), "-l");
	}

	/* 8i. A toggle that races a deletion is dropped, not a crash. Both a
	 * vanished owner and a stale item id have to be survivable. */
	xray::set_item_visible("uuid-does-not-exist", 1, true);
	xray::set_item_locked("uuid-does-not-exist", 1, true);
	xray::set_item_visible("uuid-S", 999999, true);
	xray::set_item_locked("uuid-S", 999999, true);
	std::cout << "PASS  stale toggle targets ignored\n";

	/* 8j. Every row carries the source id its icon is chosen from. */
	reset();
	g_program = make("Program", "scene");
	obs_source *s6 = make("S", "scene");
	add(s6, make("Cam", "dshow_input"));
	add(g_program, s6);
	{
		const xray::WalkResult r = xray::walk_program_scene();
		const xray::Node &scene = r.tree.at(0);
		check("source ids carried for icon lookup", scene.source_id + "," + scene.children.at(0).source_id,
		      "scene,dshow_input");
	}

	/* --- phase 5: reorder, rename, collapse --- */

	/* 8k. Reorder anchored on a sibling, with pruning in play. The scene
	 * holds [Cam, S1, Box, S2]; only S1 and S2 are displayed, so a displayed
	 * index is not an order position. Dragging S2 above S1 must land it
	 * immediately before S1 in the real scene order. */
	reset();
	g_program = make("Program", "scene");
	add(g_program, make("Cam", "input"));
	obs_source *s1 = make("S1", "scene");
	add(g_program, s1);
	add(g_program, make("Box", "color_source"));
	obs_source *s2 = make("S2", "scene");
	add(g_program, s2);
	/* Scene order [Cam, S1, Box, S2] displays as [S2, Box, S1, Cam], of
	 * which only S2 and S1 are drawn. Dragging S1 above S2 must end with
	 * S1 drawn first, which is S1 *last* in scene order. */
	{
		int64_t s1id = g_program->scene->items[1]->id;
		int64_t s2id = g_program->scene->items[3]->id;
		xray::move_item_before("uuid-Program", s1id, s2id);
	}
	{
		std::string order;
		for (obs_sceneitem *i : g_program->scene->items)
			order += i->source->name + " ";
		check("reorder above a pruned-past sibling", order, "Cam Box S2 S1 ");
	}

	/* 8l. Dropping past the last row moves to the bottom of the display,
	 * which is the front of the scene's own order. */
	{
		int64_t s2id = -1;
		for (obs_sceneitem *i : g_program->scene->items)
			if (i->source->name == "S2")
				s2id = i->id;
		xray::move_item_before("uuid-Program", s2id, -1);
		std::string order;
		for (obs_sceneitem *i : g_program->scene->items)
			order += i->source->name + " ";
		check("reorder to the bottom of the display", order, "S2 Cam Box S1 ");
	}

	/* 8m. Reorder inside a group addresses the group, not the outer scene. */
	reset();
	g_program = make("Program", "scene");
	obs_source *g5 = make("G", "group");
	obs_source *a5 = make("A", "scene");
	obs_source *b5 = make("B", "scene");
	obs_sceneitem *ai = add(g5, a5);
	obs_sceneitem *bi = add(g5, b5);
	add(g_program, g5);
	/* Group holds [A, B], drawn [B, A]. Dragging A above B ends with A
	 * drawn first, so B first in scene order. */
	xray::move_item_before("uuid-G", ai->id, bi->id);
	{
		std::string order;
		for (obs_sceneitem *i : g5->scene->items)
			order += i->source->name + " ";
		check("reorder within a group", order, "B A ");
	}

	/* 8n. A reorder against a vanished scene or item is dropped. */
	xray::move_item_before("uuid-gone", 1, 2);
	xray::move_item_before("uuid-G", 999999, ai->id);
	xray::move_item_before("uuid-G", ai->id, 999999); /* unknown anchor -> end */
	std::cout << "PASS  stale reorder targets ignored\n";

	/* 8o. Rename writes through; empty and duplicate names are refused. */
	reset();
	g_program = make("Program", "scene");
	obs_source *r1 = make("Alpha", "scene");
	obs_source *r2 = make("Beta", "scene");
	add(g_program, r1);
	add(g_program, r2);
	{
		bool ok1 = xray::rename_source("uuid-Alpha", "Gamma");
		bool ok2 = xray::rename_source("uuid-Beta", "");
		bool ok3 = xray::rename_source("uuid-Beta", "Gamma");
		bool ok4 = xray::rename_source("uuid-gone", "Whatever");
		check("rename accepts, refuses empty/duplicate/missing",
		      std::string(ok1 ? "1" : "0") + (ok2 ? "1" : "0") + (ok3 ? "1" : "0") + (ok4 ? "1" : "0") + " " +
			      r1->name + "/" + r2->name,
		      "1000 Gamma/Beta");
	}

	/* 8p. Renaming to the current name is a no-op success, not a self-clash. */
	check("rename to same name succeeds", xray::rename_source("uuid-Alpha", "Gamma") ? "ok" : "refused", "ok");

	/* 8q. Collapsed round-trips through the scene item, and collapsing does
	 * not change what the walk produces -- only what the dock draws. */
	reset();
	g_program = make("Program", "scene");
	obs_source *c1 = make("Sub", "scene");
	add(c1, make("Inner", "text"));
	obs_sceneitem *ci = add(g_program, c1);
	xray::set_item_collapsed("uuid-Program", ci->id, true);
	{
		const xray::WalkResult r = xray::walk_program_scene();
		check("collapsed round-trips and subtree still walked",
		      std::string(r.tree.at(0).collapsed ? "collapsed" : "open") + "/" +
			      std::to_string(r.tree.at(0).children.size()),
		      "collapsed/1");
	}

	/* 8r. Display order is the reverse of the scene's own order. OBS builds
	 * its list with items.insert(0, item) in SourceTreeModel::enumItem, so
	 * first_item is the bottom row. Walking forwards renders every list
	 * upside down relative to the Sources dock. */
	reset();
	g_program = make("Program", "scene");
	obs_source *host = make("Host", "scene");
	add(host, make("First", "text")); /* added first -> drawn last */
	add(host, make("Second", "text"));
	add(host, make("Third", "text")); /* added last -> drawn first */
	add(g_program, host);
	check("display order is reverse of scene order", run(),
	      "S:Host\n"
	      "  -:Third\n"
	      "  -:Second\n"
	      "  -:First\n");

	/* 8s. Two subscenes side by side at the top level, the case that
	 * surfaced this: they must appear in the same order as OBS shows them. */
	reset();
	g_program = make("Program", "scene");
	add(g_program, make("SceneA", "scene"));
	add(g_program, make("GroupB", "group"));
	add(g_program->scene->items[1]->source, make("Nested", "scene"));
	check("top-level siblings match OBS order", run(),
	      "G:GroupB\n"
	      "  S:Nested\n"
	      "S:SceneA\n");

	/* --- collapse defaults, collapse-all, selection --- */

	/* 8t. Branches start collapsed, so a deep tree opens tidy. The stub only
	 * honours the "xray_collapsed" key, so this also proves the plugin is not
	 * writing OBS's own "collapsed" key -- sharing that would collapse the
	 * operator's groups in their real Sources dock. */
	reset();
	g_program = make("Program", "scene");
	obs_source *t1 = make("Sub", "scene");
	add(t1, make("Leaf", "text"));
	obs_sceneitem *ti = add(g_program, t1);
	{
		const xray::WalkResult r = xray::walk_program_scene();
		check("branches start collapsed", r.tree.at(0).collapsed ? "collapsed" : "open", "collapsed");
	}

	/* 8u. An explicit choice sticks, in both directions. */
	xray::set_item_collapsed("uuid-Program", ti->id, false);
	{
		const xray::WalkResult r = xray::walk_program_scene();
		check("explicit expand sticks", r.tree.at(0).collapsed ? "collapsed" : "open", "open");
	}
	xray::set_item_collapsed("uuid-Program", ti->id, true);
	{
		const xray::WalkResult r = xray::walk_program_scene();
		check("explicit collapse sticks", r.tree.at(0).collapsed ? "collapsed" : "open", "collapsed");
	}

	/* 8v. Collapse-all and expand-all reach nested branches, including ones
	 * hidden inside a collapsed parent -- the walk never stops at a
	 * collapsed node, so they are all reachable in one pass. */
	reset();
	g_program = make("Program", "scene");
	obs_source *outer = make("Outer", "scene");
	obs_source *nested = make("Inner", "scene");
	add(nested, make("Deep", "text"));
	add(outer, nested);
	add(g_program, outer);

	xray::set_all_collapsed(false);
	{
		const xray::WalkResult r = xray::walk_program_scene();
		const xray::Node &o = r.tree.at(0);
		check("expand all reaches nested branches",
		      std::string(o.collapsed ? "c" : "o") + (o.children.at(0).collapsed ? "c" : "o"), "oo");
	}

	xray::set_all_collapsed(true);
	{
		const xray::WalkResult r = xray::walk_program_scene();
		const xray::Node &o = r.tree.at(0);
		check("collapse all reaches nested branches",
		      std::string(o.collapsed ? "c" : "o") + (o.children.at(0).collapsed ? "c" : "o"), "cc");
	}

	/* 8w. Selection is carried onto the row, which is what lets a pick in
	 * OBS's Sources dock highlight and scroll to the match here. */
	reset();
	g_program = make("Program", "scene");
	obs_source *sel = make("Picked", "scene");
	obs_sceneitem *si = add(g_program, sel);
	add(g_program, make("Other", "scene"));
	obs_sceneitem_select(si, true);
	{
		const xray::WalkResult r = xray::walk_program_scene();
		std::string flags;
		for (const xray::Node &n : r.tree)
			flags += n.name + (n.selected ? "=on " : "=off ");
		check("selection carried onto rows", flags, "Other=off Picked=on ");
	}

	/* 8x. Double-click and the context menu route through these. A source
	 * with no properties must not be handed to the frontend, and neither
	 * must one that does not support interaction -- OBS greys both out
	 * rather than opening an empty dialog. */
	reset();
	g_opened.clear();
	g_program = make("Program", "scene");
	obs_source *host2 = make("Host", "scene");
	add(host2, make("Cam", "dshow_input"));
	add(host2, make("Web", "browser_source"));
	add(g_program, host2);

	xray::open_source_properties("uuid-Cam");     /* configurable -> opens */
	xray::open_source_properties("uuid-Host");    /* a scene -> refused */
	xray::open_source_properties("uuid-missing"); /* gone -> refused */
	xray::open_source_interaction("uuid-Web");    /* interactive -> opens */
	xray::open_source_interaction("uuid-Cam");    /* not interactive -> refused */
	xray::open_source_filters("uuid-Cam");        /* always allowed */
	check("frontend dialogs guarded by capability", g_opened, "props:Cam interact:Web filters:Cam ");

	/* 8y. The menu greys entries out using the same answers. */
	check("capability queries agree with the guards",
	      std::string(xray::source_is_configurable("uuid-Cam") ? "1" : "0") +
		      (xray::source_is_configurable("uuid-Host") ? "1" : "0") +
		      (xray::source_is_interactive("uuid-Web") ? "1" : "0") +
		      (xray::source_is_interactive("uuid-Cam") ? "1" : "0") +
		      (xray::source_is_configurable("uuid-missing") ? "1" : "0"),
	      "10100");

	/* 9. Deep nesting terminates and stays balanced. */
	reset();
	g_program = make("Program", "scene");
	obs_source *prev = g_program;
	for (int i = 0; i < 100; i++) {
		obs_source *next = make("L" + std::to_string(i), "scene");
		add(prev, next);
		prev = next;
	}
	std::string deep = run();
	size_t rows = 0;
	for (char c : deep)
		if (c == '\n')
			rows++;
	if (rows > 0 && rows < 100) {
		std::cout << "PASS  depth cap terminates (" << rows << " rows)\n";
	} else {
		g_failures++;
		std::cout << "FAIL  depth cap terminates (" << rows << " rows)\n";
	}

	/* 10. Every scene-item reference taken during the walk was released. */
	if (g_live_item_refs == 0) {
		std::cout << "PASS  no leaked sceneitem refs\n";
	} else {
		g_failures++;
		std::cout << "FAIL  leaked " << g_live_item_refs << " sceneitem refs\n";
	}

	std::cout << (g_failures ? "\nFAILURES: " : "\nall green: ") << g_failures << "\n";
	return g_failures != 0;
}
