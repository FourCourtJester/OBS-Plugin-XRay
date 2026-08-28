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
 * The Transform submenu, ported from OBSBasic.
 *
 * Split out from SceneWalk.cpp because these are the only operations needing
 * libobs's graphics maths. The test harness links SceneWalk.cpp and supplies
 * its own libobs; standing up matrix4 and vec3 there would mean the tests were
 * checking the stubbed maths rather than anything of ours, so this stays out of
 * the harness deliberately.
 *
 * The arithmetic is a faithful port of OBSBasic_Scenes.cpp and
 * OBSBasic_SceneItems.cpp -- reset_tr, RotateSelectedSources,
 * MultiplySelectedItemScale, CenterAlignSelectedItems and
 * CenterSelectedSceneItems -- reduced to a single item, since this dock has no
 * multi-selection.
 */

#include "SceneWalk.hpp"

#include <obs.h>
#include <obs.hpp>

#include <graphics/matrix4.h>
#include <graphics/vec2.h>
#include <graphics/vec3.h>

#include <algorithm>
#include <cmath>

namespace xray {

namespace {

constexpr float INFINITE_COORD = 1000000.0f;

obs_sceneitem_t *resolve(const std::string &owner_uuid, int64_t item_id, OBSSourceAutoRelease &owner)
{
	owner = obs_get_source_by_uuid(owner_uuid.c_str());
	if (!owner)
		return nullptr;

	obs_scene_t *scene = obs_group_or_scene_from_source(owner);
	return scene ? obs_scene_find_sceneitem_by_id(scene, item_id) : nullptr;
}

/* The item's axis-aligned bounding box in scene space. */
void item_box(obs_sceneitem_t *item, vec3 &tl, vec3 &br)
{
	matrix4 transform;
	obs_sceneitem_get_box_transform(item, &transform);

	vec3_set(&tl, INFINITE_COORD, INFINITE_COORD, 0.0f);
	vec3_set(&br, -INFINITE_COORD, -INFINITE_COORD, 0.0f);

	const float corners[4][2] = {{0.0f, 0.0f}, {1.0f, 0.0f}, {0.0f, 1.0f}, {1.0f, 1.0f}};

	for (const auto &corner : corners) {
		vec3 pos;
		vec3_set(&pos, corner[0], corner[1], 0.0f);
		vec3_transform(&pos, &pos, &transform);
		vec3_min(&tl, &tl, &pos);
		vec3_max(&br, &br, &pos);
	}
}

vec3 item_tl(obs_sceneitem_t *item)
{
	vec3 tl, br;
	item_box(item, tl, br);
	return tl;
}

/*
 * Rotating and flipping move the item's top-left corner, so OBS records where
 * it was and shifts the item back afterwards. Without this the item wanders
 * across the canvas as you rotate it.
 */
void restore_tl(obs_sceneitem_t *item, const vec3 &tl)
{
	vec2 pos;
	obs_sceneitem_get_pos(item, &pos);

	const vec3 moved = item_tl(item);
	pos.x += tl.x - moved.x;
	pos.y += tl.y - moved.y;

	obs_sceneitem_set_pos(item, &pos);
}

void reset_transform(obs_sceneitem_t *item)
{
	obs_sceneitem_defer_update_begin(item);

	obs_transform_info info;
	vec2_set(&info.pos, 0.0f, 0.0f);
	vec2_set(&info.scale, 1.0f, 1.0f);
	info.rot = 0.0f;
	info.alignment = OBS_ALIGN_TOP | OBS_ALIGN_LEFT;
	info.bounds_type = OBS_BOUNDS_NONE;
	info.bounds_alignment = OBS_ALIGN_CENTER;
	info.crop_to_bounds = false;
	vec2_set(&info.bounds, 0.0f, 0.0f);
	obs_sceneitem_set_info2(item, &info);

	obs_sceneitem_crop crop = {};
	obs_sceneitem_set_crop(item, &crop);

	obs_sceneitem_defer_update_end(item);
}

void rotate(obs_sceneitem_t *item, float degrees)
{
	const vec3 tl = item_tl(item);

	float rot = degrees + obs_sceneitem_get_rot(item);
	if (rot >= 360.0f)
		rot -= 360.0f;
	else if (rot <= -360.0f)
		rot += 360.0f;

	obs_sceneitem_set_rot(item, rot);
	obs_sceneitem_force_update_transform(item);
	restore_tl(item, tl);
}

void flip(obs_sceneitem_t *item, float x, float y)
{
	const vec3 tl = item_tl(item);

	vec2 multiplier;
	vec2_set(&multiplier, x, y);

	vec2 scale;
	obs_sceneitem_get_scale(item, &scale);
	vec2_mul(&scale, &scale, &multiplier);
	obs_sceneitem_set_scale(item, &scale);

	obs_sceneitem_force_update_transform(item);
	restore_tl(item, tl);
}

/* Fit and Stretch differ only in the bounds type they ask for. */
void bound_to_screen(obs_sceneitem_t *item, obs_bounds_type bounds_type)
{
	obs_video_info ovi;
	if (!obs_get_video_info(&ovi))
		return;

	obs_transform_info info;
	vec2_set(&info.pos, 0.0f, 0.0f);
	vec2_set(&info.scale, 1.0f, 1.0f);
	info.rot = 0.0f;
	info.alignment = OBS_ALIGN_LEFT | OBS_ALIGN_TOP;
	vec2_set(&info.bounds, float(ovi.base_width), float(ovi.base_height));
	info.bounds_type = bounds_type;
	info.bounds_alignment = OBS_ALIGN_CENTER;
	info.crop_to_bounds = obs_sceneitem_get_bounds_crop(item);

	obs_sceneitem_set_info2(item, &info);
}

enum class CenterAxis { Both, Vertical, Horizontal };

void center(obs_sceneitem_t *item, CenterAxis axis)
{
	obs_source_t *source = obs_sceneitem_get_source(item);
	if (!source)
		return;

	/* An item with no size has no centre to compute. */
	obs_transform_info info;
	obs_sceneitem_get_info2(item, &info);
	if (float(obs_source_get_width(source)) * info.scale.x == 0.0f ||
	    float(obs_source_get_height(source)) * info.scale.y == 0.0f)
		return;

	obs_video_info ovi;
	if (!obs_get_video_info(&ovi))
		return;

	vec3 tl, br;
	item_box(item, tl, br);

	vec3 item_center;
	vec3_set(&item_center, (br.x + tl.x) / 2.0f, (br.y + tl.y) / 2.0f, 0.0f);

	vec3 screen_center;
	vec3_set(&screen_center, float(ovi.base_width), float(ovi.base_height), 0.0f);
	vec3_mulf(&screen_center, &screen_center, 0.5f);

	vec3 offset;
	vec3_sub(&offset, &screen_center, &item_center);

	vec3 target;
	vec3_add(&target, &tl, &offset);

	/* Centring on one axis leaves the other where it was. */
	const vec3 current = item_tl(item);
	if (axis == CenterAxis::Vertical)
		target.x = current.x;
	else if (axis == CenterAxis::Horizontal)
		target.y = current.y;

	restore_tl(item, target);
}

} // namespace

void apply_transform(const std::string &owner_uuid, int64_t item_id, TransformOp op)
{
	OBSSourceAutoRelease owner;
	obs_sceneitem_t *item = resolve(owner_uuid, item_id, owner);
	if (!item)
		return;

	/* OBS refuses to transform a locked item, and so does this. */
	if (obs_sceneitem_locked(item))
		return;

	switch (op) {
	case TransformOp::Reset:
		reset_transform(item);
		break;
	case TransformOp::Rotate90CW:
		rotate(item, 90.0f);
		break;
	case TransformOp::Rotate90CCW:
		rotate(item, -90.0f);
		break;
	case TransformOp::Rotate180:
		rotate(item, 180.0f);
		break;
	case TransformOp::FlipHorizontal:
		flip(item, -1.0f, 1.0f);
		break;
	case TransformOp::FlipVertical:
		flip(item, 1.0f, -1.0f);
		break;
	case TransformOp::FitToScreen:
		bound_to_screen(item, OBS_BOUNDS_SCALE_INNER);
		break;
	case TransformOp::StretchToScreen:
		bound_to_screen(item, OBS_BOUNDS_STRETCH);
		break;
	case TransformOp::CenterToScreen:
		center(item, CenterAxis::Both);
		break;
	case TransformOp::CenterVertically:
		center(item, CenterAxis::Vertical);
		break;
	case TransformOp::CenterHorizontally:
		center(item, CenterAxis::Horizontal);
		break;
	}
}

} // namespace xray
