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

#include <QFrame>

class QCheckBox;
class QLabel;

/*
 * One row of the tree: icon, name, visibility and lock.
 *
 * Built to mirror OBS's own SourceTreeItem -- same child order, same zero
 * margins and spacing, and above all the same "class" properties. OBS's themes
 * style the eye and lock entirely through .checkbox-icon, .indicator-visibility
 * and .indicator-lock, all of which are Qt class selectors matching the widget's
 * class property rather than its C++ type. Setting the same strings gets the
 * real themed controls, in whatever theme the user is running, with no icon
 * assets shipped and nothing to keep in sync.
 */
class XRayRow : public QFrame {
	Q_OBJECT

public:
	XRayRow(const xray::Node &node, int depth, QWidget *parent = nullptr);

private slots:
	void onVisibilityToggled(bool checked);
	void onLockToggled(bool checked);

private:
	std::string ownerUuid;
	int64_t itemId = -1;

	QLabel *iconLabel = nullptr;
	QLabel *label = nullptr;
	QCheckBox *vis = nullptr;
	QCheckBox *lock = nullptr;
};
