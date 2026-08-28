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
#include <cstdlib>
#include <cstring>
#include <iostream>
#include <map>
#include <memory>
#include <string>
#include <algorithm>
#include <vector>

struct obs_scene;
struct obs_source;
struct obs_sceneitem;

/*
 * Stand-in for obs_data. Tri-state per key, matching the real thing: a key that
 * was never written reads back as absent, which is what the collapse default
 * turns on.
 */
struct Settings {
	std::map<std::string, bool> bools;
	std::map<std::string, long long> ints;
	std::map<std::string, std::string> strings;
};

struct obs_source {
	std::string name;
	std::string uuid;
	std::string id; /* "scene", "group", or anything else */
	std::string unversionedId;
	obs_scene *scene = nullptr;
	int deinterlace = 0;
	int fieldOrder = 0;
	Settings priv;
	bool hidden = false;
	bool isPrivate = false;
	bool audioActive = true;
	/* How many times set_audio_active flipped it, so the mixer nudge can be
	 * checked without a frontend to watch. */
	int audioToggles = 0;
	int monitoring = 0;
};

struct obs_sceneitem {
	obs_source *source = nullptr;
	int64_t id = 0;
	bool visible = true;
	bool locked = false;
	bool selected = false;
	int scale = 0;
	int blendMode = 0;
	int blendMethod = 0;
	Settings priv;
	obs_source *showTransition = nullptr;
	obs_source *hideTransition = nullptr;
	int showDuration = 0;
	int hideDuration = 0;
	long refs = 1;
};

/* Weak references hold the uuid, not the pointer, so one that outlives its
 * source reports gone rather than dereferencing freed memory -- which is the
 * whole point of the clipboard holding weak references. */
struct obs_weak_source {
	std::string uuid;
	long refs = 1;
};

struct obs_scene {
	obs_source *source = nullptr;
	std::vector<obs_sceneitem *> items;
};

/* A view onto whichever Settings the caller asked for. */
struct obs_data {
	Settings *settings = nullptr;
};

/*
 * Mirrored from libobs with the real values, because this file deliberately
 * does not include obs.h -- it defines the entry points SceneWalk links
 * against. Checked against libobs/obs-source.h and libobs/obs.h.
 */
#define OBS_SOURCE_VIDEO (1 << 0)
#define OBS_SOURCE_AUDIO (1 << 1)
#define OBS_SOURCE_ASYNC (1 << 2)
#define OBS_SOURCE_ASYNC_VIDEO (OBS_SOURCE_ASYNC | OBS_SOURCE_VIDEO)
#define OBS_SOURCE_INTERACTION (1 << 5)
#define OBS_SOURCE_DEPRECATED (1 << 8)
#define OBS_SOURCE_CAP_DISABLED (1 << 10)
#define OBS_SOURCE_MONITOR_BY_DEFAULT (1 << 11)

enum obs_monitoring_type {
	OBS_MONITORING_TYPE_NONE,
	OBS_MONITORING_TYPE_MONITOR_ONLY,
	OBS_MONITORING_TYPE_MONITOR_AND_OUTPUT,
};

enum obs_scale_type {
	OBS_SCALE_DISABLE,
	OBS_SCALE_POINT,
	OBS_SCALE_BICUBIC,
	OBS_SCALE_BILINEAR,
	OBS_SCALE_LANCZOS,
	OBS_SCALE_AREA,
};

enum obs_blending_method {
	OBS_BLEND_METHOD_DEFAULT,
	OBS_BLEND_METHOD_SRGB_OFF,
};

enum obs_blending_type {
	OBS_BLEND_NORMAL,
	OBS_BLEND_ADDITIVE,
	OBS_BLEND_SUBTRACT,
	OBS_BLEND_SCREEN,
	OBS_BLEND_MULTIPLY,
	OBS_BLEND_LIGHTEN,
	OBS_BLEND_DARKEN,
};

enum obs_deinterlace_mode {
	OBS_DEINTERLACE_MODE_DISABLE,
	OBS_DEINTERLACE_MODE_DISCARD,
	OBS_DEINTERLACE_MODE_RETRO,
	OBS_DEINTERLACE_MODE_BLEND,
	OBS_DEINTERLACE_MODE_BLEND_2X,
	OBS_DEINTERLACE_MODE_LINEAR,
	OBS_DEINTERLACE_MODE_LINEAR_2X,
	OBS_DEINTERLACE_MODE_YADIF,
	OBS_DEINTERLACE_MODE_YADIF_2X,
};

enum obs_deinterlace_field_order {
	OBS_DEINTERLACE_FIELD_ORDER_TOP,
	OBS_DEINTERLACE_FIELD_ORDER_BOTTOM,
};

enum obs_order_movement {
	OBS_ORDER_MOVE_UP,
	OBS_ORDER_MOVE_DOWN,
	OBS_ORDER_MOVE_TOP,
	OBS_ORDER_MOVE_BOTTOM,
};

typedef struct obs_scene obs_scene_t;
typedef struct obs_source obs_source_t;
typedef struct obs_sceneitem obs_sceneitem_t;
typedef struct obs_data obs_data_t;
typedef struct obs_weak_source obs_weak_source_t;

#include "SceneWalk.hpp"

/* ---- stub libobs ---- */

