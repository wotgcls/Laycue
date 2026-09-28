#include "ui/pages/new-layout-page.hpp"

#include "core/layouts.hpp"
#include "features/app.hpp"
#include "obs/layout-scene.hpp"
#include "obs/overlay-items.hpp"
#include "obs/pixel-reader.hpp"
#include "obs/scene-reader.hpp"
#include "ui/controls.hpp"
#include "ui/labels.hpp"
#include "ui/pages/helpers.hpp"
#include "ui/tokens.hpp"

#include <QAbstractButton>
#include <QGridLayout>
#include <QLabel>
#include <QLineEdit>
#include <QMenu>
#include <QPainter>
#include <QPainterPath>

#include <algorithm>

namespace laycue::ui {

using namespace page;

// One template drawn as a small picture of the screen with its parts.
class TemplateCard : public QAbstractButton {
public:
	TemplateCard(const LayoutTemplate &layout, QWidget *parent) : QAbstractButton(parent), layout_(layout)
	{
		setCheckable(true);
		setCursor(Qt::PointingHandCursor);
		setFocusPolicy(Qt::TabFocus);
		setFixedHeight(96);
		setAccessibleName(lt(layout.nameKey));
		setToolTip(lt(layout.hintKey));
	}

	QSize sizeHint() const override { return {110, 96}; }

protected:
	void enterEvent(QEnterEvent *) override
	{
		hovered_ = true;
		update();
	}
	void leaveEvent(QEvent *) override
	{
		hovered_ = false;
		update();
	}
	void paintEvent(QPaintEvent *) override
	{
		QPainter p(this);
		p.setRenderHint(QPainter::Antialiasing);
		const QRectF outer = QRectF(rect()).adjusted(1, 1, -1, -1);
		p.setPen(Qt::NoPen);
		p.setBrush(isChecked() ? color::stage : hovered_ ? color::graphiteHover : color::graphite);
		p.drawRoundedRect(outer, size::radius, size::radius);
		if (isChecked()) {
			p.setBrush(Qt::NoBrush);
			p.setPen(QPen(color::setup, 2));
			p.drawRoundedRect(outer, size::radius, size::radius);
		}

		// The screen.
		const double pad = 8;
		const double h = outer.height() - 2 * pad - 20;
		const double w = std::min(outer.width() - 2 * pad, h * 16 / 9);
		const QRectF screen(outer.x() + (outer.width() - w) / 2, outer.y() + pad, w, w * 9 / 16);
		p.setPen(Qt::NoPen);
		p.setBrush(color::recess);
		p.drawRoundedRect(screen, 4, 4);
		const double inset = 2;
		for (size_t i = 0; i < layout_.shapes.size(); ++i) {
			const SlotShape &s = layout_.shapes[i];
			const QRectF r(screen.x() + s.x * screen.width() + inset,
				       screen.y() + s.y * screen.height() + inset, s.w * screen.width() - 2 * inset,
				       s.h * screen.height() - 2 * inset);
			// The first slot is the main one; later slots are quieter.
			p.setBrush(i == 0 ? color::cream : color::textTertiary);
			p.drawRoundedRect(r, 3, 3);
		}

		p.setPen(isChecked() ? color::cream : color::textSecondary);
		p.setFont(uiFont(11, 600));
		p.drawText(QRectF(outer.x() + 4, screen.bottom() + 3, outer.width() - 8, 22), Qt::AlignCenter,
			   lt(layout_.nameKey));
	}

private:
	const LayoutTemplate &layout_;
	bool hovered_ = false;
};

NewLayoutPage::NewLayoutPage(App &app, QWidget *parent)
	: PageFrame(lt("Laycue.Layouts.Title"), lt("Laycue.Layout.NewTitle"), parent),
	  app_(app)
{
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

	body()->addWidget(label(this, lt("Laycue.Layout.Intro"), 12, 400, color::textSecondary));

	// 1. The shape.
	body()->addWidget(label(this, lt("Laycue.Layout.StepShape"), 13, 700, color::cream));
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
	body()->addWidget(shapes);

	// 2. What goes where.
	body()->addWidget(label(this, lt("Laycue.Layout.StepSources"), 13, 700, color::cream));
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
				    set ? "laycueSecondary" : "laycuePrimary");
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
