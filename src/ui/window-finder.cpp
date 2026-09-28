#include "ui/window-finder.hpp"

#include "ui/tokens.hpp"

#include <obs-module.h>
#include <plugin-support.h>

#include <QButtonGroup>
#include <QComboBox>
#include <QFileDialog>
#include <QFileInfo>
#include <QHBoxLayout>
#include <QLabel>
#include <QPainter>
#include <QPainterPath>
#include <QPushButton>
#include <QVBoxLayout>

#include <algorithm>

namespace laycue::ui {

namespace {

constexpr double kAlmostEverything = 0.95; // share of the picture

QString text(const char *key)
{
	return QString::fromUtf8(obs_module_text(key));
}

QLabel *makeLabel(QWidget *parent, const QFont &font, const QColor &colour, const QString &content = QString())
{
	auto *label = new QLabel(content, parent);
	label->setFont(font);
	label->setStyleSheet(labelStyle(colour, font));
	return label;
}

void setDot(QLabel *dot, const QColor &colour)
{
	dot->setStyleSheet(QStringLiteral("background: %1; border-radius: 4px;").arg(hex(colour)));
}

QString finderStyleSheet()
{
	return QStringLiteral(R"(
#LaycueFinder { background: %1; }
#LaycueFinder QPushButton#laycueSecondary, #LaycueFinder QPushButton#laycueSegment {
  background: transparent; color: %2; border: 1.5px solid %3; border-radius: %4px;
  padding: 0 14px; min-height: 36px; font-size: 13px; font-weight: 600; }
#LaycueFinder QPushButton#laycueSecondary:hover, #LaycueFinder QPushButton#laycueSegment:hover { background: %5; }
#LaycueFinder QPushButton#laycueSegment:checked { background: %5; border-color: %6; }
#LaycueFinder QPushButton:focus { border: 2px solid %6; }
#LaycueFinder QPushButton#laycueSegment:disabled { color: %7; border-color: %8; background: transparent; }
#LaycueFinder QComboBox { min-height: 36px; }
)")
		       .arg(hex(color::ink), hex(color::cream), hex(color::lineStrong), QString::number(size::radius),
			    hex(color::graphite), hex(color::setup), hex(color::textDisabled), hex(color::line)) +
	       (uiFamily().isEmpty()
			? QString()
			: QStringLiteral("#LaycueFinder QLabel, #LaycueFinder QPushButton { font-family: \"%1\"; }")
				  .arg(uiFamily()));
}

QImage toQImage(const RgbaImage &picture)
{
	if (picture.empty())
		return QImage();
	QImage image(picture.pixels.data(), picture.width, picture.height, picture.width * 4, QImage::Format_RGBA8888);
	return image.copy(); // own the pixels
}

} // namespace

// ----- WindowPreview -----

WindowPreview::WindowPreview(QWidget *parent) : QWidget(parent)
{
	QSizePolicy policy(QSizePolicy::Preferred, QSizePolicy::Preferred);
	policy.setHeightForWidth(true);
	setSizePolicy(policy);
	setMinimumHeight(160);
}

void WindowPreview::setPicture(const QImage &image)
{
	image_ = image;
	found_ = false;
	updateGeometry();
	update();
}

void WindowPreview::setWindow(const Rect &window, bool found, bool confident)
{
	window_ = window;
	found_ = found;
	confident_ = confident;
	update();
}

int WindowPreview::heightForWidth(int width) const
{
	if (image_.isNull())
		return width * 9 / 16;
	return std::max(160, width * image_.height() / std::max(1, image_.width()));
}

void WindowPreview::paintEvent(QPaintEvent *)
{
	QPainter p(this);
	p.setRenderHint(QPainter::Antialiasing);
	const QRectF area = QRectF(rect()).adjusted(0.5, 0.5, -0.5, -0.5);

	QPainterPath frame;
	frame.addRoundedRect(area, size::pictureRadius, size::pictureRadius);
	p.fillPath(frame, color::graphite);
	if (image_.isNull()) {
		p.setPen(color::textTertiary);
		p.setFont(uiFont(12, 500));
		p.drawText(area.adjusted(16, 8, -16, -8), Qt::AlignCenter | Qt::TextWordWrap,
			   text("Laycue.Finder.Empty"));
		return;
	}

	// Fit the picture inside the area.
	const QSizeF fitted = QSizeF(image_.size()).scaled(area.size(), Qt::KeepAspectRatio);
	const QRectF target(area.center().x() - fitted.width() / 2, area.center().y() - fitted.height() / 2,
			    fitted.width(), fitted.height());
	const qreal scale = fitted.width() / image_.width();

	p.save();
	p.setClipPath(frame);
	// A checkerboard under see-through areas, so they read as "empty".
	const int cell = 10;
	for (int y = 0; y * cell < target.height(); ++y) {
		for (int x = 0; x * cell < target.width(); ++x) {
			const QRectF square(target.left() + x * cell, target.top() + y * cell, cell, cell);
			p.fillRect(square.intersected(target), (x + y) % 2 ? color::line : color::graphite);
		}
	}
	p.setRenderHint(QPainter::SmoothPixmapTransform);
	p.drawImage(target, image_);

	if (found_) {
		const QRectF window(target.left() + window_.x * scale, target.top() + window_.y * scale,
				    window_.width * scale, window_.height * scale);
		// Dim everything except the window, then outline it.
		QPainterPath outside;
		outside.addRect(target);
		QPainterPath hole;
		hole.addRect(window);
		QColor dim = color::ink;
		dim.setAlpha(150);
		p.fillPath(outside.subtracted(hole), dim);
		p.setPen(QPen(confident_ ? color::setup : color::overlay, 2));
		p.setBrush(Qt::NoBrush);
		p.drawRect(window.adjusted(1, 1, -1, -1));
	}
	p.restore();
}

