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
 * Which of OBS's two scenes a dock follows. Fixed for the life of a dock: the
 * panel following program never becomes the one following preview.
 *
 * Program is obs_frontend_get_current_scene(), which is the scene on air in
 * studio mode and simply the selected scene out of it -- so a program dock
 * behaves identically whether or not studio mode is on.
 *
 * Preview is obs_frontend_get_current_preview_scene(), which exists only while
 * studio mode is on. Out of studio mode a preview walk returns nothing, and the
 * dock says why rather than looking broken.
 */
enum class SceneTarget { Program, Preview };

/* Whether OBS is in studio mode, which is the only reason a preview walk comes
 * back empty when the scene collection plainly is not. */
bool studio_mode_active();

/*
 * Walks the chosen scene and returns its branches.
 *
 * With show_all false the pruning is asymmetric, and deliberately so. Above a
 * subscene only items on a path to one survive: an ordinary source sitting at
 * the top level of the program scene does not get a row, because the stock
 * Sources dock already shows it, and a group earns a row only if a subscene
 * turned up beneath it. At and below a subscene everything is listed, because
 * seeing inside the subscene is the entire point of the dock.
 *
 * With show_all true nothing above a subscene is pruned either, so the dock
 * mirrors the Sources dock and then carries on down into the nested scenes.
 * That costs the operator a duplicate of a list they already have when the
 * scene is flat, which is why it is a setting rather than the only behaviour --
 * but it also means a group holding no subscene stops silently vanishing, which
 * reads as a missing feature rather than as pruning.
 *
 * The tree is empty when the chosen scene has nothing to show, and the watches
 * are empty too when there is no such scene at all -- which is the normal state
 * of a preview walk out of studio mode, not a fault.
 */
WalkResult walk_scene_for(SceneTarget target, bool show_all = false);

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

/*
 * Collapses or expands every branch under the chosen scene, in one pass. Both
 * arguments have to match what that dock is displaying, or the sweep would miss
 * the branches that only exist in the other view.
 */
void set_all_collapsed(bool collapsed, SceneTarget target, bool show_all = false);

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
 * Scene-item properties the context menu exposes.
 *
 * Mirrored rather than pulled in from obs.h so this header stays free of libobs
 * -- which is what lets the test harness supply its own. Translation to the
 * libobs enums is explicit in the .cpp, so a reordering upstream cannot
 * silently turn one setting into another.
 */
enum class ScaleFilter { Disable, Point, Bicubic, Bilinear, Lanczos, Area };
enum class BlendingMode { Normal, Additive, Subtract, Screen, Multiply, Lighten, Darken };
enum class BlendingMethod { Default, SrgbOff };
enum class OrderMovement { Up, Down, Top, Bottom };

/*
 * Everything the menu needs to decide what to show, gathered in one resolve so
 * a right-click does not look the item up a dozen times.
 *
 * found is false when the item has gone since the row was drawn, in which case
 * the menu offers nothing rather than acting on a stale target.
 */
struct ItemProperties {
	bool found = false;

	bool has_video = false;
	bool has_audio = false;
	bool is_async_video = false;
	bool is_group = false;

	ScaleFilter scale = ScaleFilter::Disable;
	BlendingMode blending_mode = BlendingMode::Normal;
	BlendingMethod blending_method = BlendingMethod::Default;
};

ItemProperties item_properties(const std::string &owner_uuid, int64_t item_id);

void set_scale_filter(const std::string &owner_uuid, int64_t item_id, ScaleFilter filter);
void set_blending_mode(const std::string &owner_uuid, int64_t item_id, BlendingMode mode);
void set_blending_method(const std::string &owner_uuid, int64_t item_id, BlendingMethod method);

/*
 * Order movements are stated in display terms, and libobs already agrees:
 * OBS_ORDER_MOVE_UP attaches the item after its successor in the scene list,
 * which is one row higher on screen because display is the reverse of scene
 * order. No inversion is needed here, unlike move_item_before above.
 */
void set_order(const std::string &owner_uuid, int64_t item_id, OrderMovement movement);

/*
 * Deinterlacing lives on the source, not the scene item, so it is addressed by
 * source uuid and applies everywhere that source appears. OBS offers it only
 * for async video.
 */
enum class DeinterlaceMode { Disable, Discard, Retro, Blend, Blend2x, Linear, Linear2x, Yadif, Yadif2x };
enum class FieldOrder { Top, Bottom };

DeinterlaceMode deinterlace_mode(const std::string &source_uuid);
FieldOrder deinterlace_field_order(const std::string &source_uuid);
void set_deinterlace_mode(const std::string &source_uuid, DeinterlaceMode mode);
void set_deinterlace_field_order(const std::string &source_uuid, FieldOrder order);

/*
 * The transform operations OBS offers from the Transform submenu, minus the
 * Edit Transform dialog, which is an OBSBasic window with no frontend API.
 *
 * Implemented in SceneTransform.cpp rather than here: they need libobs's
 * graphics maths, and the test harness would have to stand that up too --
 * at which point the tests would be checking the stubbed matrix code rather
 * than anything of ours. The maths is a faithful port of OBSBasic's.
 */
enum class TransformOp {
	Reset,
	Rotate90CW,
	Rotate90CCW,
	Rotate180,
	FlipHorizontal,
	FlipVertical,
	FitToScreen,
	StretchToScreen,
	CenterToScreen,
	CenterVertically,
	CenterHorizontally,
};