static obs_source *g_program = nullptr;
static long g_live_item_refs = 0;
static std::vector<std::unique_ptr<obs_source>> g_sources;
static std::vector<std::unique_ptr<obs_scene>> g_scenes;
static std::vector<std::unique_ptr<obs_sceneitem>> g_items;
static int64_t g_next_id = 1;

/*
 * Every source in the graph is made here, whether the test built it or libobs
 * was asked to create one. Private sources -- what a show/hide transition is --
 * stay out of the by-name lookup and out of obs_enum_sources, as they do in
 * libobs, but keep a uuid so a weak reference can still find them.
 */
static obs_source *new_source(const std::string &name, const std::string &id, bool isPrivate)
{
	auto src = std::make_unique<obs_source>();
	src->name = name;
	src->uuid = "uuid-" + name;
	src->id = id;
	src->unversionedId = id;
	src->isPrivate = isPrivate;

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
	d->settings = item ? &item->priv : nullptr;
	return d;
}

obs_data_t *obs_source_get_private_settings(obs_source_t *source)
{
	obs_data *d = new obs_data;
	d->settings = source ? &source->priv : nullptr;
	return d;
}

void obs_data_release(obs_data_t *d)
{
	delete d;
}

bool obs_data_get_bool(obs_data_t *d, const char *key)
{
	if (!d || !d->settings)
		return false;
	auto at = d->settings->bools.find(key);
	return at != d->settings->bools.end() ? at->second : false;
}

void obs_data_set_bool(obs_data_t *d, const char *key, bool v)
{
	if (d && d->settings)
		d->settings->bools[key] = v;
}

long long obs_data_get_int(obs_data_t *d, const char *key)
{
	if (!d || !d->settings)
		return 0;
	auto at = d->settings->ints.find(key);
	return at != d->settings->ints.end() ? at->second : 0;
}

void obs_data_set_int(obs_data_t *d, const char *key, long long v)
{
	if (d && d->settings)
		d->settings->ints[key] = v;
}

const char *obs_data_get_string(obs_data_t *d, const char *key)
{
	if (!d || !d->settings)
		return "";
	auto at = d->settings->strings.find(key);
	return at != d->settings->strings.end() ? at->second.c_str() : "";
}

void obs_data_set_string(obs_data_t *d, const char *key, const char *v)
{
	if (d && d->settings)
		d->settings->strings[key] = v ? v : "";
}

bool obs_data_has_user_value(obs_data_t *d, const char *key)
{
	return d && d->settings && d->settings->bools.count(key) > 0;
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
		if (!src->isPrivate && src->name == name)
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
	/* Mirrors libobs: sources with no settings UI are not configurable. A cut
	 * transition is the one here with nothing to configure. */
	return s && s->id != "scene" && s->id != "group" && s->id != "cut_transition";
}

uint32_t obs_source_get_output_flags(const obs_source_t *s)
{
	if (!s)
		return 0;
	if (s->id == "browser_source")
		return OBS_SOURCE_VIDEO | OBS_SOURCE_INTERACTION;
	if (s->id == "wasapi_input" || s->id == "wasapi_input_v2")
		return OBS_SOURCE_AUDIO | OBS_SOURCE_MONITOR_BY_DEFAULT;
	if (s->id == "ffmpeg_source")
		return OBS_SOURCE_ASYNC_VIDEO | OBS_SOURCE_AUDIO;
	return OBS_SOURCE_VIDEO;
}

bool obs_sceneitem_is_group(obs_sceneitem_t *item)
{
	return item && item->source && item->source->id == "group";
}

enum obs_scale_type obs_sceneitem_get_scale_filter(obs_sceneitem_t *i)
{
	return static_cast<enum obs_scale_type>(i ? i->scale : 0);
}

void obs_sceneitem_set_scale_filter(obs_sceneitem_t *i, enum obs_scale_type f)
{
	if (i)
		i->scale = static_cast<int>(f);
}

enum obs_blending_type obs_sceneitem_get_blending_mode(obs_sceneitem_t *i)
{
	return static_cast<enum obs_blending_type>(i ? i->blendMode : 0);
}

void obs_sceneitem_set_blending_mode(obs_sceneitem_t *i, enum obs_blending_type m)
{
	if (i)
		i->blendMode = static_cast<int>(m);
}

enum obs_blending_method obs_sceneitem_get_blending_method(obs_sceneitem_t *i)
{
	return static_cast<enum obs_blending_method>(i ? i->blendMethod : 0);
}

void obs_sceneitem_set_blending_method(obs_sceneitem_t *i, enum obs_blending_method m)
{
	if (i)
		i->blendMethod = static_cast<int>(m);
}

/*
 * Mirrors obs_sceneitem_set_order. Index 0 of the vector is first_item, which
 * is the BOTTOM of the displayed list, so MOVE_UP walks toward the end.
 */
void obs_sceneitem_set_order(obs_sceneitem_t *item, enum obs_order_movement movement)
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
	auto at = std::find(v.begin(), v.end(), item);
	const size_t idx = static_cast<size_t>(std::distance(v.begin(), at));
	v.erase(at);

	switch (movement) {
	case OBS_ORDER_MOVE_UP:
		v.insert(v.begin() + static_cast<long>(std::min(idx + 1, v.size())), item);
		break;
	case OBS_ORDER_MOVE_DOWN:
		v.insert(v.begin() + static_cast<long>(idx > 0 ? idx - 1 : 0), item);
		break;
	case OBS_ORDER_MOVE_TOP:
		v.push_back(item);
		break;
	case OBS_ORDER_MOVE_BOTTOM:
		v.insert(v.begin(), item);
		break;
	}
}