// ----- WindowFinderDialog -----

WindowFinderDialog::WindowFinderDialog(QWidget *parent) : QDialog(parent)
{
	setWindowTitle(text("Laycue.Finder.Title"));
	setAttribute(Qt::WA_DeleteOnClose);
	resize(520, 640);

	auto *outer = new QVBoxLayout(this);
	outer->setContentsMargins(0, 0, 0, 0);
	auto *content = new QWidget(this);
	content->setObjectName("LaycueFinder");
	content->setAttribute(Qt::WA_StyledBackground, true);
	content->setStyleSheet(finderStyleSheet());
	outer->addWidget(content);

	auto *root = new QVBoxLayout(content);
	root->setContentsMargins(size::gapLarge, size::gapLarge, size::gapLarge, size::gapLarge);
	root->setSpacing(size::gap);

	root->addWidget(makeLabel(content, uiFont(18, 700), color::cream, text("Laycue.Finder.Title")));
	auto *intro = makeLabel(content, uiFont(12, 400), color::textSecondary, text("Laycue.Finder.Intro"));
	intro->setWordWrap(true);
	root->addWidget(intro);

	// Where the picture comes from: a file, or a source already in OBS.
	auto *pickRow = new QHBoxLayout();
	pickRow->setSpacing(size::gapSmall);
	auto *fileButton = new QPushButton(text("Laycue.Finder.ChooseImage"), content);
	fileButton->setObjectName("laycueSecondary");
	fileButton->setCursor(Qt::PointingHandCursor);
	connect(fileButton, &QPushButton::clicked, this, &WindowFinderDialog::chooseFile);
	pickRow->addWidget(fileButton);
	sourceBox_ = new QComboBox(content);
	sourceBox_->setAccessibleName(text("Laycue.Finder.FromObs"));
	pickRow->addWidget(sourceBox_, 1);
	root->addLayout(pickRow);
	reloadSources();
	connect(sourceBox_, &QComboBox::activated, this, &WindowFinderDialog::chooseSource);

	originLabel_ = makeLabel(content, uiFont(11, 500), color::textTertiary);
	originLabel_->setWordWrap(true);
	originLabel_->hide();
	root->addWidget(originLabel_);

	preview_ = new WindowPreview(content);
	preview_->setAccessibleName(text("Laycue.Finder.PreviewAccessible"));
	root->addWidget(preview_, 1);

	// See-through or green (chosen automatically; the operator can switch).
	auto *kindRow = new QHBoxLayout();
	kindRow->setSpacing(size::gapSmall);
	kindRow->addWidget(makeLabel(content, uiFont(12, 600), color::textSecondary, text("Laycue.Finder.WindowIs")));
	seeThroughButton_ = new QPushButton(text("Laycue.Finder.SeeThrough"), content);
	greenButton_ = new QPushButton(text("Laycue.Finder.Green"), content);
	auto *kindGroup = new QButtonGroup(this);
	for (QPushButton *button : {seeThroughButton_, greenButton_}) {
		button->setObjectName("laycueSegment");
		button->setCheckable(true);
		button->setEnabled(false);
		button->setCursor(Qt::PointingHandCursor);
		kindGroup->addButton(button);
		kindRow->addWidget(button);
	}
	seeThroughButton_->setToolTip(text("Laycue.Finder.SeeThroughTip"));
	greenButton_->setToolTip(text("Laycue.Finder.GreenTip"));
	connect(seeThroughButton_, &QPushButton::clicked, this, [this]() {
		kind_ = WindowKind::SeeThrough;
		detect(false);
	});
	connect(greenButton_, &QPushButton::clicked, this, [this]() {
		kind_ = WindowKind::Green;
		detect(false);
	});
	kindRow->addStretch(1);
	root->addLayout(kindRow);

	// Result
	auto *resultRow = new QHBoxLayout();
	resultRow->setSpacing(size::gapSmall);
	resultDot_ = new QLabel(content);
	resultDot_->setFixedSize(8, 8);
	resultDot_->hide();
	resultRow->addWidget(resultDot_, 0, Qt::AlignVCenter);
	resultLabel_ = makeLabel(content, uiFont(14, 600), color::cream);
	resultLabel_->setWordWrap(true);
	resultRow->addWidget(resultLabel_, 1);
	root->addLayout(resultRow);
	detailLabel_ = makeLabel(content, uiFont(12, 400), color::textSecondary);
	detailLabel_->setWordWrap(true);
	root->addWidget(detailLabel_);

	auto *footer = new QHBoxLayout();
	footer->addStretch(1);
	auto *closeButton = new QPushButton(text("Laycue.Finder.Close"), content);
	closeButton->setObjectName("laycueSecondary");
	closeButton->setCursor(Qt::PointingHandCursor);
	connect(closeButton, &QPushButton::clicked, this, &QDialog::close);
	footer->addWidget(closeButton);
	root->addLayout(footer);
}

