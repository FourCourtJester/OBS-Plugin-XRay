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

#include <QWidget>

#include <vector>

class QVBoxLayout;
class XRayRow;

/*
 * The stack of rows, and the drop target for reordering.
 *
 * Reordering is confined to one scene: a row can only be dropped between rows
 * that share its owning scene. That is the spec, and it is also the only
 * meaning a drop has -- rows at different depths belong to different scenes, so
 * "between" them is not a position that exists. Candidate drop points are
 * therefore filtered to matching owners rather than the drop being rejected
 * afterwards, which keeps the indicator honest about where a release will land.
 */
class XRayList : public QWidget {
	Q_OBJECT

public:
	explicit XRayList(QWidget *parent = nullptr);

	void addRow(XRayRow *row);
	void clearRows();

	/*
	 * True while QDrag::exec() is running. It spins a nested event loop, so
	 * signals keep being delivered and a rebuild would delete these rows --
	 * including the one whose mouseMoveEvent is still on the stack. The dock
	 * defers refreshing until this clears.
	 */
	bool isDragging() const { return dragged != nullptr; }

	const std::vector<XRayRow *> &rowWidgets() const { return rows; }

signals:
	/* A completed reorder, for the dock to apply. */
	void reorderRequested(const std::string &ownerUuid, int64_t itemId, int64_t beforeItemId);

public slots:
	void beginDrag(XRayRow *row, const QPoint &globalPos);

protected:
	void dragEnterEvent(QDragEnterEvent *event) override;
	void dragMoveEvent(QDragMoveEvent *event) override;
	void dragLeaveEvent(QDragLeaveEvent *event) override;
	void dropEvent(QDropEvent *event) override;
	void paintEvent(QPaintEvent *event) override;

private:
	/*
	 * Resolves a cursor position to a drop slot among the rows that share
	 * dragged's owner. Returns the row the dragged item would land above, or
	 * nullptr for "after the last of them"; sets indicatorY either way.
	 */
	XRayRow *dropTargetAt(int y, bool &valid);

	QVBoxLayout *box = nullptr;
	std::vector<XRayRow *> rows;

	XRayRow *dragged = nullptr;
	int indicatorY = -1;
};
