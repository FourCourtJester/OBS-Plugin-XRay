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
#include "XRayAddSource.hpp"
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
#include <QColorDialog>
#include <QContextMenuEvent>
#include <QActionGroup>
#include <QGridLayout>
#include <QGuiApplication>
#include <QMenu>
#include <QMessageBox>
#include <QPushButton>
#include <QScreen>
#include <QMetaObject>
#include <QMouseEvent>
#include <QPainter>
#include <QPointer>
#include <QSpinBox>
#include <QWidgetAction>

#include <functional>
#include <utility>
#include <vector>

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
	  sourceName(node.name),
	  item(node.item_id),
	  branch(!node.children.empty()),
	  selected(node.selected)
{
	setObjectName("xrayRow");
	applyRowColor();

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
	emit redrawRequested();
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

namespace {

/*
 * A run of mutually exclusive settings, with the current one ticked.
 *
 * Every property menu OBS offers has this shape, so one helper keeps each of
 * them down to its list of labels rather than a dozen near-identical blocks.
 * Added into a menu rather than creating one, because OBS puts two of these
 * runs -- deinterlace mode and field order -- in a single submenu.
 */
template<typename Value>
void addChoiceEntries(QMenu *menu, const std::vector<std::pair<QString, Value>> &choices, Value current,
		      const std::function<void(Value)> &apply)
{
	QActionGroup *group = new QActionGroup(menu);
	group->setExclusive(true);

	for (const auto &choice : choices) {
		QAction *action = menu->addAction(choice.first);
		action->setCheckable(true);
		action->setChecked(choice.second == current);
		group->addAction(action);

		const Value value = choice.second;
		QObject::connect(action, &QAction::triggered, menu, [apply, value] { apply(value); });
	}
}

template<typename Value>
void addChoiceMenu(QMenu &parent, const QString &title, const std::vector<std::pair<QString, Value>> &choices,
		   Value current, const std::function<void(Value)> &apply)
{
	addChoiceEntries(parent.addMenu(title), choices, current, apply);
}

/*
 * The eight swatches, verbatim from OBS. Both the menu that holds the buttons
 * and the list that holds the rows are styled with this, so a colour set here
 * is the same colour the stock Sources dock draws.
 */
const char *const SWATCH_STYLESHEET = "*[bgColor=\"1\"]{background-color:rgba(255,68,68,33%);}"
				      "*[bgColor=\"2\"]{background-color:rgba(255,255,68,33%);}"
				      "*[bgColor=\"3\"]{background-color:rgba(68,255,68,33%);}"
				      "*[bgColor=\"4\"]{background-color:rgba(68,255,255,33%);}"
				      "*[bgColor=\"5\"]{background-color:rgba(68,68,255,33%);}"
				      "*[bgColor=\"6\"]{background-color:rgba(255,68,255,33%);}"
				      "*[bgColor=\"7\"]{background-color:rgba(68,68,68,33%);}"
				      "*[bgColor=\"8\"]{background-color:rgba(255,255,255,33%);}";

} // namespace

/*
 * The row's tint, which OBS calls "Set Color".
 *
 * Mirrors SourceTreeItem's own three cases exactly, including the bgColor
 * property being one less than the stored preset -- the stored 0 means no
 * colour, so the eight swatches are stored as 2..9 and drawn as 1..8.
 */
void XRayRow::applyRowColor()
{
	const xray::ItemColor colour = xray::item_color(owner, item);

	if (colour.preset == 1) {
		setStyleSheet(QStringLiteral("background: %1").arg(QString::fromStdString(colour.custom)));
	} else if (colour.preset > 1) {
		setStyleSheet(SWATCH_STYLESHEET);
		setProperty("bgColor", colour.preset - 1);
	} else {
		setStyleSheet("background: none");
	}
}

void XRayRow::buildColorMenu(QMenu *menu)
{
	menu->setStyleSheet(SWATCH_STYLESHEET);

	const xray::ItemColor colour = xray::item_color(owner, item);

	QAction *clear = menu->addAction(obs_module_text("Row.Color.Clear"));
	clear->setCheckable(true);
	clear->setChecked(colour.preset == 0);
	connect(clear, &QAction::triggered, this, [this] {
		xray::set_item_color(owner, item, 0, std::string());
		emit redrawRequested();
	});

	QAction *custom = menu->addAction(obs_module_text("Row.Color.Custom"));
	custom->setCheckable(true);
	custom->setChecked(colour.preset == 1);
	connect(custom, &QAction::triggered, this, [this] { chooseCustomColor(); });

	menu->addSeparator();

	/*
	 * The swatches are plain buttons in a grid, tinted by the menu's own
	 * stylesheet through the same bgColor property the rows use. OBS builds
	 * this from a .ui file; the layout is the same four-by-two of 22px
	 * squares.
	 */
	QWidget *swatches = new QWidget(menu);
	swatches->setStyleSheet("QPushButton { border: 1px solid black; margin: 0; padding: 0; }");

	QGridLayout *grid = new QGridLayout(swatches);
	grid->setContentsMargins(4, 4, 4, 4);
	grid->setSpacing(2);

	for (int swatch = 1; swatch < 9; swatch++) {
		QPushButton *button = new QPushButton(swatches);
		button->setFixedSize(22, 22);
		button->setProperty("bgColor", swatch);

		/* The current one is picked out with a heavier border, as OBS
		 * does -- there is no tick to show on a colour square. */
		if (colour.preset == swatch + 1)
			button->setStyleSheet("border: 2px solid black");

		grid->addWidget(button, (swatch - 1) / 4, (swatch - 1) % 4);

		connect(button, &QPushButton::released, this, [this, swatch, menu] {
			xray::set_item_color(owner, item, swatch + 1, std::string());
			menu->close();
			emit redrawRequested();
		});
	}

	QWidgetAction *action = new QWidgetAction(menu);
	action->setDefaultWidget(swatches);
	menu->addAction(action);
}

void XRayRow::chooseCustomColor()
{
	const xray::ItemColor colour = xray::item_color(owner, item);

	/* OBS's own starting colour when there is nothing stored yet. */
	const QString start = colour.custom.empty() ? QStringLiteral("#55FF0000")
						    : QString::fromStdString(colour.custom);

	QColorDialog::ColorDialogOptions options = QColorDialog::ShowAlphaChannel;
#ifdef __linux__
	/* Carried over from OBS: the native dialog can hang on Ubuntu here. */
	options |= QColorDialog::DontUseNativeDialog;
#endif

	/*
	 * open() rather than exec(): this runs while the context menu's own
	 * event loop is still unwinding, and a second nested loop on top of
	 * that is how rows get deleted underneath their own handlers. The
	 * dialog is parented to the row, so it goes when the row does.
	 */
	QColorDialog *dialog = new QColorDialog(this);
	dialog->setOptions(options);
	dialog->setCurrentColor(QColor(start));
	dialog->setAttribute(Qt::WA_DeleteOnClose);

	QPointer<XRayRow> self = this;
	connect(dialog, &QColorDialog::colorSelected, this, [self](const QColor &chosen) {
		if (!self || !chosen.isValid())
			return;

		xray::set_item_color(self->owner, self->item, 1, chosen.name(QColor::HexArgb).toStdString());
		emit self->redrawRequested();
	});

	dialog->open();
}

void XRayRow::buildTransitionMenu(QMenu *menu, bool show)
{
	const xray::ItemTransition current = xray::item_transition(owner, item, show);

	/*
	 * OBS names the transition source after the item it belongs to, so it
	 * is recognisable in a properties window. sourceName rather than the
	 * label, which may carry the "(recursive)" marker.
	 */
	const QString suffix = obs_module_text(show ? "Row.ShowTransition" : "Row.HideTransition");
	const std::string newName = (QString::fromStdString(sourceName) + " " + suffix).toStdString();

	QActionGroup *group = new QActionGroup(menu);
	group->setExclusive(true);

	auto addType = [&](const QString &label, const std::string &id) {
		QAction *action = menu->addAction(label);
		action->setCheckable(true);
		action->setChecked(current.id == id);
		group->addAction(action);

		connect(action, &QAction::triggered, this, [this, id, show, newName] {
			xray::set_item_transition(owner, item, show, id, newName);

			/* Matches OBS: picking a transition that has settings
			 * opens them straight away. */
			xray::open_item_transition_properties(owner, item, show);
		});
	};

	addType(obs_module_text("Row.Transition.None"), std::string());

	for (const xray::SourceType &type : xray::transition_types())
		addType(QString::fromStdString(type.name), type.id);

	menu->addSeparator();

	/* Same bounds as OBS's spin box, and the same live application: the
	 * duration lands as it is typed rather than on closing the menu. */
	QSpinBox *duration = new QSpinBox(menu);
	duration->setMinimum(50);
	duration->setMaximum(20000);
	duration->setSingleStep(50);
	duration->setSuffix(" ms");
	duration->setValue(current.duration_ms);

	connect(duration, &QSpinBox::valueChanged, this,
		[this, show](int ms) { xray::set_item_transition_duration(owner, item, show, ms); });

	QWidgetAction *durationAction = new QWidgetAction(menu);
	durationAction->setDefaultWidget(duration);
	menu->addAction(durationAction);

	if (current.configurable) {
		menu->addSeparator();

		QAction *properties = menu->addAction(obs_module_text("Row.Properties"));
		connect(properties, &QAction::triggered, this,
			[this, show] { xray::open_item_transition_properties(owner, item, show); });
	}

	menu->addSeparator();

	QAction *copy = menu->addAction(obs_module_text("Row.Copy"));
	copy->setEnabled(!current.id.empty());
	connect(copy, &QAction::triggered, this, [this, show] { xray::copy_item_transition(owner, item, show); });

	QAction *paste = menu->addAction(obs_module_text("Row.Paste"));
	paste->setEnabled(xray::clipboard_has_transition());
	connect(paste, &QAction::triggered, this, [this, show] { xray::paste_item_transition(owner, item, show); });
}

void XRayRow::buildAddSourceMenu(QMenu *menu)
{
	QMenu *deprecated = nullptr;

	/*
	 * Inserted in name order as it goes, which is how OBS keeps this list
	 * sorted -- libobs enumerates input types in registration order, not
	 * alphabetically.
	 */
	auto entry = [this](QMenu *into, const xray::SourceType &type) {
		QAction *action = new QAction(QString::fromStdString(type.name), into);
		action->setIcon(source_icon(type.id));
		connect(action, &QAction::triggered, this, [this, type] { openAddSource(type); });

		QAction *before = nullptr;
		for (QAction *present : into->actions()) {
			if (present->text().compare(action->text(), Qt::CaseInsensitive) >= 0) {
				before = present;
				break;
			}
		}

		into->insertAction(before, action);
	};

	for (const xray::SourceType &type : xray::input_types()) {
		if (!type.deprecated) {
			entry(menu, type);
			continue;
		}

		if (!deprecated)
			deprecated = new QMenu(obs_module_text("Row.Add.Deprecated"), menu);

		entry(deprecated, type);
	}

	entry(menu, {"scene", obs_module_text("Row.Add.Scene"), false});

	/* Group sits below the separator rather than in the sorted run, which
	 * is where OBS puts it. */
	menu->addSeparator();

	const xray::SourceType groupType = {"group", obs_module_text("Row.Add.Group"), false};
	QAction *group = menu->addAction(source_icon(groupType.id), QString::fromStdString(groupType.name));
	connect(group, &QAction::triggered, this, [this, groupType] { openAddSource(groupType); });

	if (deprecated) {
		menu->addSeparator();
		menu->addMenu(deprecated);
	}
}

void XRayRow::openAddSource(const xray::SourceType &type)
{
	/*
	 * Queued, and for the usual reason: the action fires while the context
	 * menu's exec() is still unwinding, and the dialog runs an event loop
	 * of its own on top of it.
	 */
	QPointer<XRayRow> self = this;
	const xray::SourceType chosen = type;

	QMetaObject::invokeMethod(
		this,
		[self, chosen] {
			if (!self)
				return;

			XRayAddSource dialog(self->owner, chosen, self->window());
			dialog.exec();
		},
		Qt::QueuedConnection);
}

/*
 * Laid out in the same order as OBSBasic::CreateSourcePopupMenu, section for
 * section, so the two menus read the same side by side. What is missing is
 * missing for a reason: Group Items needs multi-select, Edit Transform is an
 * OBSBasic window with no frontend API, and Resize Output would change the
 * canvas from a dock that is meant to be about one nested item.
 */
void XRayRow::contextMenuEvent(QContextMenuEvent *event)
{
	if (editor) {
		QFrame::contextMenuEvent(event);
		return;
	}

	QMenu menu(this);

	const xray::ItemProperties props = xray::item_properties(owner, item);
	if (!props.found) {
		/* The item went away between the row being drawn and the click,
		 * so there is nothing for a menu to act on. */
		QFrame::contextMenuEvent(event);
		return;
	}

	/* --- add a source, into this row's own scene --- */

	buildAddSourceMenu(menu.addMenu(obs_module_text("Row.Add")));
	menu.addSeparator();

	/* --- projector and screenshot, video sources only --- */

	if (props.has_video) {
		QMenu *projector = menu.addMenu(obs_module_text("Row.Projector"));

		const QList<QScreen *> screens = QGuiApplication::screens();
		for (int i = 0; i < screens.size(); i++) {
			const QRect geometry = screens[i]->geometry();
			const QString label = QStringLiteral("%1: %2x%3 @ %4,%5")
						      .arg(i + 1)
						      .arg(geometry.width())
						      .arg(geometry.height())
						      .arg(geometry.x())
						      .arg(geometry.y());

			QAction *screen = projector->addAction(label);
			const int monitor = i;
			connect(screen, &QAction::triggered, this,
				[this, monitor] { xray::open_source_projector(sourceUuid, monitor); });
		}

		projector->addSeparator();
		QAction *window = projector->addAction(obs_module_text("Row.ProjectorWindow"));
		connect(window, &QAction::triggered, this, [this] { xray::open_source_projector(sourceUuid, -1); });

		menu.addSeparator();

		QAction *screenshot = menu.addAction(obs_module_text("Row.Screenshot"));
		connect(screenshot, &QAction::triggered, this, [this] { xray::screenshot_source(sourceUuid); });

		menu.addSeparator();
	}

	/* --- how the row is labelled, and whether it reaches the mixer --- */

	buildColorMenu(menu.addMenu(obs_module_text("Row.Color")));

	if (props.has_audio) {
		QAction *hideMixer = menu.addAction(obs_module_text("Row.HideMixer"));
		hideMixer->setCheckable(true);
		hideMixer->setChecked(xray::source_mixer_hidden(sourceUuid));
		connect(hideMixer, &QAction::triggered, this,
			[this](bool checked) { xray::set_source_mixer_hidden(sourceUuid, checked); });
	}

	menu.addSeparator();

	/* --- how the item is rendered --- */

	if (props.has_video) {
		addChoiceMenu<xray::ScaleFilter>(menu, obs_module_text("Row.ScaleFiltering"),
						 {{obs_module_text("Row.Scale.Disable"), xray::ScaleFilter::Disable},
						  {obs_module_text("Row.Scale.Point"), xray::ScaleFilter::Point},
						  {obs_module_text("Row.Scale.Bilinear"), xray::ScaleFilter::Bilinear},
						  {obs_module_text("Row.Scale.Bicubic"), xray::ScaleFilter::Bicubic},
						  {obs_module_text("Row.Scale.Lanczos"), xray::ScaleFilter::Lanczos},
						  {obs_module_text("Row.Scale.Area"), xray::ScaleFilter::Area}},
						 props.scale, [this](xray::ScaleFilter v) {
							 xray::set_scale_filter(owner, item, v);
						 });

		addChoiceMenu<xray::BlendingMode>(
			menu, obs_module_text("Row.BlendingMode"),
			{{obs_module_text("Row.Blend.Normal"), xray::BlendingMode::Normal},
			 {obs_module_text("Row.Blend.Additive"), xray::BlendingMode::Additive},
			 {obs_module_text("Row.Blend.Subtract"), xray::BlendingMode::Subtract},
			 {obs_module_text("Row.Blend.Screen"), xray::BlendingMode::Screen},
			 {obs_module_text("Row.Blend.Multiply"), xray::BlendingMode::Multiply},
			 {obs_module_text("Row.Blend.Lighten"), xray::BlendingMode::Lighten},
			 {obs_module_text("Row.Blend.Darken"), xray::BlendingMode::Darken}},
			props.blending_mode, [this](xray::BlendingMode v) { xray::set_blending_mode(owner, item, v); });

		addChoiceMenu<xray::BlendingMethod>(
			menu, obs_module_text("Row.BlendingMethod"),
			{{obs_module_text("Row.Method.Default"), xray::BlendingMethod::Default},
			 {obs_module_text("Row.Method.SrgbOff"), xray::BlendingMethod::SrgbOff}},
			props.blending_method,
			[this](xray::BlendingMethod v) { xray::set_blending_method(owner, item, v); });

		/* --- deinterlacing, async video only, matching OBS --- */

		if (props.is_async_video) {
			/* One submenu holding both runs, split by a separator,
			 * exactly as AddDeinterlacingMenu builds it. */
			QMenu *deinterlacing = menu.addMenu(obs_module_text("Row.Deinterlacing"));

			addChoiceEntries<xray::DeinterlaceMode>(
				deinterlacing,
				{{obs_module_text("Row.Deint.Disable"), xray::DeinterlaceMode::Disable},
				 {obs_module_text("Row.Deint.Discard"), xray::DeinterlaceMode::Discard},
				 {obs_module_text("Row.Deint.Retro"), xray::DeinterlaceMode::Retro},
				 {obs_module_text("Row.Deint.Blend"), xray::DeinterlaceMode::Blend},
				 {obs_module_text("Row.Deint.Blend2x"), xray::DeinterlaceMode::Blend2x},
				 {obs_module_text("Row.Deint.Linear"), xray::DeinterlaceMode::Linear},
				 {obs_module_text("Row.Deint.Linear2x"), xray::DeinterlaceMode::Linear2x},
				 {obs_module_text("Row.Deint.Yadif"), xray::DeinterlaceMode::Yadif},
				 {obs_module_text("Row.Deint.Yadif2x"), xray::DeinterlaceMode::Yadif2x}},
				xray::deinterlace_mode(sourceUuid),
				[this](xray::DeinterlaceMode v) { xray::set_deinterlace_mode(sourceUuid, v); });

			deinterlacing->addSeparator();

			addChoiceEntries<xray::FieldOrder>(
				deinterlacing,
				{{obs_module_text("Row.Field.Top"), xray::FieldOrder::Top},
				 {obs_module_text("Row.Field.Bottom"), xray::FieldOrder::Bottom}},
				xray::deinterlace_field_order(sourceUuid),
				[this](xray::FieldOrder v) { xray::set_deinterlace_field_order(sourceUuid, v); });
		}

		buildTransitionMenu(menu.addMenu(obs_module_text("Row.ShowTransition")), true);
		buildTransitionMenu(menu.addMenu(obs_module_text("Row.HideTransition")), false);

		menu.addSeparator();
	}

	/* --- order, which every kind of item has, and transform --- */

	QMenu *order = menu.addMenu(obs_module_text("Row.Order"));
	const std::vector<std::pair<const char *, xray::OrderMovement>> moves = {
		{"Row.Order.Up", xray::OrderMovement::Up},   {"Row.Order.Down", xray::OrderMovement::Down},
		{nullptr, xray::OrderMovement::Up}, /* separator */
		{"Row.Order.Top", xray::OrderMovement::Top}, {"Row.Order.Bottom", xray::OrderMovement::Bottom},
	};
	for (const auto &move : moves) {
		if (!move.first) {
			order->addSeparator();
			continue;
		}

		QAction *action = order->addAction(obs_module_text(move.first));
		const xray::OrderMovement movement = move.second;
		connect(action, &QAction::triggered, this,
			[this, movement] { xray::set_order(owner, item, movement); });
	}

	if (props.has_video) {
		QMenu *transform = menu.addMenu(obs_module_text("Row.Transform"));

		const std::vector<std::pair<const char *, xray::TransformOp>> ops = {
			{"Row.Transform.Reset", xray::TransformOp::Reset},
			{nullptr, xray::TransformOp::Reset}, /* separator */
			{"Row.Transform.Rotate90CW", xray::TransformOp::Rotate90CW},
			{"Row.Transform.Rotate90CCW", xray::TransformOp::Rotate90CCW},
			{"Row.Transform.Rotate180", xray::TransformOp::Rotate180},
			{nullptr, xray::TransformOp::Reset},
			{"Row.Transform.FlipH", xray::TransformOp::FlipHorizontal},
			{"Row.Transform.FlipV", xray::TransformOp::FlipVertical},
			{nullptr, xray::TransformOp::Reset},
			{"Row.Transform.FitToScreen", xray::TransformOp::FitToScreen},
			{"Row.Transform.StretchToScreen", xray::TransformOp::StretchToScreen},
			{"Row.Transform.CenterToScreen", xray::TransformOp::CenterToScreen},
			{"Row.Transform.CenterVertically", xray::TransformOp::CenterVertically},
			{"Row.Transform.CenterHorizontally", xray::TransformOp::CenterHorizontally},
		};

		for (const auto &entry : ops) {
			if (!entry.first) {
				transform->addSeparator();
				continue;
			}

			QAction *action = transform->addAction(obs_module_text(entry.first));
			const xray::TransformOp op = entry.second;
			connect(action, &QAction::triggered, this,
				[this, op] { xray::apply_transform(owner, item, op); });
		}
	}

	menu.addSeparator();

	if (props.is_group) {
		QAction *ungroup = menu.addAction(obs_module_text("Row.Ungroup"));
		connect(ungroup, &QAction::triggered, this, [this] { xray::ungroup_item(owner, item); });
		menu.addSeparator();
	}

	/* --- clipboard, this dock's own --- */

	QAction *copy = menu.addAction(obs_module_text("Row.Copy"));
	connect(copy, &QAction::triggered, this, [this] { xray::copy_item(owner, item); });

	QAction *pasteRef = menu.addAction(obs_module_text("Row.PasteReference"));
	pasteRef->setEnabled(xray::clipboard_has_item());
	connect(pasteRef, &QAction::triggered, this, [this] { xray::paste_item(owner, false); });

	QAction *pasteDup = menu.addAction(obs_module_text("Row.PasteDuplicate"));
	pasteDup->setEnabled(xray::clipboard_has_item());
	connect(pasteDup, &QAction::triggered, this, [this] { xray::paste_item(owner, true); });

	menu.addSeparator();

	if (props.has_video || props.has_audio) {
		QAction *copyFilters = menu.addAction(obs_module_text("Row.CopyFilters"));
		connect(copyFilters, &QAction::triggered, this, [this] { xray::copy_filters(sourceUuid); });

		QAction *pasteFilters = menu.addAction(obs_module_text("Row.PasteFilters"));
		pasteFilters->setEnabled(xray::clipboard_has_filters());
		connect(pasteFilters, &QAction::triggered, this, [this] { xray::paste_filters(sourceUuid); });

		menu.addSeparator();
	}

	QAction *remove = menu.addAction(obs_module_text("Row.Remove"));
	connect(remove, &QAction::triggered, this, [this] { confirmRemove(); });

	QAction *rename = menu.addAction(obs_module_text("Row.Rename"));

	/*
	 * Queued. The action fires while exec() below is still unwinding, and
	 * enterEditMode() rearranges this row's layout -- doing that underneath a
	 * running menu is asking for trouble.
	 */
	connect(rename, &QAction::triggered, this,
		[this] { QMetaObject::invokeMethod(this, [this] { enterEditMode(); }, Qt::QueuedConnection); });

	menu.addSeparator();

	if (xray::source_is_interactive(sourceUuid)) {
		QAction *interact = menu.addAction(obs_module_text("Row.Interact"));
		connect(interact, &QAction::triggered, this, [this] { xray::open_source_interaction(sourceUuid); });
	}

	QAction *filters = menu.addAction(obs_module_text("Row.Filters"));
	connect(filters, &QAction::triggered, this, [this] { xray::open_source_filters(sourceUuid); });

	QAction *properties = menu.addAction(obs_module_text("Row.Properties"));
	properties->setEnabled(xray::source_is_configurable(sourceUuid));
	connect(properties, &QAction::triggered, this, [this] { xray::open_source_properties(sourceUuid); });

	/*
	 * exec() spins a nested event loop, so scene signals keep arriving and a
	 * rebuild would delete this row while its own handler is still on the
	 * stack -- the same hazard QDrag::exec() has. Hold the dock off until the
	 * menu closes.
	 */
	/* Rows are parented to the XRayList, so no back-pointer is needed. */
	XRayList *list = qobject_cast<XRayList *>(parentWidget());

	if (list)
		list->enterNestedLoop();

	menu.exec(event->globalPos());

	if (list)
		list->exitNestedLoop();
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

void XRayRow::confirmRemove()
{
	/*
	 * Queued for two reasons: the action fires while the menu's exec() is
	 * still unwinding, and a confirmed removal destroys this row. Guarded
	 * with a QPointer because a rebuild can beat the queued call to it.
	 */
	QPointer<XRayRow> self = this;

	QMetaObject::invokeMethod(
		this,
		[self] {
			if (!self)
				return;

			const QString question =
				QString(obs_module_text("Row.Remove.Confirm")).arg(self->label->text());

			if (QMessageBox::question(self, obs_module_text("Row.Remove"), question) != QMessageBox::Yes)
				return;

			/* Takes the item out of its scene. The source itself
			 * survives if it is still used anywhere else. */
			xray::remove_item(self->owner, self->item);
		},
		Qt::QueuedConnection);
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