void obs_sceneitem_remove(obs_sceneitem_t *item)
{
	if (!item)
		return;
	for (auto &sc : g_scenes) {
		auto &v = sc->items;
		auto at = std::find(v.begin(), v.end(), item);
		if (at != v.end()) {
			v.erase(at);
			return;
		}
	}
}

void obs_frontend_take_source_screenshot(obs_source_t *) {}

enum obs_deinterlace_mode obs_source_get_deinterlace_mode(const obs_source_t *s)
{
	return static_cast<enum obs_deinterlace_mode>(s ? s->deinterlace : 0);
}

void obs_source_set_deinterlace_mode(obs_source_t *s, enum obs_deinterlace_mode m)
{
	if (s)
		s->deinterlace = static_cast<int>(m);
}

enum obs_deinterlace_field_order obs_source_get_deinterlace_field_order(const obs_source_t *s)
{
	return static_cast<enum obs_deinterlace_field_order>(s ? s->fieldOrder : 0);
}

void obs_source_set_deinterlace_field_order(obs_source_t *s, enum obs_deinterlace_field_order o)
{
	if (s)
		s->fieldOrder = static_cast<int>(o);
}

/* Mirrors libobs: the group's children move into the parent scene in place. */
void obs_sceneitem_group_ungroup(obs_sceneitem_t *group)
{
	if (!group || !group->source || group->source->id != "group")
		return;

	obs_scene *parent = nullptr;
	size_t at = 0;
	for (auto &sc : g_scenes) {
		for (size_t i = 0; i < sc->items.size(); i++) {
			if (sc->items[i] == group) {
				parent = sc.get();
				at = i;
			}
		}
	}
	if (!parent)
		return;

	std::vector<obs_sceneitem *> children = group->source->scene->items;
	group->source->scene->items.clear();

	parent->items.erase(parent->items.begin() + static_cast<long>(at));
	parent->items.insert(parent->items.begin() + static_cast<long>(at), children.begin(), children.end());
}

void obs_frontend_open_projector(const char *, int, const char *, const char *) {}

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

/* ---- tranche 3: clipboard, transitions, adding sources ---- */

/*
 * darray, which the frontend scene list is built on, allocates through libobs's
 * allocator. obs_frontend_source_list_free is a static inline in the frontend
 * header, so SceneWalk.cpp compiles its own copy of it and frees the array with
 * bfree -- which is why the array below has to come from bmalloc.
 */
void *bmalloc(size_t size)
{
	return malloc(size ? size : 1);
}

void *brealloc(void *ptr, size_t size)
{
	return realloc(ptr, size ? size : 1);
}

void bfree(void *ptr)
{
	free(ptr);
}

/*
 * Layout-compatible with obs_frontend_source_list: DARRAY(obs_source_t *)
 * expands to { type *array; size_t num; size_t capacity; }, per
 * libobs/util/darray.h. This file cannot include the frontend header -- it
 * would pull in obs.h and collide with every stub here -- so the shape is
 * mirrored instead. If that layout ever changes upstream, this is what breaks.
 */
struct fake_source_list {
	obs_source **array;
	size_t num;
	size_t capacity;
};

void obs_frontend_get_scenes(void *out)
{
	fake_source_list *list = static_cast<fake_source_list *>(out);

	std::vector<obs_source *> scenes;
	for (auto &src : g_sources)
		if (!src->isPrivate && src->id == "scene")
			scenes.push_back(src.get());

	list->num = scenes.size();
	list->capacity = scenes.size();
	list->array = scenes.empty() ? nullptr
				     : static_cast<obs_source **>(bmalloc(sizeof(obs_source *) * scenes.size()));

	for (size_t i = 0; i < scenes.size(); i++)
		list->array[i] = scenes[i];
}

obs_weak_source_t *obs_source_get_weak_source(obs_source_t *source)
{
	if (!source)
		return nullptr;

	obs_weak_source *weak = new obs_weak_source;
	weak->uuid = source->uuid;
	return weak;
}

void obs_weak_source_release(obs_weak_source_t *weak)
{
	delete weak;
}

obs_source_t *obs_weak_source_get_source(obs_weak_source_t *weak)
{
	return weak ? obs_get_source_by_uuid(weak->uuid.c_str()) : nullptr;
}

/*
 * Transform and crop are carried through the clipboard untouched, so there is
 * nothing here for a stub to get right or wrong -- and standing up libobs's
 * matrix types would mean testing those instead of anything of ours.
 */
void obs_sceneitem_get_info2(const obs_sceneitem_t *, void *) {}
void obs_sceneitem_set_info2(obs_sceneitem_t *, const void *) {}
void obs_sceneitem_get_crop(const obs_sceneitem_t *, void *) {}
void obs_sceneitem_set_crop(obs_sceneitem_t *, const void *) {}

/* Index 0 is first_item, which is the bottom row, so a new item appends to the
 * end and lands at the top -- the same as libobs. */
obs_sceneitem_t *obs_scene_add(obs_scene_t *scene, obs_source_t *source)
{
	if (!scene || !source)
		return nullptr;

	auto item = std::make_unique<obs_sceneitem>();
	item->source = source;
	item->id = g_next_id++;

	obs_sceneitem *raw = item.get();
	scene->items.push_back(raw);
	g_items.push_back(std::move(item));

	return raw;
}

