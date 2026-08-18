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
#include <QPoint>

class QCheckBox;
class QContextMenuEvent;
class QHBoxLayout;
class QLabel;
class QLineEdit;

/*
 * One row of the tree: expand, icon, name, visibility and lock.
 *
 * Built to mirror OBS's own SourceTreeItem -- same child order, same zero
 * margins and spacing, and above all the same "class" properties. OBS's themes
 * style the eye, lock and expand arrow entirely through .checkbox-icon,
 * .indicator-visibility, .indicator-lock and .indicator-expand, all of which
 * are Qt class selectors matching the widget's class property rather than its
 * C++ type. Setting the same strings gets the real themed controls, in whatever
 * theme the user is running, with no icon assets shipped.
 */
class XRayRow : public QFrame {
	Q_OBJECT

public:
	XRayRow(const xray::Node &node, int depth, QWidget *parent = nullptr);

	const std::string &ownerUuid() const { return owner; }
	int64_t itemId() const { return item; }
	bool hasChildren() const { return branch; }
	bool isSelected() const { return selected; }

signals:
	/* Emitted after writing "collapsed", so the dock can redraw. */
	void collapsedChanged();

	/* Asks the list to begin a drag for this row. */
	void dragRequested(XRayRow *row, const QPoint &globalPos);

protected:
	void paintEvent(QPaintEvent *event) override;
	void mousePressEvent(QMouseEvent *event) override;
	void mouseMoveEvent(QMouseEvent *event) override;
	void mouseDoubleClickEvent(QMouseEvent *event) override;
	void contextMenuEvent(QContextMenuEvent *event) override;
	bool eventFilter(QObject *watched, QEvent *event) override;

private slots:
	void onVisibilityToggled(bool checked);
	void onLockToggled(bool checked);
	void onExpandToggled(bool checked);

private:
	void enterEditMode();
	void exitEditMode(bool save);

	std::string owner;
	std::string sourceUuid;
	int64_t item = -1;
	bool branch = false;
	bool selected = false;

	QPoint pressPos;

	QHBoxLayout *box = nullptr;
	QCheckBox *expand = nullptr;
	QLabel *iconLabel = nullptr;
	QLabel *label = nullptr;
	QLineEdit *editor = nullptr;
	QCheckBox *vis = nullptr;
	QCheckBox *lock = nullptr;
};
