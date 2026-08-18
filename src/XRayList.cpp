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

#include "XRayList.hpp"
#include "XRayRow.hpp"

#include <QApplication>
#include <QDrag>
#include <QDragEnterEvent>
#include <QDragLeaveEvent>
#include <QDragMoveEvent>
#include <QDropEvent>
#include <QMimeData>
#include <QPainter>
#include <QPalette>
#include <QVBoxLayout>

namespace {

/* Private to this widget; nothing outside the dock should accept it. */
const char *const MIME_ROW = "application/x-obs-xray-row";

constexpr int INDICATOR_THICKNESS = 2;

} // namespace

XRayList::XRayList(QWidget *parent) : QWidget(parent)
{
	setObjectName("xrayList");
	setAcceptDrops(true);

	box = new QVBoxLayout(this);
	box->setContentsMargins(0, 0, 0, 0);
	box->setSpacing(0);
	box->addStretch(1);
}

void XRayList::addRow(XRayRow *row)
{
	rows.push_back(row);

	/* Keep the trailing stretch last so rows stay top-aligned. */
	box->insertWidget(box->count() - 1, row);

	connect(row, &XRayRow::dragRequested, this, &XRayList::beginDrag);
}

void XRayList::clearRows()
{
	rows.clear();
	dragged = nullptr;
	indicatorY = -1;

	while (QLayoutItem *item = box->takeAt(0)) {
		if (QWidget *widget = item->widget())
			widget->deleteLater();
		delete item;
	}

	box->addStretch(1);
}

void XRayList::beginDrag(XRayRow *row, const QPoint &)
{
	dragged = row;

	QMimeData *mime = new QMimeData;
	mime->setData(MIME_ROW, QByteArray());

	QDrag *drag = new QDrag(this);
	drag->setMimeData(mime);
	drag->setPixmap(row->grab());
	drag->setHotSpot(QPoint(row->width() / 2, row->height() / 2));

	drag->exec(Qt::MoveAction);

	/* exec() is blocking; by here the drop has been handled or abandoned. */
	dragged = nullptr;
	indicatorY = -1;
	update();
}

XRayRow *XRayList::dropTargetAt(int y, bool &valid)
{
	valid = false;
	indicatorY = -1;

	if (!dragged)
		return nullptr;

	XRayRow *lastSibling = nullptr;

	for (XRayRow *row : rows) {
		/* Only rows of the same scene are candidate neighbours. */
		if (row->ownerUuid() != dragged->ownerUuid())
			continue;

		const int mid = row->y() + row->height() / 2;
		if (y < mid) {
			valid = true;
			indicatorY = row->y();
			return row;
		}

		lastSibling = row;
	}

	if (!lastSibling)
		return nullptr;

	valid = true;
	indicatorY = lastSibling->y() + lastSibling->height();
	return nullptr;
}

void XRayList::dragEnterEvent(QDragEnterEvent *event)
{
	if (dragged && event->mimeData()->hasFormat(MIME_ROW))
		event->acceptProposedAction();
	else
		event->ignore();
}

void XRayList::dragMoveEvent(QDragMoveEvent *event)
{
	bool valid = false;
	dropTargetAt(event->position().toPoint().y(), valid);
	update();

	if (valid)
		event->acceptProposedAction();
	else
		event->ignore();
}

void XRayList::dragLeaveEvent(QDragLeaveEvent *)
{
	indicatorY = -1;
	update();
}

void XRayList::dropEvent(QDropEvent *event)
{
	bool valid = false;
	XRayRow *before = dropTargetAt(event->position().toPoint().y(), valid);

	indicatorY = -1;
	update();

	if (!valid || !dragged) {
		event->ignore();
		return;
	}

	/*
	 * Anchored on the neighbouring row rather than on a row index. At the
	 * top level the rows are a pruned subset of the scene's items, so a
	 * displayed index is not an order position -- the real one is worked out
	 * from the scene itself when this is applied.
	 */
	const int64_t beforeId = before ? before->itemId() : -1;

	if (beforeId != dragged->itemId())
		emit reorderRequested(dragged->ownerUuid(), dragged->itemId(), beforeId);

	event->acceptProposedAction();
}

void XRayList::paintEvent(QPaintEvent *event)
{
	QWidget::paintEvent(event);

	if (indicatorY < 0)
		return;

	QPainter painter(this);
	painter.fillRect(0, indicatorY, width(), INDICATOR_THICKNESS, palette().highlight());
}
