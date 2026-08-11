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

#include "XRayDock.hpp"

#include <obs-module.h>
#include <obs-frontend-api.h>
#include <plugin-support.h>

#include <QPointer>

OBS_DECLARE_MODULE()
OBS_MODULE_USE_DEFAULT_LOCALE(PLUGIN_NAME, "en-US")

/*
 * obs_frontend_add_dock_by_id() landed in OBS 30.0.0; 29.x only has the
 * deprecated obs_frontend_add_dock().
 *
 * This check is load-bearing, not belt-and-braces. libobs resolves the
 * obs_module_ver export at open time but never compares it -- as of 31.1.1
 * MODULE_INCOMPATIBLE_VER is defined and never returned. Nothing rejects a
 * module built against newer headers, so we have to refuse for ourselves.
 */
static constexpr uint32_t MINIMUM_OBS_VERSION = MAKE_SEMANTIC_VERSION(30, 0, 0);

static const char *DOCK_ID = "obs-xray-subscene-sources";

static bool dock_registered = false;
static bool callback_registered = false;

/* Guarded rather than raw: OBS owns the widget and outlives our pointer to it. */
static QPointer<XRayDock> dock_widget;

static void create_dock(void)
{
	if (dock_registered)
		return;

	/*
	 * OBS takes ownership: add_dock_by_id() wraps this in an OBSDock
	 * parented to the main window. We never delete it ourselves.
	 */
	XRayDock *dock = new XRayDock();

	if (!obs_frontend_add_dock_by_id(DOCK_ID, obs_module_text("Dock.Title"), dock)) {
		obs_log(LOG_ERROR, "failed to register dock '%s' (duplicate id?)", DOCK_ID);
		delete dock;
		return;
	}

	dock_widget = dock;
	dock_registered = true;
	obs_log(LOG_INFO, "dock registered as '%s'", DOCK_ID);

	/* FINISHED_LOADING lands after the first scene is already active. */
	dock->refresh();
}

static void destroy_dock(void)
{
	if (!dock_registered)
		return;

	obs_frontend_remove_dock(DOCK_ID);
	dock_widget = nullptr;
	dock_registered = false;
}

static void frontend_event(enum obs_frontend_event event, void *)
{
	switch (event) {
	case OBS_FRONTEND_EVENT_FINISHED_LOADING:
		/* Build here, not in obs_module_load() -- the frontend does not
		 * exist yet at module load time. */
		create_dock();
		break;

	case OBS_FRONTEND_EVENT_SCENE_CHANGED:
		/*
		 * The dock is anchored to the program scene, so it rewrites
		 * itself on every cut. Accepted for v1.0; a "pin to scene"
		 * toggle is the cheap fix if it proves disruptive.
		 */
		if (dock_widget)
			dock_widget->refresh();
		break;

	case OBS_FRONTEND_EVENT_SCENE_COLLECTION_CHANGING:
		/*
		 * Drop every row before OBS destroys the sources underneath us.
		 * This does arrive: OBSBasic emits it before ClearSceneData()
		 * raises disableSaving, which is what would otherwise suppress
		 * frontend events.
		 */
		if (dock_widget)
			dock_widget->clear();
		break;

	case OBS_FRONTEND_EVENT_SCENE_COLLECTION_CHANGED:
		if (dock_widget)
			dock_widget->refresh();
		break;

	case OBS_FRONTEND_EVENT_EXIT:
		/*
		 * This is the last safe moment to touch the frontend. OBSBasic
		 * calls obs_frontend_set_callbacks_internal(nullptr) on the very
		 * next line after emitting EXIT, so by the time obs_module_unload()
		 * runs inside obs_shutdown() every obs_frontend_* call is a no-op
		 * that logs an error. Dock layout state is saved before EXIT, so
		 * removing here still persists the panel's position.
		 */
		destroy_dock();

		obs_frontend_remove_event_callback(frontend_event, nullptr);
		callback_registered = false;
		break;

	default:
		break;
	}
}

bool obs_module_load(void)
{
	const uint32_t version = obs_get_version();

	if (version < MINIMUM_OBS_VERSION) {
		obs_log(LOG_WARNING, "requires OBS %u.%u.%u or later, running on %s -- not loading",
			MINIMUM_OBS_VERSION >> 24, (MINIMUM_OBS_VERSION >> 16) & 0xFF, MINIMUM_OBS_VERSION & 0xFFFF,
			obs_get_version_string());
		return false;
	}

	obs_frontend_add_event_callback(frontend_event, nullptr);
	callback_registered = true;

	obs_log(LOG_INFO, "plugin loaded successfully (version %s)", PLUGIN_VERSION);
	return true;
}

void obs_module_unload(void)
{
	/*
	 * Normally EXIT has already cleaned up and both flags are false. These
	 * only fire when the module is unloaded without the frontend having run
	 * its shutdown path at all.
	 */
	if (callback_registered) {
		obs_frontend_remove_event_callback(frontend_event, nullptr);
		callback_registered = false;
	}

	destroy_dock();

	obs_log(LOG_INFO, "plugin unloaded");
}