void WindowFinderDialog::reloadSources()
{
	sourceBox_->clear();
	sourceBox_->addItem(text("Laycue.Finder.FromObs"), QString());
	for (const VideoSourceInfo &source : listVideoSources())
		sourceBox_->addItem(QString::fromStdString(source.name), QString::fromStdString(source.uuid));
}

void WindowFinderDialog::chooseFile()
{
	const QString path = QFileDialog::getOpenFileName(this, text("Laycue.Finder.ChooseImage"), QString(),
							  text("Laycue.Finder.ImageFilter") +
								  QStringLiteral(" (*.png *.webp *.jpg *.jpeg *.bmp)"));
	if (path.isEmpty())
		return;
	sourceBox_->setCurrentIndex(0);
	RgbaImage picture = loadImageFile(path.toStdString());
	if (picture.empty()) {
		setPicture({}, QString(), false);
		resultLabel_->setText(text("Laycue.Finder.CantOpen"));
		detailLabel_->setText(QFileInfo(path).fileName());
		return;
	}
	setPicture(std::move(picture), QFileInfo(path).fileName(), false);
}

void WindowFinderDialog::chooseSource(int index)
{
	const QString uuid = sourceBox_->itemData(index).toString();
	if (uuid.isEmpty())
		return;
	RgbaImage picture = captureSource(uuid.toStdString());
	if (picture.empty()) {
		setPicture({}, QString(), true);
		resultLabel_->setText(text("Laycue.Finder.NoPicture"));
		detailLabel_->setText(text("Laycue.Finder.NoPictureHint"));
		return;
	}
	setPicture(std::move(picture), sourceBox_->itemText(index), true);
}

void WindowFinderDialog::setPicture(RgbaImage picture, const QString &origin, bool fromSource)
{
	picture_ = std::move(picture);
	fromSource_ = fromSource;
	result_ = DetectResult();
	preview_->setPicture(toQImage(picture_));
	resultDot_->hide();
	resultLabel_->clear();
	detailLabel_->clear();
	seeThroughButton_->setEnabled(!picture_.empty());
	greenButton_->setEnabled(!picture_.empty());
	if (picture_.empty()) {
		originLabel_->hide();
		return;
	}
	originLabel_->setText(QStringLiteral("%1 · %2 × %3").arg(origin).arg(picture_.width).arg(picture_.height));
	originLabel_->show();
	detect(true);
}

void WindowFinderDialog::detect(bool automatic)
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
			result_ = detectWindow(view, options);
		}
	} catch (...) {
		obs_log(LOG_ERROR, "Window detection failed");
		result_ = DetectResult();
		result_.kind = kind_;
	}
	(kind_ == WindowKind::Green ? greenButton_ : seeThroughButton_)->setChecked(true);
	showResult();
}

void WindowFinderDialog::showResult()
{
	const Rect &w = result_.window;
	preview_->setWindow(w, result_.found, result_.found && !result_.lowConfidence);
	resultDot_->show();

	QStringList details;
	if (!result_.found) {
		setDot(resultDot_, color::overlay);
		resultLabel_->setText(text("Laycue.Finder.NotFound"));
		details << text(kind_ == WindowKind::Green ? "Laycue.Finder.NotFoundGreen"
							   : "Laycue.Finder.NotFoundSeeThrough");
	} else {
		const bool confident = !result_.lowConfidence;
		setDot(resultDot_, confident ? color::setup : color::overlay);
		resultLabel_->setText(confident ? text("Laycue.Finder.Found")
						: text("Laycue.Finder.CheckEdges")
							  .arg(static_cast<int>(result_.fillRatio * 100 + 0.5)));
		details << text("Laycue.Finder.Position").arg(w.width).arg(w.height).arg(w.x).arg(w.y);
		// A window covering nearly everything usually means a Chroma Key set too strong.
		const double share = static_cast<double>(w.width) * w.height /
				     (static_cast<double>(picture_.width) * picture_.height);
		if (share >= kAlmostEverything) {
			setDot(resultDot_, color::overlay);
			resultLabel_->setText(text("Laycue.Finder.AlmostAll"));
			details << text(fromSource_ ? "Laycue.Finder.AlmostAllFilter" : "Laycue.Finder.AlmostAllFile");
		}
		if (kind_ == WindowKind::Green) {
			const QColor key(result_.key.r, result_.key.g, result_.key.b);
			details << text("Laycue.Finder.KeyColour").arg(key.name(QColor::HexRgb).toUpper());
		}
	}
	if (fromSource_)
		details << text("Laycue.Finder.AfterFilters");
	detailLabel_->setText(details.join(QStringLiteral("\n")));
}

} // namespace laycue::ui
