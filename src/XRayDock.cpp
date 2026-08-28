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
#include "XRayList.hpp"
#include "XRayRow.hpp"

#include <obs-module.h>
#include <obs-frontend-api.h>

#include <util/config-file.h>

#include <QFrame>
#include <QHBoxLayout>
#include <QLabel>
#include <QMetaObject>
#include <QPointer>
#include <QScrollArea>
#include <QTimer>
#include <QToolButton>
#include <QVBoxLayout>

namespace {

/*
 * Long enough to swallow the burst from one user action, short enough that the
 * panel still feels live.
 */
constexpr int COALESCE_MS = 50;

constexpr int TOOLBAR_MARGIN_PX = 4;

/*
 * Where the show-all preference lives.
 *
 * User config rather than profile config: this is a standing choice about what
 * the panel is for, not something that should change when the operator switches
 * between a streaming and a recording profile.
 */
const char *const CONFIG_SECTION = "XRay";
const char *const CONFIG_SHOW_ALL = "ShowAllSources";

bool load_show_all()
{
	config_t *config = obs_frontend_get_user_config();
	if (!config)
		return false;

	config_set_default_bool(config, CONFIG_SECTION, CONFIG_SHOW_ALL, false);
	return config_get_bool(config, CONFIG_SECTION, CONFIG_SHOW_ALL);
}

void save_show_all(bool value)
{
	config_t *config = obs_frontend_get_user_config();
	if (!config)
		return;

	config_set_bool(config, CONFIG_SECTION, CONFIG_SHOW_ALL, value);

	/* OBS writes this file on exit, but a crash in between should not cost
	 * the setting -- it is one line and the write is cheap. */
	config_save_safe(config, "tmp", "bak");
}

/*
 * Structural signals on a scene or group source.
 *
 * The reorder signal is "reorder", not "item_reorder" -- see obs_scene_signals[]
 * in obs-scene.c. "refresh" covers the bulk rebuilds that individual item
 * signals do not announce.
 *
 * item_transform is deliberately absent. It fires continuously while an item is
 * dragged in the preview, and this dock renders nothing derived from a
 * transform, so subscribing would be pure churn.
 *
 * item_select and item_deselect are here so that picking a row in OBS's own
 * Sources dock highlights and scrolls to the matching row here --
 * SourceTree calls obs_sceneitem_select(), which is what raises them.
 */
const char *const CONTAINER_SIGNALS[] = {
	"item_add", "item_remove", "reorder", "refresh", "item_visible", "item_locked", "item_select", "item_deselect",
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

	list = new XRayList;

	scrollArea = new QScrollArea(this);
	scrollArea->setWidget(list);
	scrollArea->setWidgetResizable(true);
	scrollArea->setFrameShape(QFrame::NoFrame);

	placeholder = new QLabel(obs_module_text("Dock.Empty"), this);
	placeholder->setAlignment(Qt::AlignCenter);
	placeholder->setWordWrap(true);
	placeholder->setEnabled(false);

	QToolButton *collapseButton = new QToolButton(this);
	collapseButton->setText(obs_module_text("Dock.CollapseAll"));
	collapseButton->setToolTip(obs_module_text("Dock.CollapseAll"));
	collapseButton->setAutoRaise(true);

	QToolButton *expandButton = new QToolButton(this);
	expandButton->setText(obs_module_text("Dock.ExpandAll"));
	expandButton->setToolTip(obs_module_text("Dock.ExpandAll"));
	expandButton->setAutoRaise(true);

	showAll = load_show_all();

	showAllButton = new QToolButton(this);
	showAllButton->setText(obs_module_text("Dock.ShowAll"));
	showAllButton->setToolTip(obs_module_text("Dock.ShowAll.Tip"));
	showAllButton->setAutoRaise(true);
	showAllButton->setCheckable(true);
	showAllButton->setChecked(showAll);

	QWidget *toolbar = new QWidget(this);
	toolbar->setObjectName("xrayToolbar");

	QHBoxLayout *toolbarLayout = new QHBoxLayout(toolbar);
	toolbarLayout->setContentsMargins(TOOLBAR_MARGIN_PX, TOOLBAR_MARGIN_PX, TOOLBAR_MARGIN_PX, TOOLBAR_MARGIN_PX);
	toolbarLayout->setSpacing(TOOLBAR_MARGIN_PX);
	/* The view toggle sits opposite the two collapse buttons: it changes
	 * what the list contains, they only change how much of it is unfolded. */
	toolbarLayout->addWidget(showAllButton);
	toolbarLayout->addStretch(1);
	toolbarLayout->addWidget(collapseButton);
	toolbarLayout->addWidget(expandButton);

	QVBoxLayout *layout = new QVBoxLayout(this);
	layout->setContentsMargins(0, 0, 0, 0);
	layout->addWidget(placeholder);
	layout->addWidget(scrollArea);
	layout->addWidget(toolbar);

	connect(collapseButton, &QToolButton::clicked, this, &XRayDock::collapseAll);
	connect(expandButton, &QToolButton::clicked, this, &XRayDock::expandAll);
	connect(showAllButton, &QToolButton::toggled, this, &XRayDock::setShowAll);

	refreshTimer = new QTimer(this);
	refreshTimer->setSingleShot(true);
	refreshTimer->setInterval(COALESCE_MS);
	connect(refreshTimer, &QTimer::timeout, this, &XRayDock::refresh);

	connect(list, &XRayList::reorderRequested, this, &XRayDock::applyReorder);

	clear();
}

XRayDock::~XRayDock()
{
	watches.clear();
}

void XRayDock::clear()
{
	watches.clear();
	list->clearRows();

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

void XRayDock::collapseAll()
{
	xray::set_all_collapsed(true, showAll);
	refresh();
}

void XRayDock::expandAll()
{
	xray::set_all_collapsed(false, showAll);
	refresh();
}

void XRayDock::setShowAll(bool value)
{
	if (showAll == value)
		return;

	showAll = value;
	save_show_all(value);
	refresh();
}

void XRayDock::applyReorder(const std::string &ownerUuid, int64_t itemId, int64_t beforeItemId)
{
	/* libobs emits "reorder", which the watch turns into a rebuild. */
	xray::move_item_before(ownerUuid, itemId, beforeItemId);
}

void XRayDock::refresh()
{
	/*
	 * Never rebuild under a nested event loop. QDrag::exec() and QMenu::exec()
	 * both keep delivering scene signals, so a rebuild would delete the row
	 * whose event handler is still on the stack. Try again once it ends.
	 */
	if (list->isBusy()) {
		refreshTimer->start();
		return;
	}

	refreshTimer->stop();
	clear();

	const xray::WalkResult result = xray::walk_program_scene(showAll);

	/*
	 * Watches are rewired even when nothing is rendered. The program scene is
	 * always watched, so adding the first scene source to an empty program
	 * scene still wakes the dock up.
	 */
	rewatch(result.watches);

	if (result.tree.empty()) {
		/* Two different empties: nothing nested to show, versus a scene
		 * with nothing in it at all. */
		placeholder->setText(obs_module_text(showAll ? "Dock.Empty.All" : "Dock.Empty"));
		return;
	}

	addRows(result.tree, 0);

	placeholder->setVisible(false);
	scrollArea->setVisible(true);

	revealSelection();
}

void XRayDock::revealSelection()
{
	XRayRow *found = nullptr;
	for (XRayRow *row : list->rowWidgets()) {
		if (row->isSelected()) {
			found = row;
			break;
		}
	}

	if (!found) {
		revealedOwner.clear();
		revealedItem = -1;
		return;
	}

	if (found->ownerUuid() == revealedOwner && found->itemId() == revealedItem)
		return;

	revealedOwner = found->ownerUuid();
	revealedItem = found->itemId();

	/*
	 * Queued, because the rows were only just added and their geometry is
	 * not settled until the layout has run. Guarded, because another rebuild
	 * can land first and delete every row before this arrives.
	 */
	QPointer<XRayRow> target = found;
	QMetaObject::invokeMethod(
		this,
		[this, target] {
			if (target)
				scrollArea->ensureWidgetVisible(target);
		},
		Qt::QueuedConnection);
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
		XRayRow *row = new XRayRow(node, depth, list);
		list->addRow(row);

		connect(row, &XRayRow::redrawRequested, this, &XRayDock::scheduleRefresh);

		/*
		 * Collapsing hides rows only. The subtree was still walked --
		 * pruning depends on what is underneath, so a collapsed branch
		 * has to keep earning its place.
		 */
		if (!node.collapsed)
			addRows(node.children, depth + 1);
	}
}