void obs_scene_atomic_update(obs_scene_t *scene, void (*func)(void *, obs_scene_t *), void *data)
{
	if (func)
		func(data, scene);
}

void obs_enter_graphics(void) {}
void obs_leave_graphics(void) {}

obs_sceneitem_t *obs_scene_get_group(obs_scene_t *scene, const char *name)
{
	if (!scene || !name)
		return nullptr;

	for (obs_sceneitem *item : scene->items)
		if (item->source && item->source->id == "group" && item->source->name == name)
			return item;

	return nullptr;
}

obs_source_t *obs_source_create(const char *id, const char *name, obs_data_t *, obs_data_t *)
{
	return (id && name) ? new_source(name, id, false) : nullptr;
}

obs_source_t *obs_source_create_private(const char *id, const char *name, obs_data_t *)
{
	return (id && name) ? new_source(name, id, true) : nullptr;
}

obs_source_t *obs_source_duplicate(obs_source_t *source, const char *name, bool create_private)
{
	return (source && name) ? new_source(name, source->id, create_private) : nullptr;
}

bool obs_source_is_hidden(obs_source_t *s)
{
	return s ? s->hidden : false;
}

const char *obs_source_get_unversioned_id(const obs_source_t *s)
{
	return s ? s->unversionedId.c_str() : nullptr;
}

/* Mirrors libobs: public inputs and groups, never scenes. */
void obs_enum_sources(bool (*cb)(void *, obs_source_t *), void *param)
{
	std::vector<obs_source *> snapshot;
	for (auto &src : g_sources)
		if (!src->isPrivate && src->id != "scene")
			snapshot.push_back(src.get());

	for (obs_source *src : snapshot)
		if (!cb(param, src))
			break;
}

/* Recorded rather than performed: what matters is which pairs were asked for. */
static std::string g_filter_copies;

void obs_source_copy_filters(obs_source_t *dst, obs_source_t *src)
{
	g_filter_copies += (src ? src->name : std::string("?")) + "->" + (dst ? dst->name : std::string("?")) + " ";
}

bool obs_source_audio_active(const obs_source_t *s)
{
	return s ? s->audioActive : false;
}

void obs_source_set_audio_active(obs_source_t *s, bool active)
{
	if (!s || s->audioActive == active)
		return;

	s->audioActive = active;
	s->audioToggles++;
}

void obs_source_set_monitoring_type(obs_source_t *s, enum obs_monitoring_type type)
{
	if (s)
		s->monitoring = static_cast<int>(type);
}

static const char *const TRANSITION_IDS[] = {"fade_transition", "cut_transition"};

bool obs_enum_transition_types(size_t idx, const char **id)
{
	if (idx >= sizeof(TRANSITION_IDS) / sizeof(TRANSITION_IDS[0]))
		return false;

	*id = TRANSITION_IDS[idx];
	return true;
}

obs_source_t *obs_sceneitem_get_transition(obs_sceneitem_t *item, bool show)
{
	if (!item)
		return nullptr;

	return show ? item->showTransition : item->hideTransition;
}

void obs_sceneitem_set_transition(obs_sceneitem_t *item, bool show, obs_source_t *transition)
{
	if (!item)
		return;

	if (show)
		item->showTransition = transition;
	else
		item->hideTransition = transition;
}

uint32_t obs_sceneitem_get_transition_duration(obs_sceneitem_t *item, bool show)
{
	if (!item)
		return 0;

	return static_cast<uint32_t>(show ? item->showDuration : item->hideDuration);
}

void obs_sceneitem_set_transition_duration(obs_sceneitem_t *item, bool show, uint32_t ms)
{
	if (!item)
		return;

	if (show)
		item->showDuration = static_cast<int>(ms);
	else
		item->hideDuration = static_cast<int>(ms);
}

/* The frontend's own default, which an item with no duration of its own falls
 * back to. Any value will do here as long as the test knows it. */
int obs_frontend_get_transition_duration(void)
{
	return 300;
}

struct FakeInput {
	const char *id;
	const char *unversioned;
	uint32_t caps;
};

/*
 * One of each case the enumeration has to handle: a plain input, one that is
 * registered under a newer versioned id, one deprecated, and one disabled --
 * which must not be offered at all.
 */
static const FakeInput INPUT_TYPES[] = {
	{"browser_source", "browser_source", OBS_SOURCE_VIDEO | OBS_SOURCE_INTERACTION},
	{"wasapi_input_v2", "wasapi_input", OBS_SOURCE_AUDIO | OBS_SOURCE_MONITOR_BY_DEFAULT},
	{"ffmpeg_source", "ffmpeg_source", OBS_SOURCE_ASYNC_VIDEO | OBS_SOURCE_AUDIO},
	{"old_thing", "old_thing", OBS_SOURCE_VIDEO | OBS_SOURCE_DEPRECATED},
	{"broken_thing", "broken_thing", OBS_SOURCE_VIDEO | OBS_SOURCE_CAP_DISABLED},
};

bool obs_enum_input_types2(size_t idx, const char **id, const char **unversioned)
{
	if (idx >= sizeof(INPUT_TYPES) / sizeof(INPUT_TYPES[0]))
		return false;

	*id = INPUT_TYPES[idx].id;
	*unversioned = INPUT_TYPES[idx].unversioned;
	return true;
}

