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

#include "SceneWalk.hpp"

#include <obs.hpp>

#include <QWidget>

#include <vector>

class QLabel;
class QScrollArea;
class QTimer;
class XRayList;

/*
 * The SubScene Sources panel.
 *
 * Ownership note: this widget is handed to obs_frontend_add_dock_by_id(), which
 * wraps it in an OBSDock parented to the main window and takes ownership of it.
 * Never delete an instance directly -- call obs_frontend_remove_dock() instead.
 */
class XRayDock : public QWidget {
	Q_OBJECT

public:
	explicit XRayDock(QWidget *parent = nullptr);

	/*
	 * Drops the watches explicitly. Left to the implicit destructor they
	 * would be torn down after ~QWidget, leaving a window in which a libobs
	 * signal on another thread could reach a half-destroyed object.
	 */
	~XRayDock() override;

public slots:
	/*
	 * Rebuilds the tree from the current program scene and rewires the
	 * signal watches to match it.
	 *
	 * Every slot here touches widgets, so all of them must run on the UI
	 * thread. Frontend events already arrive there -- OnEvent dispatches
	 * synchronously from OBSBasic -- but libobs scene signals do not, so
	 * those reach scheduleRefresh() through a queued connection.
	 */
	void refresh();

	/* Drops every row and every watch, and shows the empty state. */
	void clear();

	/*
	 * Coalescing entry point for libobs signals. A single edit can produce a
	 * burst -- removing a group emits item_remove per child plus a reorder --
	 * and a scene collection load emits thousands, so the rebuild is deferred
	 * behind a short timer rather than run per signal.
	 */
	void scheduleRefresh();

private slots:
	void applyReorder(const std::string &ownerUuid, int64_t itemId, int64_t beforeItemId);

private:
	static void onContainerChanged(void *data, calldata_t *cd);
	static void onSourceChanged(void *data, calldata_t *cd);

	void addRows(const std::vector<xray::Node> &nodes, int depth);
	void rewatch(const std::vector<xray::Watch> &watches);

	QScrollArea *scrollArea = nullptr;
	XRayList *list = nullptr;
	QLabel *placeholder = nullptr;
	QTimer *refreshTimer = nullptr;

	/*
	 * OBSSignal connects with signal_handler_connect_ref(), which takes a
	 * reference on the handler itself rather than the source. The handler
	 * therefore outlives its source, disconnection stays safe after the
	 * source is gone, and nothing here keeps a source alive -- so the
	 * destroy signal still fires.
	 */
	std::vector<OBSSignal> watches;
};