void apply_transform(const std::string &owner_uuid, int64_t item_id, TransformOp op);

/*
 * Whether the source is kept out of the audio mixer dock, which OBS stores on
 * the source's private settings rather than the scene item -- so it applies
 * wherever the source is used.
 *
 * Writing the flag is only half the job. OBS adds and removes the mixer strip
 * from its own source_audio_activate/deactivate handlers, which a plugin
 * cannot call, so the setter nudges those signals; see the .cpp.
 */
bool source_mixer_hidden(const std::string &source_uuid);
void set_source_mixer_hidden(const std::string &source_uuid, bool hidden);

/*
 * The row tint OBS calls "Set Color".
 *
 * preset is OBS's own encoding, kept verbatim so both docks read each other's
 * colours: 0 is no tint, 1 means the custom colour in `custom`, and 2..9 are
 * the eight swatches. Unlike collapse, this one deliberately shares OBS's key
 * -- a colour is a label the operator put on the item, and it should mean the
 * same thing in both lists.
 */
struct ItemColor {
	int preset = 0;

	/* "#AARRGGBB", and only meaningful when preset is 1. */
	std::string custom;
};

ItemColor item_color(const std::string &owner_uuid, int64_t item_id);
void set_item_color(const std::string &owner_uuid, int64_t item_id, int preset, const std::string &custom);

/*
 * Copy and paste, on a clipboard of this dock's own.
 *
 * OBS's clipboard lives on OBSBasic and is not reachable from a plugin, so
 * this is a separate one: copying here does not disturb what the operator has
 * on OBS's clipboard, and vice versa. The trade is that the two do not
 * interoperate, which is the honest outcome given multi-select is not
 * supported here either.
 *
 * Paste targets owner_uuid -- the scene or group the row sits in -- which is
 * the point: it drops a source into a nested scene without leaving the one on
 * air.
 *
 * duplicate follows OBS: false pastes another reference to the same source,
 * true copies it under a new name. A reference to a group that is already in
 * the target scene is refused, exactly as OBS refuses it.
 */
void copy_item(const std::string &owner_uuid, int64_t item_id);
bool clipboard_has_item();
void paste_item(const std::string &owner_uuid, bool duplicate);

void copy_filters(const std::string &source_uuid);
bool clipboard_has_filters();
void paste_filters(const std::string &source_uuid);

/*
 * Releases every weak reference the clipboards hold. Called from the module's
 * teardown rather than left to static destruction, which would run after
 * obs_shutdown() has already freed what those references point at.
 */
void clear_clipboard();

/* An id paired with the name to show for it. */
struct SourceType {
	std::string id;
	std::string name;
	bool deprecated = false;
};

std::vector<SourceType> transition_types();

/*
 * The show or hide transition on one scene item.
 *
 * id is empty when the item has none. duration_ms falls back to the frontend's
 * default when the item has not been given one, matching what OBS shows in the
 * spin box.
 */
struct ItemTransition {
	std::string id;
	int duration_ms = 0;
	bool configurable = false;
};

ItemTransition item_transition(const std::string &owner_uuid, int64_t item_id, bool show);

/*
 * An empty type_id clears the transition. Otherwise a private transition source
 * is created under new_name if the item does not already have one of that type;
 * the caller supplies the name because it is user-facing text, and this file
 * stays clear of the module locale so the test harness can link it.
 */
void set_item_transition(const std::string &owner_uuid, int64_t item_id, bool show, const std::string &type_id,
			 const std::string &new_name);
void set_item_transition_duration(const std::string &owner_uuid, int64_t item_id, bool show, int duration_ms);
void open_item_transition_properties(const std::string &owner_uuid, int64_t item_id, bool show);

void copy_item_transition(const std::string &owner_uuid, int64_t item_id, bool show);
bool clipboard_has_transition();
void paste_item_transition(const std::string &owner_uuid, int64_t item_id, bool show);

/*
 * The input types offered by the Add Source menu, minus the ones libobs marks
 * unavailable. Scene and group are not in here -- OBS appends those to the menu
 * itself, and so does the caller.
 */
std::vector<SourceType> input_types();

/*
 * Existing sources of a type that could be added to this scene, by name.
 * Hidden sources are left out, as are groups already present in the target,
 * which OBS refuses to reference twice.
 */
std::vector<std::string> addable_sources(const std::string &owner_uuid, const std::string &type_id);

bool source_name_taken(const std::string &name);

/*
 * Both add to owner_uuid rather than to the program scene, which is what makes
 * them worth having here: a source can be added straight into a nested scene.
 *
 * add_new_source refuses a name already in use rather than uniquifying it, the
 * same as OBS's own dialog. add_existing_source uniquifies, because there it is
 * a copy and a new name is expected.
 */
bool add_new_source(const std::string &owner_uuid, const std::string &type_id, const std::string &name, bool visible);
bool add_existing_source(const std::string &owner_uuid, const std::string &source_name, bool duplicate, bool visible);

/* Dissolves a group, leaving its children in the parent scene. */
void ungroup_item(const std::string &owner_uuid, int64_t item_id);

/* Removes the item from its scene. The source survives if used elsewhere. */
void remove_item(const std::string &owner_uuid, int64_t item_id);

/* Saves a still of the source through OBS's own screenshot path. */
void screenshot_source(const std::string &source_uuid);

/* A monitor of -1 opens a projector window rather than a fullscreen projector. */
void open_source_projector(const std::string &source_uuid, int monitor);

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
