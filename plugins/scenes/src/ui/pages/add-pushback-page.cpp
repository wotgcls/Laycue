#include "ui/pages/add-pushback-page.hpp"

#include "features/app.hpp"
#include "obs/overlay-items.hpp"
#include "obs/pixel-reader.hpp"
#include "obs/pushback-scene.hpp"
#include "obs/scene-reader.hpp"
#include "ui/controls.hpp"
#include "ui/labels.hpp"
#include "ui/pages/helpers.hpp"
#include "ui/tokens.hpp"
#include "ui/window-preview.hpp"

#include <obs-module.h>
#include <plugin-support.h>

#include <QColorDialog>
#include <QFileDialog>
#include <QFileInfo>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QMenu>
#include <QPushButton>
#include <QStackedWidget>
#include <QVBoxLayout>

#include <algorithm>

namespace laycue::ui {

namespace {

using page::button;
using page::clear;
using page::innerLabel;
using page::label;

QImage toQImage(const RgbaImage &picture)
{
	if (picture.empty())
		return QImage();
	QImage image(picture.pixels.data(), picture.width, picture.height, picture.width * 4, QImage::Format_RGBA8888);
	return image.copy(); // own the pixels
}

QString niceName(const QString &fileName)
{
	QString name = QFileInfo(fileName).completeBaseName();
	name.replace('-', ' ').replace('_', ' ');
	name = name.simplified();
	if (!name.isEmpty())
		name[0] = name[0].toUpper();
	return name;
}

QString hexOf(const Rgb &rgb)
{
	return QColor(rgb.r, rgb.g, rgb.b).name(QColor::HexRgb).toUpper();
}

bool isLaycueScene(const std::string &name)
{
	return name.rfind("Laycue · ", 0) == 0;
}

} // namespace

AddPushbackPage::AddPushbackPage(App &app, QWidget *parent)
	: PageFrame(lt("Laycue.Layouts.Title"), lt("Laycue.Add.Title"), parent),
	  app_(app)
{
	progress_ = label(this, QString(), 12, 500, color::textTertiary);
	body()->addWidget(progress_);

	steps_ = new QStackedWidget(this);
	steps_->addWidget(buildGraphicStep());
	steps_->addWidget(buildCamerasStep());
	steps_->addWidget(buildNameStep());
	steps_->addWidget(buildDoneStep());
	body()->addWidget(steps_);

	auto *footer = new QWidget(this);
	auto *row = new QHBoxLayout(footer);
	row->setContentsMargins(size::shadowMargin, 4, size::shadowMargin, 4);
	row->setSpacing(size::gapSmall);
	backStep_ = button(footer, lt("Laycue.Back"), "laycueSecondary");
	backStep_->setMinimumHeight(size::touch);
	connect(backStep_, &QAbstractButton::clicked, this, &AddPushbackPage::backClicked);
	row->addWidget(backStep_);
	primary_ = button(footer, lt("Laycue.Next"), "laycuePrimary");
	primary_->setMinimumHeight(size::touch);
	connect(primary_, &QAbstractButton::clicked, this, &AddPushbackPage::primaryClicked);
	row->addWidget(primary_, 1);
	setFooter(footer);

	showStep(Step::Graphic);
}

// ----- Step 1: the graphic -----

QWidget *AddPushbackPage::buildGraphicStep()
{
	auto *w = new QWidget(this);
	auto *v = new QVBoxLayout(w);
	v->setContentsMargins(0, 0, 0, 0);
	v->setSpacing(size::gap);

	v->addWidget(label(w, lt("Laycue.Add.GraphicIntro"), 12, 400, color::textSecondary));

	auto *pick = new QHBoxLayout();
	pick->setContentsMargins(size::shadowMargin, 0, size::shadowMargin, 0);
	pick->setSpacing(size::gapSmall);
	auto *file = button(w, lt("Laycue.Add.ChooseImage"), "laycuePrimary");
	connect(file, &QAbstractButton::clicked, this, &AddPushbackPage::chooseFile);
	pick->addWidget(file);
	auto *fromObs = button(w, lt("Laycue.Add.FromObs"), "laycueSecondary");
	connect(fromObs, &QAbstractButton::clicked, this, &AddPushbackPage::chooseSource);
	pick->addWidget(fromObs);
	pick->addStretch(1);
	v->addLayout(pick);

	originLabel_ = label(w, QString(), 11, 500, color::textTertiary);
	originLabel_->hide();
	v->addWidget(originLabel_);

	preview_ = new WindowPreview(w);
	preview_->setAccessibleName(lt("Laycue.Finder.PreviewAccessible"));
	auto *previewRow = new QHBoxLayout();
	previewRow->setContentsMargins(size::shadowMargin, 0, size::shadowMargin, 0);
	previewRow->addWidget(preview_);
	v->addLayout(previewRow);

	// What kind of window it is.
	auto *card = new Surface(1, w);
	kindCard_ = card;
	auto *c = new QVBoxLayout(card);
	c->setSpacing(size::gapSmall + 2);
	card->setPadding(12, 12, 12, 12);

	c->addWidget(innerLabel(card, lt("Laycue.Add.WindowIs"), 12, 600, color::textSecondary));
	kindSeg_ = new Segmented(card);
	kindSeg_->setItems({lt("Laycue.Finder.SeeThrough"), lt("Laycue.Add.AColour")});
	kindSeg_->setAccessibleName(lt("Laycue.Add.WindowIs"));
	kindSeg_->setToolTip(lt("Laycue.Add.KindTip"));
	connect(kindSeg_, &Segmented::changed, this, [this](int index) {
		kind_ = index == 0 ? WindowKind::SeeThrough : WindowKind::Green;
		detect(false);
	});
	c->addWidget(kindSeg_);

	colourBox_ = new QWidget(card);
	colourBox_->setMinimumHeight(size::controlHeight + 4);
	auto *colourRow = new QHBoxLayout(colourBox_);
	colourRow->setContentsMargins(0, 0, 0, 0);
	colourRow->setSpacing(size::gapSmall);
	swatch_ = new QLabel(colourBox_);
	swatch_->setFixedSize(20, 20);
	colourRow->addWidget(swatch_);
	colourText_ = innerLabel(colourBox_, QString(), 12, 500, color::cream);
	colourRow->addWidget(colourText_, 1);
	auto *pickColour = button(colourBox_, lt("Laycue.Add.PickColour"), "laycueSecondary");
	connect(pickColour, &QAbstractButton::clicked, this, &AddPushbackPage::pickColour);
	colourRow->addWidget(pickColour);
	c->addWidget(colourBox_);

	auto *resultRow = new QHBoxLayout();
	resultRow->setSpacing(size::gapSmall);
	resultDot_ = new QLabel(card);
	resultDot_->setFixedSize(8, 8);
	resultDot_->hide();
	resultRow->addWidget(resultDot_, 0, Qt::AlignTop);
	resultLabel_ = innerLabel(card, QString(), 14, 600, color::cream);
	resultRow->addWidget(resultLabel_, 1);
	c->addLayout(resultRow);
	detailLabel_ = innerLabel(card, QString(), 12, 400, color::textSecondary);
	c->addWidget(detailLabel_);

	removeBox_ = new QWidget(card);
	auto *removeRow = new QHBoxLayout(removeBox_);
	removeRow->setContentsMargins(0, 0, 0, 0);
	auto *removeText = new QVBoxLayout();
	removeText->setSpacing(1);
	removeText->addWidget(innerLabel(removeBox_, lt("Laycue.Add.RemoveColour"), 13, 500, color::cream));
	removeColourText_ = innerLabel(removeBox_, QString(), 11, 400, color::textTertiary);
	removeText->addWidget(removeColourText_);
	removeRow->addLayout(removeText, 1);
	removeColour_ = new Switch(removeBox_);
	removeColour_->setAccessibleName(lt("Laycue.Add.RemoveColour"));
	removeColour_->setOn(app_.config().settings.autoKey);
	removeRow->addWidget(removeColour_, 0, Qt::AlignVCenter);
	c->addWidget(removeBox_);
	v->addWidget(card);
	v->addStretch(1);

	// Nothing to decide until there is a picture.
	kindCard_->hide();
	colourBox_->hide();
	removeBox_->hide();
	return w;
}

void AddPushbackPage::chooseFile()
{
	const QString path = QFileDialog::getOpenFileName(this, lt("Laycue.Add.ChooseImage"), QString(),
							  lt("Laycue.Finder.ImageFilter") +
								  QStringLiteral(" (*.png *.webp *.jpg *.jpeg *.bmp)"));
	if (path.isEmpty())
		return;
	RgbaImage picture = loadImageFile(path.toStdString());
	if (picture.empty()) {
		resultLabel_->setText(lt("Laycue.Finder.CantOpen"));
		detailLabel_->setText(QFileInfo(path).fileName());
		return;
	}
	filePath_ = path;
	sourceUuid_.clear();
	sourceName_.clear();
	setPicture(std::move(picture), QFileInfo(path).fileName());
}

void AddPushbackPage::chooseSource()
{
	QMenu menu(this);
	const std::vector<VideoSourceInfo> sources = listVideoSources();
	if (sources.empty()) {
		QAction *none = menu.addAction(lt("Laycue.Add.NoSources"));
		none->setEnabled(false);
	}
	for (const VideoSourceInfo &source : sources) {
		QAction *action = menu.addAction(QString::fromStdString(source.name));
		connect(action, &QAction::triggered, this, [this, source]() {
			RgbaImage picture = captureSource(source.uuid);
			if (picture.empty()) {
				resultLabel_->setText(lt("Laycue.Finder.NoPicture"));
				detailLabel_->setText(lt("Laycue.Finder.NoPictureHint"));
				return;
			}
			filePath_.clear();
			sourceUuid_ = source.uuid;
			sourceName_ = source.name;
			setPicture(std::move(picture), QString::fromStdString(source.name));
		});
	}
	menu.exec(QCursor::pos());
}

void AddPushbackPage::setPicture(RgbaImage picture, const QString &origin)
{
	picture_ = std::move(picture);
	customKey_.reset();
	preview_->setPicture(toQImage(picture_));
	originLabel_->setText(QStringLiteral("%1 · %2 × %3").arg(origin).arg(picture_.width).arg(picture_.height));
	originLabel_->show();
	detect(true);
}

void AddPushbackPage::detect(bool automatic)
{
	if (picture_.empty())
		return;
	try {
		const ImageView view{picture_.width, picture_.height, picture_.width * 4, picture_.pixels.data()};
		if (automatic) {
			result_ = detectWindowAuto(view);
			kind_ = result_.kind;
		} else {
			DetectOptions options;
			options.kind = kind_;
			if (kind_ == WindowKind::Green)
				options.key = customKey_;
			result_ = detectWindow(view, options);
		}
	} catch (...) {
		obs_log(LOG_ERROR, "Window detection failed");
		result_ = DetectResult();
		result_.kind = kind_;
	}
	kindSeg_->setCurrent(kind_ == WindowKind::Green ? 1 : 0);
	showResult();
}

void AddPushbackPage::showResult()
{
	const Rect &w = result_.window;
	preview_->setWindow(w, result_.found, result_.found && !result_.lowConfidence);
	resultDot_->show();
	auto dot = [this](const QColor &c) {
		resultDot_->setStyleSheet(QStringLiteral("background: %1; border-radius: 4px;").arg(hex(c)));
	};

	QStringList details;
	bool usable = result_.found;
	if (!result_.found) {
		dot(color::overlay);
		resultLabel_->setText(lt("Laycue.Finder.NotFound"));
		details << lt(kind_ == WindowKind::Green ? "Laycue.Finder.NotFoundGreen"
							 : "Laycue.Finder.NotFoundSeeThrough");
	} else {
		const bool confident = !result_.lowConfidence;
		dot(confident ? color::setup : color::overlay);
		resultLabel_->setText(confident
					      ? lt("Laycue.Finder.Found")
					      : lt("Laycue.Finder.CheckEdges").arg(int(result_.fillRatio * 100 + 0.5)));
		details << lt("Laycue.Finder.Position").arg(w.width).arg(w.height).arg(w.x).arg(w.y);
		const double share = double(w.width) * w.height / (double(picture_.width) * picture_.height);
		if (share >= 0.95) {
			dot(color::overlay);
			resultLabel_->setText(lt("Laycue.Finder.AlmostAll"));
			details << lt(sourceUuid_.empty() ? "Laycue.Finder.AlmostAllFile"
							  : "Laycue.Finder.AlmostAllFilter");
			usable = false;
		}
	}
	detailLabel_->setText(details.join(QStringLiteral("\n")));

	const bool colour = kind_ == WindowKind::Green;
	kindCard_->show();
	colourBox_->setVisible(colour);
	removeColourText_->setText(
		sourceUuid_.empty() ? lt("Laycue.Add.RemoveColourFile")
				    : lt("Laycue.Add.RemoveColourSource").arg(QString::fromStdString(sourceName_)));
	removeBox_->setVisible(colour); // only a coloured window has a colour to remove
	updateColourRow();
	primary_->setEnabled(step_ != Step::Graphic || usable);
	primary_->setProperty("usable", usable);
}

void AddPushbackPage::updateColourRow()
{
	const Rgb key = customKey_ ? *customKey_ : result_.key;
	swatch_->setStyleSheet(
		QStringLiteral("background: %1; border-radius: 5px; border: 1px solid rgba(255,255,255,50);")
			.arg(hexOf(key)));
	colourText_->setText(
		(customKey_ ? lt("Laycue.Add.ColourChosen") : lt("Laycue.Add.ColourMeasured")).arg(hexOf(key)));
}

void AddPushbackPage::pickColour()
{
	const Rgb key = customKey_ ? *customKey_ : result_.key;
	const QColor chosen = QColorDialog::getColor(QColor(key.r, key.g, key.b), this, lt("Laycue.Add.PickColour"));
	if (!chosen.isValid())
		return;
	customKey_ = Rgb{uint8_t(chosen.red()), uint8_t(chosen.green()), uint8_t(chosen.blue())};
	kind_ = WindowKind::Green;
	detect(false);
}

// ----- Step 2: the cameras -----

QWidget *AddPushbackPage::buildCamerasStep()
{
	auto *w = new QWidget(this);
	auto *v = new QVBoxLayout(w);
	v->setContentsMargins(0, 0, 0, 0);
	v->setSpacing(size::gap);
	v->addWidget(label(w, lt("Laycue.Add.CamerasIntro"), 12, 400, color::textSecondary));

	auto *card = new Surface(1, w);
	auto *cardLayout = new QVBoxLayout(card);
	feedsHost_ = new QWidget(card);
	auto *rows = new QVBoxLayout(feedsHost_);
	rows->setContentsMargins(0, 0, 0, 0);
	rows->setSpacing(0);
	cardLayout->addWidget(feedsHost_);
	card->setPadding(4, 4, 4, 4);
	v->addWidget(card);

	v->addWidget(label(w, lt("Laycue.Add.Fit"), 12, 600, color::textSecondary));
	fitSeg_ = new Segmented(w);
	fitSeg_->setItems({lt("Laycue.Add.FitFill"), lt("Laycue.Add.FitWhole")});
	fitSeg_->setAccessibleName(lt("Laycue.Add.Fit"));
	auto *fitRow = new QHBoxLayout();
	fitRow->setContentsMargins(size::shadowMargin, 0, size::shadowMargin, 0);
	fitRow->addWidget(fitSeg_);
	v->addLayout(fitRow);
	v->addWidget(label(w, lt("Laycue.Add.FitHint"), 11, 400, color::textTertiary));
	v->addStretch(1);
	return w;
}

void AddPushbackPage::loadFeeds()
{
	// Keep earlier choices when the list is rebuilt.
	std::vector<std::string> keep;
	const bool firstTime = feeds_.empty();
	for (const FeedChoice &f : feeds_) {
		if (f.chosen)
			keep.push_back(f.uuid);
	}

	std::vector<FeedChoice> feeds;
	for (const VideoSourceInfo &source : listVideoSources()) {
		if (source.uuid == sourceUuid_)
			continue; // the pushback picture itself
		if (!tagOf(app_.config(), source.uuid).empty())
			continue; // a labelled overlay is not a camera
		FeedChoice f;
		f.uuid = source.uuid;
		f.name = source.name;
		f.chosen = std::find(keep.begin(), keep.end(), source.uuid) != keep.end();
		feeds.push_back(std::move(f));
	}
	// Kinds and names come from the item list (once per source).
	for (const OverlayItem &item : readAllItems()) {
		for (FeedChoice &f : feeds) {
			if (f.uuid == item.sourceUuid && f.kind.isEmpty()) {
				f.kind = kindDisplayName(item.kind);
				if (firstTime && (item.kind == SourceKind::Camera || item.kind == SourceKind::Ndi))
					f.chosen = true;
			}
		}
	}
	for (const SceneInfo &scene : readSnapshot().scenes) {
		if (isLaycueScene(scene.name))
			continue;
		FeedChoice f;
		f.uuid = scene.uuid;
		f.name = scene.name;
		f.kind = lt("Laycue.Kind.Scene");
		f.isScene = true;
		f.chosen = std::find(keep.begin(), keep.end(), scene.uuid) != keep.end();
		feeds.push_back(std::move(f));
	}
	feeds_ = std::move(feeds);
	rebuildFeedRows();
}

void AddPushbackPage::rebuildFeedRows()
{
	clear(feedsHost_->layout());
	auto *layout = static_cast<QVBoxLayout *>(feedsHost_->layout());
	if (feeds_.empty()) {
		auto *none = innerLabel(feedsHost_, lt("Laycue.Add.NoFeeds"), 12, 400, color::textSecondary);
		none->setContentsMargins(10, 8, 10, 8);
		layout->addWidget(none);
		return;
	}
	for (size_t i = 0; i < feeds_.size(); ++i) {
		auto *row = new QWidget(feedsHost_);
		auto *h = new QHBoxLayout(row);
		h->setContentsMargins(10, 6, 10, 6);
		auto *text = new QVBoxLayout();
		text->setSpacing(1);
		text->addWidget(innerLabel(row, QString::fromStdString(feeds_[i].name), 13, 600, color::cream));
		text->addWidget(innerLabel(row, feeds_[i].kind, 11, 400, color::textTertiary));
		h->addLayout(text, 1);
		auto *sw = new Switch(row);
		sw->setAccessibleName(QString::fromStdString(feeds_[i].name));
		sw->setOn(feeds_[i].chosen);
		const std::string uuid = feeds_[i].uuid;
		connect(sw, &QAbstractButton::toggled, this, [this, uuid](bool on) {
			for (FeedChoice &f : feeds_) {
				if (f.uuid == uuid)
					f.chosen = on;
			}
			primary_->setEnabled(std::any_of(feeds_.begin(), feeds_.end(),
							 [](const FeedChoice &f) { return f.chosen; }));
		});
		h->addWidget(sw);
		layout->addWidget(row);
	}
}

// ----- Step 3: the name -----

QWidget *AddPushbackPage::buildNameStep()
{
	auto *w = new QWidget(this);
	auto *v = new QVBoxLayout(w);
	v->setContentsMargins(0, 0, 0, 0);
	v->setSpacing(size::gap);
	v->addWidget(label(w, lt("Laycue.Add.NameIntro"), 12, 400, color::textSecondary));

	auto *card = new Surface(1, w);
	auto *c = new QVBoxLayout(card);
	c->setSpacing(size::gapSmall + 2);
	card->setPadding(12, 12, 12, 12);
	c->addWidget(innerLabel(card, lt("Laycue.Add.Name"), 12, 600, color::textSecondary));
	nameEdit_ = new QLineEdit(card);
	nameEdit_->setAccessibleName(lt("Laycue.Add.Name"));
	connect(nameEdit_, &QLineEdit::textChanged, this, [this]() {
		nameError_->hide();
		primary_->setEnabled(!nameEdit_->text().trimmed().isEmpty());
	});
	c->addWidget(nameEdit_);
	nameError_ = innerLabel(card, QString(), 12, 500, color::overlay);
	nameError_->hide();
	c->addWidget(nameError_);
	summary_ = innerLabel(card, QString(), 12, 400, color::textSecondary);
	c->addWidget(summary_);
	v->addWidget(card);

	v->addWidget(label(w, lt("Laycue.Add.WhatHappens"), 12, 400, color::textTertiary));
	v->addStretch(1);
	return w;
}

void AddPushbackPage::refreshNameStep()
{
	if (nameEdit_->text().trimmed().isEmpty()) {
		const QString base = sourceUuid_.empty() ? niceName(filePath_) : QString::fromStdString(sourceName_);
		nameEdit_->setText(base);
	}
	const int cameras =
		int(std::count_if(feeds_.begin(), feeds_.end(), [](const FeedChoice &f) { return f.chosen; }));
	QStringList lines;
	lines << lt("Laycue.Add.SummaryWindow").arg(result_.window.width).arg(result_.window.height);
	lines << lt(cameras == 1 ? "Laycue.Add.SummaryCamera" : "Laycue.Add.SummaryCameras").arg(cameras);
	lines << lt(fitSeg_->current() == 0 ? "Laycue.Add.SummaryFill" : "Laycue.Add.SummaryWhole");
	if (kind_ == WindowKind::Green && removeColour_->isChecked())
		lines << lt("Laycue.Add.SummaryKey");
	summary_->setText(lines.join(QStringLiteral("\n")));
}

// ----- Done -----

QWidget *AddPushbackPage::buildDoneStep()
{
	auto *w = new QWidget(this);
	auto *v = new QVBoxLayout(w);
	v->setContentsMargins(0, 0, 0, 0);
	v->setSpacing(size::gap);
	auto *card = new Surface(2, w);
	auto *c = new QVBoxLayout(card);
	c->setSpacing(size::gapSmall + 2);
	card->setPadding(16, 20, 16, 20);
	doneIcon_ = new QLabel(card);
	doneIcon_->setAlignment(Qt::AlignCenter);
	doneIcon_->setFixedSize(48, 48);
	doneIcon_->setFont(uiFont(24, 700));
	setDoneIcon(true);
	c->addWidget(doneIcon_, 0, Qt::AlignHCenter);
	doneTitle_ = innerLabel(card, QString(), 18, 700, color::cream);
	doneTitle_->setAlignment(Qt::AlignCenter);
	c->addWidget(doneTitle_);
	doneBody_ = innerLabel(card, QString(), 13, 400, color::textSecondary);
	doneBody_->setAlignment(Qt::AlignCenter);
	c->addWidget(doneBody_);
	v->addWidget(card);

	auto *another = button(w, lt("Laycue.Add.Another"), "laycueLink");
	connect(another, &QAbstractButton::clicked, this, [this]() {
		picture_ = RgbaImage();
		filePath_.clear();
		sourceUuid_.clear();
		sourceName_.clear();
		customKey_.reset();
		result_ = DetectResult();
		feeds_.clear();
		nameEdit_->clear();
		preview_->setPicture(QImage());
		originLabel_->hide();
		resultDot_->hide();
		resultLabel_->clear();
		detailLabel_->clear();
		kindCard_->hide();
		colourBox_->hide();
		removeBox_->hide();
		showStep(Step::Graphic);
	});
	v->addWidget(another, 0, Qt::AlignHCenter);
	v->addStretch(1);
	return w;
}

// ----- Steps -----

void AddPushbackPage::showStep(Step step)
{
	step_ = step;
	steps_->setCurrentIndex(int(step));
	const bool done = step == Step::Done;
	progress_->setVisible(!done);
	const char *names[] = {"Laycue.Add.StepGraphic", "Laycue.Add.StepCameras", "Laycue.Add.StepName"};
	if (!done)
		progress_->setText(lt("Laycue.Add.Progress").arg(int(step) + 1).arg(lt(names[int(step)])));
	backStep_->setVisible(step == Step::Cameras || step == Step::Name);

	switch (step) {
	case Step::Graphic:
		primary_->setText(lt("Laycue.Next"));
		primary_->setEnabled(result_.found && primary_->property("usable").toBool());
		break;
	case Step::Cameras:
		loadFeeds();
		primary_->setText(lt("Laycue.Next"));
		primary_->setEnabled(
			std::any_of(feeds_.begin(), feeds_.end(), [](const FeedChoice &f) { return f.chosen; }));
		break;
	case Step::Name:
		refreshNameStep();
		primary_->setText(lt("Laycue.Add.AddButton"));
		primary_->setEnabled(!nameEdit_->text().trimmed().isEmpty());
		break;
	case Step::Done:
		primary_->setText(lt("Laycue.Add.ShowMine"));
		primary_->setEnabled(true);
		break;
	}
}

void AddPushbackPage::primaryClicked()
{
	switch (step_) {
	case Step::Graphic:
		showStep(Step::Cameras);
		break;
	case Step::Cameras:
		showStep(Step::Name);
		break;
	case Step::Name:
		create();
		break;
	case Step::Done:
		emit backRequested();
		break;
	}
}

void AddPushbackPage::backClicked()
{
	if (step_ == Step::Name)
		showStep(Step::Cameras);
	else if (step_ == Step::Cameras)
		showStep(Step::Graphic);
}

void AddPushbackPage::create()
{
	const QString name = nameEdit_->text().trimmed();
	if (name.isEmpty())
		return;
	const Config &config = app_.config();
	for (const Pushback &p : config.pushbacks) {
		if (QString::fromStdString(p.name).compare(name, Qt::CaseInsensitive) == 0) {
			nameError_->setText(lt("Laycue.Add.NameTaken"));
			nameError_->show();
			return;
		}
	}

	QStringList notes;
	try {
		// 1. The picture: a new image source for a file, or the source the operator chose.
		std::string imageUuid = sourceUuid_;
		std::string imageName = sourceName_;
		SourceHold imageHold; // keeps a new picture alive until the scene holds it
		if (imageUuid.empty()) {
			imageName = "Laycue · " + name.toStdString() + " picture";
			imageHold = createImageSource(imageName, filePath_.toStdString(), imageUuid);
			if (!imageHold || imageUuid.empty()) {
				showFailure(lt("Laycue.Add.FailedImage"));
				return;
			}
		}

		// 2. Take the colour out, if asked. Laycue checks what OBS draws, so it never eats the picture.
		Pushback pushback;
		pushback.keyed = false;
		if (kind_ == WindowKind::Green && removeColour_->isChecked()) {
			const Rgb key = customKey_ ? *customKey_ : result_.key;
			const int similarity = tuneChromaKey(imageUuid, key, result_.window);
			if (similarity > 0) {
				pushback.keyed = true;
				pushback.keyColour = key;
			} else {
				notes << lt("Laycue.Add.KeyFailed");
			}
		}

		// 3. Laycue's own scene for it.
		pushback.id = newPushbackId(config);
		pushback.name = name.toStdString();
		pushback.imageUuid = imageUuid;
		pushback.imageName = imageName;
		pushback.imageWidth = picture_.width;
		pushback.imageHeight = picture_.height;
		pushback.window = result_.window;
		pushback.fit = fitSeg_->current() == 0 ? FitMode::Fill : FitMode::Whole;
		for (const FeedChoice &f : feeds_) {
			if (f.chosen)
				pushback.feeds.push_back({f.uuid, f.name});
		}
		pushback.activeFeed = pushback.feeds.empty() ? std::string() : pushback.feeds.front().uuid;

		const BuildResult built = buildPushbackScene(pushback);
		if (!built.ok) {
			showFailure(QString::fromStdString(built.error));
			return;
		}
		app_.modify([&](Config &c) { c.pushbacks.push_back(built.pushback); });
		createdSceneUuid_ = built.pushback.sceneUuid;

		setDoneIcon(true);
		doneTitle_->setText(lt("Laycue.Add.DoneTitle").arg(name));
		QString body = lt("Laycue.Add.DoneBody");
		if (!notes.isEmpty())
			body += QStringLiteral("\n\n") + notes.join(QStringLiteral("\n"));
		doneBody_->setText(body);
		showStep(Step::Done);
		emit created(createdSceneUuid_);
	} catch (const std::exception &error) {
		obs_log(LOG_ERROR, "Could not add the pushback: %s", error.what());
		showFailure(lt("Laycue.Add.FailedBody"));
	}
}

void AddPushbackPage::showFailure(const QString &message)
{
	setDoneIcon(false);
	doneTitle_->setText(lt("Laycue.Add.FailedTitle"));
	doneBody_->setText(message);
	showStep(Step::Done);
	primary_->setText(lt("Laycue.Back"));
}

void AddPushbackPage::setDoneIcon(bool success)
{
	doneIcon_->setText(success ? QStringLiteral("✓") : QStringLiteral("!"));
	const QColor fg = success ? color::setup : color::overlay;
	const QString bg = success ? QStringLiteral("#12243d") : QStringLiteral("#2a2112");
	doneIcon_->setStyleSheet(
		QStringLiteral("color: %1; background: %2; border-radius: 24px; font-size: 24px; font-weight: 700;")
			.arg(hex(fg), bg));
}

} // namespace laycue::ui