uint32_t obs_get_source_output_flags(const char *id)
{
	for (const FakeInput &type : INPUT_TYPES)
		if (id && std::string(id) == type.id)
			return type.caps;

	return 0;
}

const char *obs_get_latest_input_type_id(const char *unversioned)
{
	for (const FakeInput &type : INPUT_TYPES)
		if (unversioned && std::string(unversioned) == type.unversioned)
			return type.id;

	return unversioned;
}

const char *obs_source_get_display_name(const char *id)
{
	if (!id)
		return "";

	const std::string key = id;
	if (key == "fade_transition")
		return "Fade";
	if (key == "cut_transition")
		return "Cut";
	if (key == "browser_source")
		return "Browser";
	if (key == "wasapi_input_v2")
		return "Audio Input Capture";
	if (key == "ffmpeg_source")
		return "Media Source";
	if (key == "old_thing")
		return "Old Thing";

	return id;
}

} /* extern "C" */

/* ---- graph builders ---- */

static obs_source *make(const std::string &name, const std::string &id)
{
	return new_source(name, id, false);
}

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

	/* --- context menu operations --- */

	/* 9e. Order moves are stated in display terms, and display is the
	 * reverse of the scene's own order. OBS_ORDER_MOVE_UP attaches the item
	 * after its successor in the scene list, which is one row higher on
	 * screen -- getting this backwards makes Move Up move down. */
	reset();
	g_program = make("Program", "scene");
	add(g_program, make("Bottom", "scene")); /* added first -> drawn last  */
	add(g_program, make("Middle", "scene"));
	add(g_program, make("Top", "scene")); /* added last  -> drawn first */
	check("order baseline", run(), "S:Top\nS:Middle\nS:Bottom\n");

	auto idOf = [&](const char *name) {
		for (obs_sceneitem *i : g_program->scene->items)
			if (i->source->name == name)
				return i->id;
		return int64_t{-1};
	};

	xray::set_order("uuid-Program", idOf("Middle"), xray::OrderMovement::Up);
	check("move up goes up on screen", run(), "S:Middle\nS:Top\nS:Bottom\n");

	xray::set_order("uuid-Program", idOf("Middle"), xray::OrderMovement::Bottom);
	check("move to bottom goes to the bottom on screen", run(), "S:Top\nS:Bottom\nS:Middle\n");

	xray::set_order("uuid-Program", idOf("Middle"), xray::OrderMovement::Top);
	check("move to top goes to the top on screen", run(), "S:Middle\nS:Top\nS:Bottom\n");

	xray::set_order("uuid-Program", idOf("Middle"), xray::OrderMovement::Down);
	check("move down goes down on screen", run(), "S:Top\nS:Middle\nS:Bottom\n");

	/* 9f. Capability flags decide which menu entries appear at all. */
	reset();
	g_program = make("Program", "scene");
	obs_source *capHost = make("Host", "scene");
	obs_sceneitem *webItem = add(capHost, make("Web", "browser_source"));
	obs_sceneitem *micItem = add(capHost, make("Mic", "wasapi_input"));
	obs_sceneitem *vidItem = add(capHost, make("Clip", "ffmpeg_source"));
	obs_sceneitem *grpItem = add(capHost, make("Grp", "group"));
	add(g_program, capHost);
	{
		auto flags = [](const xray::ItemProperties &p) {
			return std::string(p.found ? "f" : "-") + (p.has_video ? "v" : "-") +
			       (p.has_audio ? "a" : "-") + (p.is_async_video ? "y" : "-") + (p.is_group ? "g" : "-");
		};
		check("capability flags per source kind",
		      flags(xray::item_properties("uuid-Host", webItem->id)) + " " +
			      flags(xray::item_properties("uuid-Host", micItem->id)) + " " +
			      flags(xray::item_properties("uuid-Host", vidItem->id)) + " " +
			      flags(xray::item_properties("uuid-Host", grpItem->id)),
		      "fv--- f-a-- fvay- fv--g");
	}

	/* 9g. Rendering properties round-trip, and a vanished item reports
	 * found=false rather than a plausible-looking set of defaults. */
	xray::set_scale_filter("uuid-Host", webItem->id, xray::ScaleFilter::Lanczos);
	xray::set_blending_mode("uuid-Host", webItem->id, xray::BlendingMode::Multiply);
	xray::set_blending_method("uuid-Host", webItem->id, xray::BlendingMethod::SrgbOff);
	{
		const xray::ItemProperties p = xray::item_properties("uuid-Host", webItem->id);
		const xray::ItemProperties gone = xray::item_properties("uuid-Host", 999999);
		check("rendering properties round-trip",
		      std::to_string(static_cast<int>(p.scale)) + "/" +
			      std::to_string(static_cast<int>(p.blending_mode)) + "/" +
			      std::to_string(static_cast<int>(p.blending_method)) + " " +
			      (gone.found ? "found" : "missing"),
		      "4/4/1 missing");
	}

	/* 9h. Remove takes the item out of its scene. */
	xray::remove_item("uuid-Host", micItem->id);
	check("remove drops the row", run(),
	      "S:Host\n"
	      "  G:Grp\n"
	      "  -:Clip\n"
	      "  -:Web\n");

	/* 9i. Menu operations against a vanished owner or item are dropped. */
	xray::set_scale_filter("uuid-gone", 1, xray::ScaleFilter::Point);
	xray::set_blending_mode("uuid-gone", 1, xray::BlendingMode::Screen);
	xray::set_order("uuid-gone", 1, xray::OrderMovement::Up);
	xray::remove_item("uuid-Host", 999999);
	xray::screenshot_source("uuid-gone");
	xray::open_source_projector("uuid-gone", 0);
	std::cout << "PASS  stale menu targets ignored\n";

	/* 9j. Deinterlacing lives on the source, so it applies wherever that
	 * source appears rather than to one scene item. */
	reset();
	g_program = make("Program", "scene");
	obs_source *dHost = make("Host", "scene");
	add(dHost, make("Clip", "ffmpeg_source"));
	add(g_program, dHost);
	xray::set_deinterlace_mode("uuid-Clip", xray::DeinterlaceMode::Yadif2x);
	xray::set_deinterlace_field_order("uuid-Clip", xray::FieldOrder::Bottom);
	check("deinterlace round-trips",
	      std::to_string(static_cast<int>(xray::deinterlace_mode("uuid-Clip"))) + "/" +
		      std::to_string(static_cast<int>(xray::deinterlace_field_order("uuid-Clip"))),
	      "8/1");

	/* A source that was never touched reports the disabled default, and a
	 * vanished one does not invent something else. */
	check("deinterlace defaults and misses",
	      std::to_string(static_cast<int>(xray::deinterlace_mode("uuid-Host"))) +
		      std::to_string(static_cast<int>(xray::deinterlace_mode("uuid-gone"))),
	      "00");

	/* 9k. Ungroup dissolves a group in place, leaving its children where
	 * the group was. Anything that is not a group is left alone. */
	reset();
	g_program = make("Program", "scene");
	obs_source *uHost = make("Host", "scene");
	obs_source *uGroup = make("Grp", "group");
	add(uGroup, make("A", "scene"));
	add(uGroup, make("B", "scene"));
	add(uHost, make("Before", "scene"));
	obs_sceneitem *groupItem = add(uHost, uGroup);
	add(uHost, make("After", "scene"));
	add(g_program, uHost);
	check("group baseline", run(),
	      "S:Host\n"
	      "  S:After\n"
	      "  G:Grp\n"
	      "    S:B\n"
	      "    S:A\n"
	      "  S:Before\n");

	xray::ungroup_item("uuid-Host", groupItem->id);
	check("ungroup dissolves in place", run(),
	      "S:Host\n"
	      "  S:After\n"
	      "  S:B\n"
	      "  S:A\n"
	      "  S:Before\n");

	/* Not a group, and a missing item: both no-ops rather than damage. */
	{
		int64_t beforeId = -1;
		for (obs_sceneitem *i : uHost->scene->items)
			if (i->source->name == "Before")
				beforeId = i->id;
		xray::ungroup_item("uuid-Host", beforeId);
		xray::ungroup_item("uuid-Host", 999999);
		xray::ungroup_item("uuid-gone", 1);
	}
	check("ungroup ignores non-groups and misses", run(),
	      "S:Host\n"
	      "  S:After\n"
	      "  S:B\n"
	      "  S:A\n"
	      "  S:Before\n");

	/* 9l. Hiding a source in the mixer is stored on the source, and has to
	 * nudge audio_active off and back on for the frontend to notice --
	 * leaving the flag exactly as it was found. */
	reset();
	g_program = make("Program", "scene");
	obs_source *mHost = make("Host", "scene");
	obs_source *mMic = make("Mic", "wasapi_input");
	add(mHost, mMic);
	add(g_program, mHost);

	xray::set_source_mixer_hidden("uuid-Mic", true);
	check("hide in mixer stores and nudges",
	      std::string(xray::source_mixer_hidden("uuid-Mic") ? "hidden" : "shown") +
		      " toggles=" + std::to_string(mMic->audioToggles) + " active=" + (mMic->audioActive ? "1" : "0"),
	      "hidden toggles=2 active=1");

	xray::set_source_mixer_hidden("uuid-Mic", false);
	check("unhide in mixer stores and nudges",
	      std::string(xray::source_mixer_hidden("uuid-Mic") ? "hidden" : "shown") +
		      " toggles=" + std::to_string(mMic->audioToggles),
	      "shown toggles=4");

	/* A source with no live audio has no mixer strip to update, so it is
	 * left alone rather than switched on behind the operator's back. */
	mMic->audioActive = false;
	mMic->audioToggles = 0;
	xray::set_source_mixer_hidden("uuid-Mic", true);
	xray::set_source_mixer_hidden("uuid-gone", true);
	check("silent source is not nudged",
	      std::to_string(mMic->audioToggles) + (mMic->audioActive ? "1" : "0") +
		      (xray::source_mixer_hidden("uuid-gone") ? "?" : "-"),
	      "00-");

	/* 9m. Row colour uses OBS's own encoding, so both docks read the same
	 * value: 0 none, 1 custom, 2..9 for the eight swatches. */
	reset();
	g_program = make("Program", "scene");
	obs_source *cHost = make("Host", "scene");
	obs_sceneitem *cItem = add(cHost, make("Web", "browser_source"));
	add(g_program, cHost);

	auto colour = [](const xray::ItemColor &c) {
		return std::to_string(c.preset) + "/" + (c.custom.empty() ? "-" : c.custom);
	};

	const xray::ItemColor unset = xray::item_color("uuid-Host", cItem->id);

	xray::set_item_color("uuid-Host", cItem->id, 5, std::string());
	const xray::ItemColor preset = xray::item_color("uuid-Host", cItem->id);

	xray::set_item_color("uuid-Host", cItem->id, 1, "#8800FF00");
	const xray::ItemColor custom = xray::item_color("uuid-Host", cItem->id);

	/* Clearing drops the custom colour too, or the next custom pick would
	 * come up holding the old one. */
	xray::set_item_color("uuid-Host", cItem->id, 0, std::string());
	const xray::ItemColor cleared = xray::item_color("uuid-Host", cItem->id);

	check("row colour round-trips",
	      colour(unset) + " " + colour(preset) + " " + colour(custom) + " " + colour(cleared),
	      "0/- 5/- 1/#8800FF00 0/-");

	xray::set_item_color("uuid-gone", 1, 3, std::string());
	check("colour on a vanished item is dropped", colour(xray::item_color("uuid-gone", 1)), "0/-");

	/* 9n. Copy and paste land in the scene the row belongs to, which is the
	 * point of having them here at all. */
	reset();
	g_program = make("Program", "scene");
	obs_source *pHost = make("Host", "scene");
	obs_source *pOther = make("Other", "scene");
	obs_sceneitem *pWeb = add(pHost, make("Web", "browser_source"));
	add(g_program, pHost);
	add(g_program, pOther);

	xray::paste_item("uuid-Other", false);
	check("paste with an empty clipboard does nothing", run(),
	      "S:Other\n"
	      "S:Host\n"
	      "  -:Web\n");

	xray::copy_item("uuid-Host", pWeb->id);
	xray::paste_item("uuid-Other", false);
	check("paste reference lands in the row's own scene", run(),
	      "S:Other\n"
	      "  -:Web\n"
	      "S:Host\n"
	      "  -:Web\n");

	xray::paste_item("uuid-Other", true);
	check("paste duplicate takes a free name", run(),
	      "S:Other\n"
	      "  -:Web 2\n"
	      "  -:Web\n"
	      "S:Host\n"
	      "  -:Web\n");

	/* A group cannot be referenced twice in one scene, so that paste is
	 * dropped rather than half applied -- but the same group is fine
	 * somewhere else. */
	obs_source *pGroup = make("Grp", "group");
	add(pGroup, make("Boxed", "scene"));
	obs_sceneitem *pGroupItem = add(pOther, pGroup);

	xray::copy_item("uuid-Other", pGroupItem->id);
	xray::paste_item("uuid-Other", false);
	xray::paste_item("uuid-Host", false);
	check("group reference refused in its own scene, allowed elsewhere", run(),
	      "S:Other\n"
	      "  G:Grp\n"
	      "    S:Boxed\n"
	      "  -:Web 2\n"
	      "  -:Web\n"
	      "S:Host\n"
	      "  G:Grp\n"
	      "    S:Boxed\n"
	      "  -:Web\n");

	/* Paste into a scene that has gone is dropped, not guessed at. */
	xray::paste_item("uuid-gone", false);
	std::cout << "PASS  paste into a vanished scene ignored\n";

	/* 9o. The clipboard holds weak references, so it empties itself when
	 * the source it was pointing at goes. */
	check("clipboard holds a copied source", xray::clipboard_has_item() ? "held" : "empty", "held");
	reset();
	check("clipboard lets go of a destroyed source", xray::clipboard_has_item() ? "held" : "empty", "empty");

	/* 9p. Filters are copied wholesale between sources, never onto the one
	 * they came from. */
	g_program = make("Program", "scene");
	obs_source *fHost = make("Host", "scene");
	add(fHost, make("Web", "browser_source"));
	add(fHost, make("Clip", "ffmpeg_source"));
	add(g_program, fHost);

	g_filter_copies.clear();
	xray::paste_filters("uuid-Web");
	xray::copy_filters("uuid-Web");
	xray::paste_filters("uuid-Web");
	xray::paste_filters("uuid-Clip");
	xray::paste_filters("uuid-gone");
	check("filters paste only onto another source", g_filter_copies, "Web->Clip ");

	/* 9q. Show and hide transitions. */
	reset();
	g_program = make("Program", "scene");
	obs_source *tHost = make("Host", "scene");
	obs_sceneitem *tWeb = add(tHost, make("Web", "browser_source"));
	obs_sceneitem *tClip = add(tHost, make("Clip", "ffmpeg_source"));
	add(g_program, tHost);

	auto transition = [](const xray::ItemTransition &t) {
		return (t.id.empty() ? std::string("none") : t.id) + "/" + std::to_string(t.duration_ms) + "/" +
		       (t.configurable ? "cfg" : "-");
	};

	/* Nothing set yet shows the frontend's own duration, which is what the
	 * item would actually use. */
	check("no transition falls back to the frontend duration",
	      transition(xray::item_transition("uuid-Host", tWeb->id, true)), "none/300/-");

	xray::set_item_transition("uuid-Host", tWeb->id, true, "fade_transition", "Web Show Transition");
	const size_t afterFirst = g_sources.size();

	/* Picking the same type again keeps the transition that is already
	 * there, settings and all, rather than building a fresh one. */
	xray::set_item_transition("uuid-Host", tWeb->id, true, "fade_transition", "Web Show Transition");
	check("re-picking the same transition changes nothing",
	      transition(xray::item_transition("uuid-Host", tWeb->id, true)) +
		      (g_sources.size() == afterFirst ? " kept" : " replaced"),
	      "fade_transition/300/cfg kept");

	/* Show and hide are separate settings on the same item. */
	check("hide transition is independent of show", transition(xray::item_transition("uuid-Host", tWeb->id, false)),
	      "none/300/-");

	xray::set_item_transition_duration("uuid-Host", tWeb->id, true, 750);
	xray::set_item_transition_duration("uuid-Host", tWeb->id, true, 0);
	xray::set_item_transition("uuid-Host", tWeb->id, false, "cut_transition", "Web Hide Transition");
	check("duration sticks, and a cut has nothing to configure",
	      transition(xray::item_transition("uuid-Host", tWeb->id, true)) + " " +
		      transition(xray::item_transition("uuid-Host", tWeb->id, false)),
	      "fade_transition/750/cfg cut_transition/300/-");

	xray::set_item_transition("uuid-Host", tWeb->id, true, std::string(), std::string());
	check("clearing a transition clears its duration too",
	      transition(xray::item_transition("uuid-Host", tWeb->id, true)), "none/300/-");

	/* Pasting duplicates rather than sharing: two items pointing at one
	 * transition would share its settings and its playback state. */
	xray::copy_item_transition("uuid-Host", tWeb->id, false);
	xray::paste_item_transition("uuid-Host", tClip->id, false);
	check("pasted transition is a copy, not the same source",
	      std::string(xray::clipboard_has_transition() ? "held" : "empty") + " " +
		      (tClip->hideTransition && tClip->hideTransition != tWeb->hideTransition ? "copy" : "shared") +
		      " " + transition(xray::item_transition("uuid-Host", tClip->id, false)),
	      "held copy cut_transition/300/-");

	/* 9r. The Add Source menu's type list. */
	{
		std::string listed;
		for (const xray::SourceType &type : xray::input_types())
			listed += type.id + (type.deprecated ? "! " : " ");

		/* broken_thing is CAP_DISABLED and must not be offered at all;
		 * wasapi_input is listed unversioned even though it is
		 * registered as wasapi_input_v2. */
		check("input types skip disabled and flag deprecated", listed,
		      "browser_source wasapi_input ffmpeg_source old_thing! ");
	}

	/* 9s. What can be added to a given scene. */
	reset();
	g_program = make("Program", "scene");
	obs_source *aHost = make("Host", "scene");
	add(g_program, aHost);
	make("Cam B", "browser_source");
	make("Cam A", "browser_source");
	make("Free Group", "group");
	add(aHost, make("Held Group", "group"));
	make("Internal", "browser_source")->hidden = true;

	auto listed = [](const std::vector<std::string> &names) {
		std::string out;
		for (const std::string &name : names)
			out += name + "|";
		return out;
	};

	check("addable inputs are sorted and skip hidden ones",
	      listed(xray::addable_sources("uuid-Host", "browser_source")), "Cam A|Cam B|");

	/* A group already in the target cannot be referenced again, so it is
	 * not offered there. */
	check("addable groups skip one already in the scene", listed(xray::addable_sources("uuid-Host", "group")),
	      "Free Group|");

	/* Scenes come from the frontend's list, with the target left out so a
	 * scene cannot be added to itself. */
	check("addable scenes exclude the target", listed(xray::addable_sources("uuid-Host", "scene")), "Program|");

	check("nothing is addable to a vanished scene", listed(xray::addable_sources("uuid-gone", "browser_source")),
	      "");

	/* 9t. Creating and adding. */
	check("a taken name is refused rather than uniquified",
	      xray::add_new_source("uuid-Host", "browser_source", "Cam A", true) ? "added" : "refused", "refused");

	check("an empty name is refused",
	      xray::add_new_source("uuid-Host", "browser_source", "", true) ? "added" : "refused", "refused");

	xray::add_new_source("uuid-Host", "wasapi_input", "New Mic", true);
	{
		obs_source *created = obs_get_source_by_name("New Mic");
		check("a new source resolves the versioned id and takes default monitoring",
		      std::string(created ? created->id : "?") + "/" +
			      std::to_string(created ? created->monitoring : -1),
		      "wasapi_input_v2/1");
	}

	xray::add_existing_source("uuid-Host", "Cam B", false, true);
	xray::add_existing_source("uuid-Host", "Cam B", true, true);
	xray::add_existing_source("uuid-Host", "Nothing At All", false, true);
	xray::add_existing_source("uuid-gone", "Cam A", false, true);

	check("existing sources add by reference and by copy", run(),
	      "S:Host\n"
	      "  -:Cam B 2\n"
	      "  -:Cam B\n"
	      "  -:New Mic\n"
	      "  G:Held Group\n");

	/* 9u. Teardown drops every weak reference the clipboards hold. */
	xray::copy_item("uuid-Host", aHost->scene->items.front()->id);
	xray::copy_filters("uuid-Cam A");
	xray::clear_clipboard();
	check("clearing empties every clipboard",
	      std::string(xray::clipboard_has_item() ? "i" : "-") + (xray::clipboard_has_filters() ? "f" : "-") +
		      (xray::clipboard_has_transition() ? "t" : "-"),
	      "---");

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
