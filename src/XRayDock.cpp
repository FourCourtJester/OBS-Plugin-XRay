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
#include "XRayRow.hpp"

#include <obs-module.h>

#include <QFrame>
#include <QLabel>
#include <QLayoutItem>
#include <QMetaObject>
#include <QScrollArea>
#include <QTimer>
#include <QVBoxLayout>

namespace {

constexpr int ROW_MARGIN_PX = 4;

/*
 * Long enough to swallow the burst from one user action, short enough that the
 * panel still feels live.
 */
constexpr int COALESCE_MS = 50;

/*
 * Structural signals on a scene or group source.
 *
 * The reorder signal is "reorder", not "item_reorder" -- see obs_scene_signals[]
 * in obs-scene.c. "refresh" covers the bulk rebuilds that individual item
 * signals do not announce.
 *
 * item_transform is deliberately absent. It fires continuously while an item is
 * dragged in the preview, and this dock renders nothing derived from a
 * transform, so subscribing would be pure churn. item_select and item_deselect
 * are absent for the same reason: selection stays local to the dock.
 */
const char *const CONTAINER_SIGNALS[] = {
	"item_add", "item_remove", "reorder", "refresh", "item_visible", "item_locked",
};

/* Leaf sources only need to redraw when their name or their existence changes. */
const char *const SOURCE_SIGNALS[] = {
	"rename",
	"destroy",
	"remove",
};

} // namespace

XRayDock::XRayDock(QWidget *parent) : QWidget(parent)
{
	setObjectName("xrayDock");
	setMinimumWidth(150);

	content = new QWidget;
	content->setObjectName("xrayDockContent");

	contentLayout = new QVBoxLayout(content);
	contentLayout->setContentsMargins(0, ROW_MARGIN_PX, 0, ROW_MARGIN_PX);
	contentLayout->setSpacing(0);
	contentLayout->addStretch(1);

	scrollArea = new QScrollArea(this);
	scrollArea->setWidget(content);
	scrollArea->setWidgetResizable(true);
	scrollArea->setFrameShape(QFrame::NoFrame);

	placeholder = new QLabel(obs_module_text("Dock.Empty"), this);
	placeholder->setAlignment(Qt::AlignCenter);
	placeholder->setWordWrap(true);
	placeholder->setEnabled(false);

	QVBoxLayout *layout = new QVBoxLayout(this);
	layout->setContentsMargins(0, 0, 0, 0);
	layout->addWidget(placeholder);
	layout->addWidget(scrollArea);

	refreshTimer = new QTimer(this);
	refreshTimer->setSingleShot(true);
	refreshTimer->setInterval(COALESCE_MS);
	connect(refreshTimer, &QTimer::timeout, this, &XRayDock::refresh);

	clear();
}

XRayDock::~XRayDock()
{
	watches.clear();
}

void XRayDock::clear()
{
	watches.clear();

	while (QLayoutItem *item = contentLayout->takeAt(0)) {
		if (QWidget *widget = item->widget())
			widget->deleteLater();
		delete item;
	}

	contentLayout->addStretch(1);

	placeholder->setVisible(true);
	scrollArea->setVisible(false);
}

void XRayDock::scheduleRefresh()
{
	refreshTimer->start();
}

void XRayDock::onContainerChanged(void *data, calldata_t *)
{
	/*
	 * Scene signals arrive on whichever thread made the change, never
	 * reliably the UI thread, so nothing here may touch a widget directly.
	 */
	QMetaObject::invokeMethod(static_cast<XRayDock *>(data), "scheduleRefresh", Qt::QueuedConnection);
}

void XRayDock::onSourceChanged(void *data, calldata_t *)
{
	QMetaObject::invokeMethod(static_cast<XRayDock *>(data), "scheduleRefresh", Qt::QueuedConnection);
}

void XRayDock::refresh()
{
	refreshTimer->stop();
	clear();

	const xray::WalkResult result = xray::walk_program_scene();

	/*
	 * Watches are rewired even when nothing is rendered. The program scene is
	 * always watched, so adding the first scene source to an empty program
	 * scene still wakes the dock up.
	 */
	rewatch(result.watches);

	if (result.tree.empty())
		return;

	addRows(result.tree, 0);

	placeholder->setVisible(false);
	scrollArea->setVisible(true);
}

void XRayDock::rewatch(const std::vector<xray::Watch> &sources)
{
	for (const xray::Watch &entry : sources) {
		OBSSourceAutoRelease source = obs_get_source_by_uuid(entry.uuid.c_str());
		if (!source)
			continue;

		signal_handler_t *handler = obs_source_get_signal_handler(source);
		if (!handler)
			continue;

		if (entry.container) {
			for (const char *signal : CONTAINER_SIGNALS)
				watches.emplace_back(handler, signal, onContainerChanged, this);
		}

		for (const char *signal : SOURCE_SIGNALS)
			watches.emplace_back(handler, signal, onSourceChanged, this);
	}
}

void XRayDock::addRows(const std::vector<xray::Node> &nodes, int depth)
{
	for (const xray::Node &node : nodes) {
		XRayRow *row = new XRayRow(node, depth, content);

		/* Keep the trailing stretch last so rows stay top-aligned. */
		contentLayout->insertWidget(contentLayout->count() - 1, row);

		addRows(node.children, depth + 1);
	}
}
