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

#include "XRayRow.hpp"
#include "XRayList.hpp"

#include <obs-module.h>
#include <obs-frontend-api.h>

#include <QApplication>
#include <QCheckBox>
#include <QHBoxLayout>
#include <QIcon>
#include <QKeyEvent>
#include <QLabel>
#include <QLineEdit>
#include <QAction>
#include <QContextMenuEvent>
#include <QMenu>
#include <QMetaObject>
#include <QMouseEvent>
#include <QPainter>

namespace {

/* OBS draws source icons at 16x16 in SourceTreeItem. */
constexpr int ICON_PX = 16;

/* One level of nesting, in pixels. */
constexpr int INDENT_PX = 14;

/* Matches --border_radius in OBS's stock themes. */
constexpr int SELECTION_RADIUS_PX = 4;

/*
 * OBS's source icons are Q_PROPERTYs on OBSBasic, populated by the active
 * theme. OBSBasic is not linkable from a plugin, but the properties are
 * readable through the meta-object system on the main window, which gets us
 * the real icons for the user's current theme with nothing shipped.
 *
 * Mirrors OBSBasic::GetSourceIcon() in frontend/widgets/OBSBasic_Icons.cpp.
 */
const char *icon_property_for(const std::string &source_id)
{
	if (source_id == "scene")
		return "sceneIcon";
	if (source_id == "group")
		return "groupIcon";

	switch (obs_source_get_icon_type(source_id.c_str())) {
	case OBS_ICON_TYPE_IMAGE:
		return "imageIcon";
	case OBS_ICON_TYPE_COLOR:
		return "colorIcon";
	case OBS_ICON_TYPE_SLIDESHOW:
		return "slideshowIcon";
	case OBS_ICON_TYPE_AUDIO_INPUT:
		return "audioInputIcon";
	case OBS_ICON_TYPE_AUDIO_OUTPUT:
		return "audioOutputIcon";
	case OBS_ICON_TYPE_DESKTOP_CAPTURE:
		return "desktopCapIcon";
	case OBS_ICON_TYPE_WINDOW_CAPTURE:
		return "windowCapIcon";
	case OBS_ICON_TYPE_GAME_CAPTURE:
		return "gameCapIcon";
	case OBS_ICON_TYPE_CAMERA:
		return "cameraIcon";
	case OBS_ICON_TYPE_TEXT:
		return "textIcon";
	case OBS_ICON_TYPE_MEDIA:
		return "mediaIcon";
	case OBS_ICON_TYPE_BROWSER:
		return "browserIcon";
	case OBS_ICON_TYPE_PROCESS_AUDIO_OUTPUT:
		return "audioProcessOutputIcon";
	default:
		/* Includes OBS_ICON_TYPE_CUSTOM, which OBS also falls back on. */
		return "defaultIcon";
	}
}

QIcon source_icon(const std::string &source_id)
{
	QWidget *main = static_cast<QWidget *>(obs_frontend_get_main_window());
	if (!main)
		return {};

	return main->property(icon_property_for(source_id)).value<QIcon>();
}

} // namespace

XRayRow::XRayRow(const xray::Node &node, int depth, QWidget *parent)
	: QFrame(parent),
	  owner(node.owner_uuid),
	  sourceUuid(node.source_uuid),
	  item(node.item_id),
	  branch(!node.children.empty()),
	  selected(node.selected)
{
	setObjectName("xrayRow");

	iconLabel = new QLabel(this);
	iconLabel->setSizePolicy(QSizePolicy::Maximum, QSizePolicy::Maximum);
	iconLabel->setPixmap(source_icon(node.source_id).pixmap(QSize(ICON_PX, ICON_PX)));
	iconLabel->setStyleSheet("background: none");
	iconLabel->setProperty("class", "source-icon");

	QString text = QString::fromStdString(node.name);
	if (node.cyclic)
		text += QStringLiteral(" %1").arg(obs_module_text("Row.Cyclic"));

	label = new QLabel(text, this);
	label->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Preferred);
	label->setAttribute(Qt::WA_TranslucentBackground);

	/*
	 * Both labels are display only, and both sit over the whole width of the
	 * row. QLabel defaults to Qt::LinksAccessibleByMouse, which makes it
	 * accept mouse presses and swallow the double-click before the row ever
	 * sees it -- so renaming never started. Letting mouse events fall
	 * straight through fixes that and keeps drags starting anywhere on the
	 * row. The checkboxes are left alone; they need their own clicks.
	 */
	label->setAttribute(Qt::WA_TransparentForMouseEvents);
	iconLabel->setAttribute(Qt::WA_TransparentForMouseEvents);

	vis = new QCheckBox(this);
	vis->setProperty("class", "checkbox-icon indicator-visibility");
	vis->setChecked(node.visible);
	vis->setSizePolicy(QSizePolicy::Maximum, QSizePolicy::Preferred);
	vis->setAccessibleName(obs_module_text("Row.Visibility"));
	vis->setToolTip(obs_module_text("Row.Visibility"));

	lock = new QCheckBox(this);
	lock->setProperty("class", "checkbox-icon indicator-lock");
	lock->setChecked(node.locked);
	lock->setSizePolicy(QSizePolicy::Maximum, QSizePolicy::Preferred);
	lock->setAccessibleName(obs_module_text("Row.Lock"));
	lock->setToolTip(obs_module_text("Row.Lock"));

