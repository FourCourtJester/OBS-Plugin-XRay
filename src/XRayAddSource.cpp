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

#include "XRayAddSource.hpp"

#include <obs-module.h>

#include <QCheckBox>
#include <QDialogButtonBox>
#include <QLineEdit>
#include <QListWidget>
#include <QMessageBox>
#include <QPushButton>
#include <QRadioButton>
#include <QVBoxLayout>

namespace {

/*
 * "Video Capture Device", then "Video Capture Device 2", and so on -- the same
 * shape OBS's own default name takes, so the dialog opens on a name that can
 * simply be accepted.
 */
std::string default_name(const std::string &base)
{
	std::string candidate = base;

	for (int suffix = 2; xray::source_name_taken(candidate); suffix++)
		candidate = base + " " + std::to_string(suffix);

	return candidate;
}

} // namespace

XRayAddSource::XRayAddSource(const std::string &ownerUuid, const xray::SourceType &sourceType, QWidget *parent)
	: QDialog(parent),
	  owner(ownerUuid),
	  type(sourceType)
{
	setWindowTitle(QString::fromStdString(type.name));
	setWindowModality(Qt::WindowModal);

	createNew = new QRadioButton(obs_module_text("Add.CreateNew"), this);
	createNew->setChecked(true);

	/*
	 * A scene can only be picked, never made here. OBS disables Create new
	 * for scenes too, and for good reason: a scene created from this dialog
	 * would not be in the scene list, which is not what "add a scene"
	 * means to anyone.
	 */
	const bool pickOnly = type.id == "scene";

	name = new QLineEdit(QString::fromStdString(default_name(type.name)), this);

	addExisting = new QRadioButton(obs_module_text("Add.AddExisting"), this);
	existing = new QListWidget(this);

	for (const std::string &candidate : xray::addable_sources(owner, type.id))
		existing->addItem(QString::fromStdString(candidate));

	/* Nothing to pick means the choice is not a choice; OBS greys it out
	 * the same way rather than offering an empty list. */
	if (existing->count() == 0) {
		addExisting->setEnabled(false);
		existing->setEnabled(false);
	} else {
		existing->setCurrentRow(0);
	}

	if (pickOnly) {
		createNew->setChecked(false);
		createNew->setEnabled(false);
		addExisting->setChecked(true);
	}

	visible = new QCheckBox(obs_module_text("Add.MakeVisible"), this);
	visible->setChecked(true);

	QDialogButtonBox *buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
	okButton = buttons->button(QDialogButtonBox::Ok);

	QVBoxLayout *box = new QVBoxLayout(this);
	box->addWidget(createNew);
	box->addWidget(name);
	box->addWidget(addExisting);
	box->addWidget(existing);
	box->addWidget(visible);
	box->addWidget(buttons);

	connect(createNew, &QRadioButton::toggled, this, &XRayAddSource::updateEnabled);
	connect(name, &QLineEdit::textChanged, this, &XRayAddSource::updateEnabled);
	connect(existing, &QListWidget::currentRowChanged, this, &XRayAddSource::updateEnabled);

	/*
	 * Not connected straight to accept(): the add can fail -- a name taken
	 * between opening the dialog and pressing OK -- and the dialog stays up
	 * with the message when it does.
	 */
	connect(buttons, &QDialogButtonBox::accepted, this, &XRayAddSource::addSource);
	connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);

	name->selectAll();
	name->setFocus();

	updateEnabled();
}

void XRayAddSource::updateEnabled()
{
	const bool creating = createNew->isChecked();

	name->setEnabled(creating);
	existing->setEnabled(!creating && existing->count() > 0);

	okButton->setEnabled(creating ? !name->text().trimmed().isEmpty() : existing->currentItem() != nullptr);
}

void XRayAddSource::addSource()
{
	const bool makeVisible = visible->isChecked();

	if (createNew->isChecked()) {
		const std::string wanted = name->text().trimmed().toStdString();

		if (xray::source_name_taken(wanted)) {
			/* Refused rather than uniquified, matching OBS: the
			 * operator typed a name and should hear that it clashes. */
			QMessageBox::information(this, obs_module_text("Add.NameExists.Title"),
						 obs_module_text("Add.NameExists.Text"));
			return;
		}

		if (!xray::add_new_source(owner, type.id, wanted, makeVisible))
			return;

	} else {
		QListWidgetItem *chosen = existing->currentItem();
		if (!chosen)
			return;

		/* A reference, not a copy -- the same as OBS's Add Existing. */
		if (!xray::add_existing_source(owner, chosen->text().toStdString(), false, makeVisible))
			return;
	}

	accept();
}
