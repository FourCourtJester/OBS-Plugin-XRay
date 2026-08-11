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

#include <obs-module.h>

#include <QFrame>
#include <QLabel>
#include <QLayoutItem>
#include <QScrollArea>
#include <QVBoxLayout>

namespace {

constexpr int INDENT_PX = 14;
constexpr int ROW_MARGIN_PX = 4;

/*
 * Phase 2 renders names only. Weight and slant are here so the walk can be
 * checked at a glance -- that groups really are being traversed, and that a
 * pruned branch really is gone. Phase 6 replaces all of this with the ported
 * OBS widget, where theming does the same job properly.
 */
void applyKindStyle(QLabel *row, const xray::Node &node)
{
	QFont font = row->font();

	switch (node.kind) {
	case xray::NodeKind::SubScene:
		font.setBold(true);
		row->setProperty("xrayKind", "subscene");
		break;
	case xray::NodeKind::Group:
		font.setItalic(true);
		row->setProperty("xrayKind", "group");
		break;
	case xray::NodeKind::Source:
		row->setProperty("xrayKind", "source");
		break;
	}

	row->setFont(font);
}

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

	clear();
}

void XRayDock::clear()
{
	while (QLayoutItem *item = contentLayout->takeAt(0)) {
		if (QWidget *widget = item->widget())
			widget->deleteLater();
		delete item;
	}

	contentLayout->addStretch(1);

	placeholder->setVisible(true);
	scrollArea->setVisible(false);
}

void XRayDock::refresh()
{
	clear();

	const std::vector<xray::Node> tree = xray::walk_program_scene();
	if (tree.empty())
		return;

	addRows(tree, 0);

	placeholder->setVisible(false);
	scrollArea->setVisible(true);
}

void XRayDock::addRows(const std::vector<xray::Node> &nodes, int depth)
{
	for (const xray::Node &node : nodes) {
		QString text = QString::fromStdString(node.name);
		if (node.cyclic)
			text += QStringLiteral(" %1").arg(obs_module_text("Row.Cyclic"));

		QLabel *row = new QLabel(text, content);
		row->setTextInteractionFlags(Qt::NoTextInteraction);
		row->setContentsMargins(ROW_MARGIN_PX + INDENT_PX * depth, 1, ROW_MARGIN_PX, 1);
		applyKindStyle(row, node);

		/* Keep the trailing stretch last so rows stay top-aligned. */
		contentLayout->insertWidget(contentLayout->count() - 1, row);

		addRows(node.children, depth + 1);
	}
}