#ifdef __APPLE__
	/* Matches SourceTreeItem: without this the checkboxes are laid out
	 * against the macOS native control rect and end up misaligned. */
	vis->setAttribute(Qt::WA_LayoutUsesWidgetRect);
	lock->setAttribute(Qt::WA_LayoutUsesWidgetRect);
#endif

	/* Greyed out while hidden, as OBS does. */
	iconLabel->setEnabled(node.visible);
	label->setEnabled(node.visible);

	if (selected) {
		/* The row paints the highlight itself, so the text has to be
		 * repainted in the matching colour or it disappears into it. */
		QPalette selectedText = label->palette();
		selectedText.setColor(QPalette::WindowText, palette().color(QPalette::HighlightedText));
		label->setPalette(selectedText);
	}

	box = new QHBoxLayout(this);
	box->setContentsMargins(INDENT_PX * depth, 0, 0, 0);
	box->setSpacing(0);

	if (branch) {
		/*
		 * Checked means collapsed, matching OBS: the theme maps
		 * :checked to the closed arrow and :unchecked to the open one.
		 */
		expand = new QCheckBox(this);
		expand->setProperty("class", "checkbox-icon indicator-expand");
		expand->setSizePolicy(QSizePolicy::Maximum, QSizePolicy::Maximum);
		expand->setChecked(node.collapsed);
		expand->setAccessibleName(obs_module_text("Row.Expand"));
#ifdef __APPLE__
		expand->setAttribute(Qt::WA_LayoutUsesWidgetRect);
#endif
		box->addWidget(expand);
		connect(expand, &QCheckBox::toggled, this, &XRayRow::onExpandToggled);
	} else {
		/* Keeps leaf names aligned with their siblings' text. */
		box->addSpacing(ICON_PX);
	}

	box->addWidget(iconLabel);
	box->addSpacing(2);
	box->addWidget(label);
	box->addWidget(vis);
	box->addWidget(lock);

	connect(vis, &QCheckBox::toggled, this, &XRayRow::onVisibilityToggled);
	connect(lock, &QCheckBox::toggled, this, &XRayRow::onLockToggled);
}

void XRayRow::onVisibilityToggled(bool checked)
{
	/*
	 * No optimistic update. The write goes to libobs, libobs emits
	 * item_visible, and the dock rebuilds from that -- so what the row shows
	 * is always what the scene actually holds, including when the write is
	 * dropped because the item has gone.
	 */
	xray::set_item_visible(owner, item, checked);
}

void XRayRow::onLockToggled(bool checked)
{
	xray::set_item_locked(owner, item, checked);
}

void XRayRow::onExpandToggled(bool checked)
{
	/*
	 * Private settings emit no signal, so unlike visibility and lock this
	 * one has to ask for the redraw itself.
	 */
	xray::set_item_collapsed(owner, item, checked);
	emit collapsedChanged();
}

/*
 * Mirrors the selection held in libobs, which is what the Sources dock writes
 * when a row is clicked there. Painted rather than styled: these rows are plain
 * widgets, not view items, so the theme's QListView::item:selected rule cannot
 * reach them, and the palette is the closest theme-following equivalent.
 */
void XRayRow::paintEvent(QPaintEvent *event)
{
	if (selected) {
		QPainter painter(this);
		painter.setRenderHint(QPainter::Antialiasing);
		painter.setPen(Qt::NoPen);
		painter.setBrush(palette().highlight());
		painter.drawRoundedRect(rect(), SELECTION_RADIUS_PX, SELECTION_RADIUS_PX);
	}

	QFrame::paintEvent(event);
}

/* ------------------------------------------------------ properties menu */

/*
 * Matches SourceTreeItem::mouseDoubleClickEvent: a branch toggles, anything
 * else opens Properties, and a source that has none is left alone. Rename is
 * deliberately not bound here -- OBS puts it on the context menu, and binding
 * it to double-click would take Properties away from exactly the rows people
 * open it on most.
 */
void XRayRow::mouseDoubleClickEvent(QMouseEvent *event)
{
	if (event->button() != Qt::LeftButton || editor) {
		QFrame::mouseDoubleClickEvent(event);
		return;
	}

	if (expand) {
		expand->setChecked(!expand->isChecked());
		return;
	}

	xray::open_source_properties(sourceUuid);
}

