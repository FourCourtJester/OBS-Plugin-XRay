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

#include <QWidget>

#include <vector>

class QLabel;
class QScrollArea;
class QVBoxLayout;

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

public slots:
	/*
	 * Rebuilds the tree from the current program scene.
	 *
	 * Both slots touch widgets, so they must run on the UI thread. Frontend
	 * events already arrive there (OnEvent dispatches synchronously from
	 * OBSBasic), but the per-scene libobs signals added in the next phase do
	 * not -- those have to reach these through a queued connection.
	 */
	void refresh();

	/* Drops every row and shows the empty state. */
	void clear();

private:
	void addRows(const std::vector<xray::Node> &nodes, int depth);

	QScrollArea *scrollArea = nullptr;
	QWidget *content = nullptr;
	QVBoxLayout *contentLayout = nullptr;
	QLabel *placeholder = nullptr;
};
