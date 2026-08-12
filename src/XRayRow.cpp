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

#include <obs-module.h>
#include <obs-frontend-api.h>

#include <QCheckBox>
#include <QHBoxLayout>
#include <QIcon>
#include <QLabel>

namespace {

/* OBS draws source icons at 16x16 in SourceTreeItem. */
constexpr int ICON_PX = 16;

/* One level of nesting, in pixels. */
constexpr int INDENT_PX = 14;

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
	  ownerUuid(node.owner_uuid),
	  itemId(node.item_id)
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

	QHBoxLayout *box = new QHBoxLayout(this);
	box->setContentsMargins(INDENT_PX * depth, 0, 0, 0);
	box->setSpacing(0);
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
	xray::set_item_visible(ownerUuid, itemId, checked);
}

void XRayRow::onLockToggled(bool checked)
{
	xray::set_item_locked(ownerUuid, itemId, checked);
}