void XRayRow::contextMenuEvent(QContextMenuEvent *event)
{
	if (editor) {
		QFrame::contextMenuEvent(event);
		return;
	}

	QMenu menu(this);

	QAction *properties = menu.addAction(obs_module_text("Row.Properties"));
	properties->setEnabled(xray::source_is_configurable(sourceUuid));
	connect(properties, &QAction::triggered, this, [this] { xray::open_source_properties(sourceUuid); });

	QAction *filters = menu.addAction(obs_module_text("Row.Filters"));
	connect(filters, &QAction::triggered, this, [this] { xray::open_source_filters(sourceUuid); });

	QAction *interact = menu.addAction(obs_module_text("Row.Interact"));
	interact->setEnabled(xray::source_is_interactive(sourceUuid));
	connect(interact, &QAction::triggered, this, [this] { xray::open_source_interaction(sourceUuid); });

	menu.addSeparator();

	QAction *rename = menu.addAction(obs_module_text("Row.Rename"));

	/*
	 * Queued. The action fires while exec() below is still unwinding, and
	 * enterEditMode() rearranges this row's layout -- doing that underneath a
	 * running menu is asking for trouble.
	 */
	connect(rename, &QAction::triggered, this,
		[this] { QMetaObject::invokeMethod(this, [this] { enterEditMode(); }, Qt::QueuedConnection); });

	/*
	 * exec() spins a nested event loop, so scene signals keep arriving and a
	 * rebuild would delete this row while its own handler is still on the
	 * stack -- the same hazard QDrag::exec() has. Hold the dock off until the
	 * menu closes.
	 */
	/* Rows are parented to the XRayList, so no back-pointer is needed. */
	XRayList *owner = qobject_cast<XRayList *>(parentWidget());

	if (owner)
		owner->enterNestedLoop();

	menu.exec(event->globalPos());

	if (owner)
		owner->exitNestedLoop();
}

/* ---------------------------------------------------------------- rename */

void XRayRow::enterEditMode()
{
	setFocusPolicy(Qt::StrongFocus);

	const int index = box->indexOf(label);
	box->removeWidget(label);
	label->hide();

	editor = new QLineEdit(label->text(), this);
	editor->setStyleSheet("background: none");
	editor->selectAll();
	editor->installEventFilter(this);
	box->insertWidget(index, editor);
	setFocusProxy(editor);
	editor->setFocus();
}

void XRayRow::exitEditMode(bool save)
{
	if (!editor)
		return;

	const std::string entered = editor->text().toStdString();

	setFocusProxy(nullptr);
	const int index = box->indexOf(editor);
	box->removeWidget(editor);
	editor->deleteLater();
	editor = nullptr;
	setFocusPolicy(Qt::NoFocus);
	box->insertWidget(index, label);
	label->show();

	if (!save)
		return;

	/*
	 * A refused rename -- empty or already taken -- just leaves the name
	 * alone. On success libobs emits "rename", which phase 3's watch turns
	 * into a rebuild, so the row is never updated by hand here.
	 */
	xray::rename_source(sourceUuid, entered);
}

bool XRayRow::eventFilter(QObject *watched, QEvent *event)
{
	if (watched != editor)
		return QFrame::eventFilter(watched, event);

	if (event->type() == QEvent::FocusOut) {
		/* Queued: Qt is still delivering, and exitEditMode destroys the editor. */
		QMetaObject::invokeMethod(this, [this] { exitEditMode(true); }, Qt::QueuedConnection);
		return false;
	}

	if (event->type() == QEvent::KeyPress) {
		const int key = static_cast<QKeyEvent *>(event)->key();

		if (key == Qt::Key_Escape) {
			QMetaObject::invokeMethod(this, [this] { exitEditMode(false); }, Qt::QueuedConnection);
			return true;
		}

		if (key == Qt::Key_Return || key == Qt::Key_Enter) {
			QMetaObject::invokeMethod(this, [this] { exitEditMode(true); }, Qt::QueuedConnection);
			return true;
		}
	}

	return QFrame::eventFilter(watched, event);
}

/* ------------------------------------------------------------------ drag */

void XRayRow::mousePressEvent(QMouseEvent *event)
{
	if (event->button() == Qt::LeftButton) {
		pressPos = event->pos();

		/*
		 * Accepted rather than passed up. QWidget's default handler
		 * ignores the press, which lets it propagate to the list and
		 * costs this row the follow-up double-click.
		 */
		event->accept();
		return;
	}

	QFrame::mousePressEvent(event);
}

void XRayRow::mouseMoveEvent(QMouseEvent *event)
{
	if (!(event->buttons() & Qt::LeftButton) || editor) {
		QFrame::mouseMoveEvent(event);
		return;
	}

	if ((event->pos() - pressPos).manhattanLength() < QApplication::startDragDistance()) {
		QFrame::mouseMoveEvent(event);
		return;
	}

	emit dragRequested(this, event->globalPosition().toPoint());
}
