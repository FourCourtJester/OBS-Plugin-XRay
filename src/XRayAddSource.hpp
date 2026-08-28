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

#include <QDialog>

class QCheckBox;
class QLineEdit;
class QListWidget;
class QPushButton;
class QRadioButton;

/*
 * Create-or-pick, for the Add Source menu.
 *
 * Stands in for OBSBasicSourceSelect, which is an OBSBasic dialog with no
 * frontend API behind it. The shape is deliberately the same -- create a new
 * source under a name, or add one that already exists, with a "make source
 * visible" toggle -- so the flow reads the same as it does from the stock
 * Sources dock.
 *
 * The one thing it does differently is where the source lands: the target is
 * the scene or group the row belongs to, not the program scene. Adding
 * straight into a nested scene is the reason this exists here at all.
 */
class XRayAddSource : public QDialog {
	Q_OBJECT

public:
	XRayAddSource(const std::string &ownerUuid, const xray::SourceType &type, QWidget *parent = nullptr);

private:
	void updateEnabled();
	void addSource();

	std::string owner;
	xray::SourceType type;

	QRadioButton *createNew = nullptr;
	QRadioButton *addExisting = nullptr;
	QLineEdit *name = nullptr;
	QListWidget *existing = nullptr;
	QCheckBox *visible = nullptr;
	QPushButton *okButton = nullptr;
};
