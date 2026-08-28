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
class QToolButton;
class XRayList;

/*
 * The Sources X-Ray panel.
 *
 * Ownership note: this widget is handed to obs_frontend_add_dock_by_id(), which
 * wraps it in an OBSDock parented to the main window and takes ownership of it.
 * Never delete an instance directly -- call obs_frontend_remove_dock() instead.
 */
class XRayDock : public QWidget {
	Q_OBJECT

public:
	/*
	 * The target is fixed for the life of the dock. Two instances are
	 * registered -- one following program, one following preview -- rather
	 * than one panel switching between them: OBS then owns their visibility,
	 * position and saved layout individually, and an operator who only wants
	 * one simply closes the other.
	 */
	explicit XRayDock(xray::SceneTarget target, QWidget *parent = nullptr);

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
	void collapseAll();
	void expandAll();

	/*
	 * Switches between mirroring the Sources dock and showing only the
	 * branches that lead to a nested scene. Persisted, because it is a
	 * standing preference about what this panel is for rather than a
	 * per-session view toggle.
	 */
	void setShowAll(bool showAll);

private:
	static void onContainerChanged(void *data, calldata_t *cd);
	static void onSourceChanged(void *data, calldata_t *cd);

	void addRows(const std::vector<xray::Node> &nodes, int depth);

	/*
	 * A preview dock out of studio mode has nothing to say and no reason to
	 * take up room, so it shows one line and hides everything else. Returns
	 * true when it did, meaning the caller should not build rows.
	 */
	bool showStudioModeNotice();
	void rewatch(const std::vector<xray::Watch> &watches);

	/*
	 * Brings the selected row into view, but only when the selection has
	 * actually moved. Scrolling on every rebuild would drag the panel around
	 * under anyone reading it, since a rebuild happens on any scene change.
	 */
	void revealSelection();

	QScrollArea *scrollArea = nullptr;
	XRayList *list = nullptr;
	QLabel *placeholder = nullptr;
	QTimer *refreshTimer = nullptr;
	QToolButton *showAllButton = nullptr;
	QWidget *toolbar = nullptr;

	const xray::SceneTarget target;
	bool showAll = false;

	/* Identifies the row last scrolled to, so a repeat is a no-op. */
	std::string revealedOwner;
	int64_t revealedItem = -1;

	/*
	 * OBSSignal connects with signal_handler_connect_ref(), which takes a
	 * reference on the handler itself rather than the source. The handler
	 * therefore outlives its source, disconnection stays safe after the
	 * source is gone, and nothing here keeps a source alive -- so the
	 * destroy signal still fires.
	 */
	std::vector<OBSSignal> watches;
};
