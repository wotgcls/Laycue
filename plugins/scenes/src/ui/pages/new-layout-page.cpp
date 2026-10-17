#include "ui/pages/new-layout-page.hpp"

#include "core/frame-style.hpp"
#include "core/layouts.hpp"
#include "features/layout-frame.hpp"
#include "features/app.hpp"
#include "obs/layout-scene.hpp"
#include "obs/overlay-items.hpp"
#include "obs/pixel-reader.hpp"
#include "obs/scene-reader.hpp"
#include "obs/scene-util.hpp"
#include "ui/controls.hpp"
#include "ui/labels.hpp"
#include "ui/pages/helpers.hpp"
#include "ui/template-card.hpp"
#include "ui/tokens.hpp"

#include <QAbstractButton>
#include <QFileDialog>
#include <QFileInfo>
#include <QGridLayout>
#include <QLabel>
#include <QLineEdit>
#include <QMenu>
#include <QPainter>
#include <QPainterPath>

#include <obs-module.h>

#include <algorithm>

namespace laycue::ui {

using namespace page;

NewLayoutPage::NewLayoutPage(App &app, int initialTemplate, QWidget *parent)
	: PageFrame(lt("Laycue.Layouts.Title"), lt("Laycue.Layout.NewTitle"), parent),
	  app_(app)
{
	const bool shapeChosen = initialTemplate >= 0 && initialTemplate < int(layoutTemplates().size());
	if (shapeChosen)
		template_ = initialTemplate;
	// Everything a slot can show: sources with a picture, and the operator's own scenes.
	for (const VideoSourceInfo &source : listVideoSources()) {
		if (source.name.rfind("Laycue · ", 0) == 0)
			continue; // Laycue's own pictures aren't something to put in a layout
		available_.push_back({source.uuid, source.name, QString()});
	}
	for (const OverlayItem &item : readAllItems()) {
		for (Choice &c : available_) {
			if (c.uuid == item.sourceUuid && c.kind.isEmpty())
				c.kind = kindDisplayName(item.kind);
		}
	}
	for (const SceneInfo &scene : readSnapshot().scenes) {
		if (scene.name.rfind("Laycue · ", 0) == 0)
			continue;
		available_.push_back({scene.uuid, scene.name, lt("Laycue.Kind.Scene")});
	}

	// 1. The shape. Opened from a shape on the Layouts tab, it is already chosen: one line says which, with a way to change it.
	if (!shapeChosen) {
		body()->addWidget(label(this, lt("Laycue.Layout.Intro"), 12, 400, color::textSecondary));
		body()->addWidget(label(this, lt("Laycue.Layout.StepShape"), 13, 700, color::cream));
	}
	auto *shapes = new QWidget(this);
	auto *grid = new QGridLayout(shapes);
	grid->setContentsMargins(size::shadowMargin, 0, size::shadowMargin, 0);
	grid->setHorizontalSpacing(size::gapSmall);
	grid->setVerticalSpacing(size::gapSmall);
	const auto &all = layoutTemplates();
	for (size_t i = 0; i < all.size(); ++i) {
		auto *card = new TemplateCard(all[i], shapes);
		connect(card, &QAbstractButton::clicked, this, [this, i]() { selectTemplate(int(i)); });
		cards_.push_back(card);
		grid->addWidget(card, int(i / 3), int(i % 3));
	}
	for (int column = 0; column < 3; ++column)
		grid->setColumnStretch(column, 1);
	if (shapeChosen) {
		auto *line = new QWidget(this);
		auto *lineLayout = new QHBoxLayout(line);
		lineLayout->setContentsMargins(0, 0, 0, 0);
		lineLayout->setSpacing(size::gapSmall);
		shapeLabel_ = label(line, QString(), 13, 700, color::cream);
		lineLayout->addWidget(shapeLabel_, 1);
		auto *change = button(line, lt("Laycue.Layout.ChangeShape"), "laycueLink");
		connect(change, &QAbstractButton::clicked, this,
			[shapes]() { shapes->setVisible(!shapes->isVisible()); });
		lineLayout->addWidget(change);
		body()->addWidget(line);
		shapes->hide();
	}
	body()->addWidget(shapes);

	// 2. What goes where.
	body()->addWidget(label(this, lt(shapeChosen ? "Laycue.Layout.StepSourcesFirst" : "Laycue.Layout.StepSources"),
				13, 700, color::cream));
	slotsHost_ = new QWidget(this);
	auto *slotsLayout = new QVBoxLayout(slotsHost_);
	slotsLayout->setContentsMargins(size::shadowMargin, 0, size::shadowMargin, 0);
	slotsLayout->setSpacing(size::gapSmall);
	body()->addWidget(slotsHost_);

	// 3. How it fits and what it's called.
	auto *card = new Surface(1, this);
	auto *c = new QVBoxLayout(card);
	c->setSpacing(size::gapSmall + 2);
	card->setPadding(12, 12, 12, 12);
	c->addWidget(innerLabel(card, lt("Laycue.Layout.Fit"), 12, 600, color::textSecondary));
	fitSeg_ = new Segmented(card);
	fitSeg_->setItems({lt("Laycue.Layout.FitFill"), lt("Laycue.Layout.FitWhole")});
	fitSeg_->setCurrent(1); // show everything: nothing gets cropped unless the operator asks for it
	c->addWidget(fitSeg_);
	c->addWidget(innerLabel(card, lt("Laycue.Layout.FitHint"), 11, 400, color::textTertiary));
	c->addWidget(innerLabel(card, lt("Laycue.Layout.Background"), 12, 600, color::textSecondary));
	auto *backgroundRow = new QWidget(card);
	auto *backgroundLayout = new QHBoxLayout(backgroundRow);
	backgroundLayout->setContentsMargins(0, 0, 0, 0);
	backgroundLayout->setSpacing(size::gapSmall);
	backgroundButton_ = button(backgroundRow, lt("Laycue.Layout.BackgroundChoose"), "laycueSecondary");
	connect(backgroundButton_, &QAbstractButton::clicked, this, &NewLayoutPage::chooseBackground);
	backgroundLayout->addWidget(backgroundButton_);
	backgroundRemove_ = button(backgroundRow, lt("Laycue.Layout.BackgroundRemove"), "laycueLink");
	connect(backgroundRemove_, &QAbstractButton::clicked, this, [this]() {
		background_.clear();
		showBackground();
	});
	backgroundLayout->addWidget(backgroundRemove_);
	backgroundLayout->addStretch(1);
	c->addWidget(backgroundRow);
	backgroundLabel_ = innerLabel(card, QString(), 11, 400, color::textTertiary);
	backgroundLabel_->setWordWrap(true);
	c->addWidget(backgroundLabel_);
	showBackground();
	c->addWidget(innerLabel(card, lt("Laycue.Add.Name"), 12, 600, color::textSecondary));
	nameEdit_ = new QLineEdit(card);
	nameEdit_->setAccessibleName(lt("Laycue.Add.Name"));
	connect(nameEdit_, &QLineEdit::textChanged, this, [this]() {
		if (!settingName_)
			nameEdited_ = true;
		errorLabel_->hide();
		updateCreate();
	});
	c->addWidget(nameEdit_);
	errorLabel_ = innerLabel(card, QString(), 12, 500, color::overlay);
	errorLabel_->hide();
	c->addWidget(errorLabel_);
	body()->addWidget(card);
	body()->addWidget(label(this, lt("Laycue.Layout.WhatHappens"), 12, 400, color::textTertiary));

	auto *footer = new QWidget(this);
	auto *row = new QHBoxLayout(footer);
	row->setContentsMargins(size::shadowMargin, 4, size::shadowMargin, 4);
	createButton_ = button(footer, lt("Laycue.Layout.Create"), "laycuePrimary");
	createButton_->setMinimumHeight(size::touch);
	connect(createButton_, &QAbstractButton::clicked, this, &NewLayoutPage::create);
	row->addWidget(createButton_, 1);
	setFooter(footer);

	selectTemplate(template_);
}

void NewLayoutPage::selectTemplate(int index)
{
	const auto &all = layoutTemplates();
	if (index < 0 || index >= int(all.size()))
		return;
	template_ = index;
	if (shapeLabel_)
		shapeLabel_->setText(lt("Laycue.Layout.ShapeIs").arg(lt(all[index].nameKey)));
	for (size_t i = 0; i < cards_.size(); ++i)
		cards_[i]->setChecked(int(i) == index);
	chosen_.resize(all[index].shapes.size()); // keeps what was chosen for the slots that still exist
	if (!nameEdited_) {
		settingName_ = true;
		nameEdit_->setText(lt(all[index].nameKey));
		settingName_ = false;
	}
	rebuildSlotRows();
	updateCreate();
}

void NewLayoutPage::rebuildSlotRows()
{
	clear(slotsHost_->layout());
	auto *layout = static_cast<QVBoxLayout *>(slotsHost_->layout());
	const LayoutTemplate &shape = layoutTemplates()[template_];
	for (size_t i = 0; i < shape.shapes.size(); ++i) {
		auto *row = new QWidget(slotsHost_);
		auto *h = new QHBoxLayout(row);
		h->setContentsMargins(0, 0, 0, 0);
		h->setSpacing(size::gapSmall);
		h->addWidget(innerLabel(row, lt(shape.shapes[i].nameKey), 13, 600, color::cream), 2);
		const bool set = !chosen_[i].uuid.empty();
		auto *pick = button(row, set ? QString::fromStdString(chosen_[i].name) : lt("Laycue.Layout.Choose"),
				    "laycueSecondary"); // the one filled button on this page is "Make this layout"
		pick->setToolTip(lt("Laycue.Layout.ChooseTip"));
		connect(pick, &QAbstractButton::clicked, this, [this, i, pick]() { chooseFor(int(i), pick); });
		h->addWidget(pick, 3);
		layout->addWidget(row);
	}
}

void NewLayoutPage::chooseFor(int slot, QAbstractButton *anchor)
{
	QMenu menu(this);
	if (available_.empty()) {
		menu.addAction(lt("Laycue.Layout.NoSources"))->setEnabled(false);
	}
	QAction *none = menu.addAction(lt("Laycue.Layout.Empty"));
	menu.addSeparator();
	std::vector<QAction *> actions;
	for (const Choice &c : available_) {
		QString text = QString::fromStdString(c.name);
		if (!c.kind.isEmpty())
			text += QStringLiteral("  ·  ") + c.kind;
		actions.push_back(menu.addAction(text));
	}
	QAction *picked = menu.exec(anchor->mapToGlobal(QPoint(0, anchor->height())));
	if (!picked || slot < 0 || slot >= int(chosen_.size()))
		return;
	if (picked == none) {
		chosen_[slot] = Choice();
	} else {
		for (size_t i = 0; i < actions.size(); ++i) {
			if (actions[i] == picked)
				chosen_[slot] = available_[i];
		}
	}
	rebuildSlotRows();
	updateCreate();
}

void NewLayoutPage::updateCreate()
{
	const bool any = std::any_of(chosen_.begin(), chosen_.end(), [](const Choice &c) { return !c.uuid.empty(); });
	createButton_->setEnabled(any && !nameEdit_->text().trimmed().isEmpty());
}

void NewLayoutPage::showBackground()
{
	const bool has = !background_.empty();
	backgroundRemove_->setVisible(has);
	if (!has) {
		backgroundLabel_->setText(lt("Laycue.Layout.BackgroundHint"));
		return;
	}
	const QString file = QFileInfo(QString::fromStdString(background_)).fileName();
	backgroundLabel_->setText(lt(silver_ ? "Laycue.Layout.FrameSilver" : "Laycue.Layout.FrameGold").arg(file));
}

void NewLayoutPage::chooseBackground()
{
	const QString file = QFileDialog::getOpenFileName(this, lt("Laycue.Layout.BackgroundChoose"), QString(),
							  lt("Laycue.Layout.PictureFiles"));
	if (file.isEmpty())
		return;
	const RgbaImage picture = loadImageFile(file.toStdString());
	if (picture.empty()) {
		errorLabel_->setText(lt("Laycue.Layout.BackgroundFailed"));
		errorLabel_->show();
		return;
	}
	errorLabel_->hide();
	// Gold or silver, whichever stands out on this picture.
	silver_ = chooseMetal(averageColor(picture.pixels, picture.width, picture.height)) == Metal::Silver;
	background_ = file.toStdString();
	showBackground();
}

void NewLayoutPage::create()
{
	const QString name = nameEdit_->text().trimmed();
	if (name.isEmpty())
		return;
	const LayoutTemplate &shape = layoutTemplates()[template_];

	Layout layout;
	layout.id = newLayoutId(app_.config());
	layout.name = name.toStdString();
	layout.templateId = shape.id;
	layout.fit = fitSeg_->current() == 0 ? FitMode::Fill : FitMode::Whole;
	for (const Choice &c : chosen_)
		layout.sources.push_back({c.uuid, c.name});

	if (!background_.empty()) {
		// The background picture, and a frame picture Laycue draws around every slot in gold or silver.
		const Canvas canvas = readCanvas();
		const Metal metal = silver_ ? Metal::Silver : Metal::Gold;
		char *base = obs_module_config_path(("frames/" + layout.id + ".png").c_str());
		const std::string framePath = base ? base : "";
		bfree(base);
		const std::vector<RectF> rects = slotRects(shape, canvas.w, canvas.h, frameGap(canvas.w, canvas.h));
		if (!renderFramePicture(framePath, int(canvas.w), int(canvas.h), rects,
					frameThickness(canvas.w, canvas.h), metal)) {
			errorLabel_->setText(lt("Laycue.Layout.Failed"));
			errorLabel_->show();
			return;
		}
		layout.background = background_;
		layout.framePath = framePath;
		layout.metal = silver_ ? "silver" : "gold";
	}

	const LayoutBuildResult result = buildLayoutScene(layout);
	if (!result.ok) {
		errorLabel_->setText(lt("Laycue.Layout.Failed"));
		errorLabel_->show();
		return;
	}
	app_.modify([&](Config &config) { config.layouts.push_back(result.layout); });
	emit created(result.layout.sceneUuid, name);
	emit backRequested();
}

} // namespace laycue::ui
