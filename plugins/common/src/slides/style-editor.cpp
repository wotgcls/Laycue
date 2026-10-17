#include "slides/style-editor.hpp"

#include "ui/controls.hpp"
#include "ui/pages/helpers.hpp"
#include "ui/text.hpp"
#include "ui/tokens.hpp"

#include <QColorDialog>
#include <QFileDialog>
#include <QFileInfo>
#include <QHBoxLayout>
#include <QLabel>
#include <QPainter>
#include <QPainterPath>
#include <QVBoxLayout>

#include <algorithm>

namespace laycue::slides {

using namespace laycue::ui;
using namespace laycue::ui::page;

namespace {

QColor toQColor(uint32_t c)
{
	return QColor(int((c >> 16) & 0xFF), int((c >> 8) & 0xFF), int(c & 0xFF));
}

uint32_t fromQColor(const QColor &c)
{
	return (uint32_t(c.red()) << 16) | (uint32_t(c.green()) << 8) | uint32_t(c.blue());
}

QWidget *row(QWidget *parent, QHBoxLayout *&layout)
{
	auto *w = new QWidget(parent);
	layout = new QHBoxLayout(w);
	layout->setContentsMargins(0, 0, 0, 0);
	layout->setSpacing(size::gapSmall);
	return w;
}

} // namespace

// ---- ColourSwatch -------------------------------------------------------------------------------------------

ColourSwatch::ColourSwatch(const QString &label, QWidget *parent) : QAbstractButton(parent)
{
	setText(label);
	setAccessibleName(label);
	setCursor(Qt::PointingHandCursor);
	setFocusPolicy(Qt::StrongFocus);
	connect(this, &QAbstractButton::clicked, this, [this]() {
		const QColor picked = QColorDialog::getColor(colour_, this, text());
		if (picked.isValid()) {
			setColour(picked);
			emit colourChosen(picked);
		}
	});
}

void ColourSwatch::setColour(const QColor &colour)
{
	colour_ = colour;
	update();
}

QSize ColourSwatch::sizeHint() const
{
	QFont f = uiFont(12, 600);
	return QSize(QFontMetrics(f).horizontalAdvance(text()) + 52, size::controlHeight);
}

void ColourSwatch::paintEvent(QPaintEvent *)
{
	QPainter p(this);
	p.setRenderHint(QPainter::Antialiasing, true);
	const QRectF r = QRectF(rect()).adjusted(0.5, 0.5, -0.5, -0.5);
	QPainterPath box;
	box.addRoundedRect(r, size::radius, size::radius);
	p.fillPath(box, underMouse() ? color::graphiteHover : color::control);
	if (hasFocus()) {
		p.setPen(QPen(color::setup, 2));
		p.drawPath(box);
	}
	const QRectF chip(r.left() + 8, r.center().y() - 9, 18, 18);
	QPainterPath chipPath;
	chipPath.addRoundedRect(chip, 5, 5);
	p.fillPath(chipPath, colour_);
	p.setPen(QPen(QColor(255, 255, 255, 60), 1));
	p.drawPath(chipPath);
	p.setFont(uiFont(12, 600));
	p.setPen(isEnabled() ? color::cream : color::textDisabled);
	p.drawText(r.adjusted(34, 0, -8, 0), Qt::AlignVCenter | Qt::AlignLeft, text());
}

// ---- StyleEditor --------------------------------------------------------------------------------------------

StyleEditor::StyleEditor(const Options &options, QWidget *parent) : QWidget(parent)
{
	auto *v = new QVBoxLayout(this);
	v->setContentsMargins(0, 0, 0, 0);
	v->setSpacing(size::gapSmall);

	v->addWidget(innerLabel(this, lt("Laycue.Slides.Where"), 11, 600, color::textTertiary));
	layout_ = new Segmented(this);
	layout_->setItems({lt("Laycue.Slides.LowerThird"), lt("Laycue.Slides.Card"), lt("Laycue.Slides.FullScreen")});
	connect(layout_, &Segmented::changed, this, [this](int index) {
		style_.layout = index;
		emitChanged();
	});
	v->addWidget(layout_);

	v->addWidget(innerLabel(this, lt("Laycue.Slides.Behind"), 11, 600, color::textTertiary));
	fill_ = new Segmented(this);
	fill_->setItems({lt("Laycue.Slides.FillNone"), lt("Laycue.Slides.FillColour"), lt("Laycue.Slides.FillFaded"),
			 lt("Laycue.Slides.FillGradient"), lt("Laycue.Slides.FillPicture")});
	connect(fill_, &Segmented::changed, this, [this](int index) {
		if (index == int(Fill::Picture) && style_.picture.empty()) {
			choosePicture(); // a picture fill needs a picture
			return;
		}
		style_.fill = index;
		emitChanged();
	});
	v->addWidget(fill_);

	QHBoxLayout *colours = nullptr;
	v->addWidget(row(this, colours));
	colour1_ = new ColourSwatch(lt("Laycue.Slides.Colour"), this);
	connect(colour1_, &ColourSwatch::colourChosen, this, [this](const QColor &c) {
		style_.color1 = fromQColor(c);
		emitChanged();
	});
	colours->addWidget(colour1_, 1);
	colour2_ = new ColourSwatch(lt("Laycue.Slides.ColourTo"), this);
	connect(colour2_, &ColourSwatch::colourChosen, this, [this](const QColor &c) {
		style_.color2 = fromQColor(c);
		emitChanged();
	});
	colours->addWidget(colour2_, 1);

	QHBoxLayout *picture = nullptr;
	pictureRow_ = row(this, picture);
	auto *choose = button(pictureRow_, lt("Laycue.Slides.ChooseBackground"), "laycueSecondary");
	connect(choose, &QAbstractButton::clicked, this, &StyleEditor::choosePicture);
	picture->addWidget(choose);
	pictureName_ = innerLabel(pictureRow_, QString(), 11, 400, color::textTertiary);
	picture->addWidget(pictureName_, 1);
	v->addWidget(pictureRow_);

	QHBoxLayout *words = nullptr;
	v->addWidget(row(this, words));
	text_ = new ColourSwatch(lt("Laycue.Slides.Words"), this);
	connect(text_, &ColourSwatch::colourChosen, this, [this](const QColor &c) {
		style_.text = fromQColor(c);
		emitChanged();
	});
	words->addWidget(text_, 1);
	accent_ = new ColourSwatch(options.accentLabel, this);
	connect(accent_, &ColourSwatch::colourChosen, this, [this](const QColor &c) {
		style_.accent = fromQColor(c);
		emitChanged();
	});
	words->addWidget(accent_, 1);

	QHBoxLayout *sizeRow = nullptr;
	v->addWidget(row(this, sizeRow));
	sizeRow->addWidget(innerLabel(this, lt("Laycue.Slides.TextSize"), 12, 600, color::textSecondary), 1);
	auto *smaller = button(this, QStringLiteral("−"), "laycueSecondary");
	smaller->setAccessibleName(lt("Laycue.Slides.Smaller"));
	connect(smaller, &QAbstractButton::clicked, this, [this]() {
		style_.sizePercent = std::max(kMinSize, style_.sizePercent - kSizeStep);
		emitChanged();
	});
	sizeRow->addWidget(smaller);
	size_ = innerLabel(this, QString(), 13, 700, color::cream);
	size_->setAlignment(Qt::AlignCenter);
	size_->setMinimumWidth(56);
	sizeRow->addWidget(size_);
	auto *bigger = button(this, QStringLiteral("+"), "laycueSecondary");
	bigger->setAccessibleName(lt("Laycue.Slides.Bigger"));
	connect(bigger, &QAbstractButton::clicked, this, [this]() {
		style_.sizePercent = std::min(kMaxSize, style_.sizePercent + kSizeStep);
		emitChanged();
	});
	sizeRow->addWidget(bigger);

	if (options.showCase) {
		v->addWidget(innerLabel(this, lt("Laycue.Slides.Case"), 11, 600, color::textTertiary));
		case_ = new Segmented(this);
		case_->setItems({lt("Laycue.Slides.CaseAsWritten"), lt("Laycue.Slides.CaseUpper"),
				 lt("Laycue.Slides.CaseFirst")});
		connect(case_, &Segmented::changed, this, [this](int index) {
			style_.textCase = index;
			emitChanged();
		});
		v->addWidget(case_);
	}

	auto *reset = button(this, lt("Laycue.Slides.Reset"), "laycueLink");
	connect(reset, &QAbstractButton::clicked, this, &StyleEditor::resetRequested);
	v->addWidget(reset, 0, Qt::AlignLeft);
	refresh();
}

void StyleEditor::setStyle(const SlideStyle &style)
{
	style_ = style;
	refresh();
}

void StyleEditor::emitChanged()
{
	sanitize(style_);
	refresh();
	emit changed(style_);
}

void StyleEditor::choosePicture()
{
	const QString file = QFileDialog::getOpenFileName(this, lt("Laycue.Slides.ChooseBackground"), QString(),
							  lt("Laycue.Slides.PictureFiles"));
	if (file.isEmpty()) {
		refresh(); // the picture choice goes back to what it was
		return;
	}
	style_.picture = file.toStdString();
	style_.fill = int(Fill::Picture);
	emitChanged();
}

void StyleEditor::refresh()
{
	layout_->setCurrent(style_.layout);
	fill_->setCurrent(style_.fill);
	if (case_)
		case_->setCurrent(style_.textCase);
	const Fill fill = Fill(style_.fill);
	colour1_->setColour(toQColor(style_.color1));
	colour2_->setColour(toQColor(style_.color2));
	colour1_->setVisible(fill == Fill::Solid || fill == Fill::SeeThrough || fill == Fill::Gradient);
	colour2_->setVisible(fill == Fill::Gradient);
	pictureRow_->setVisible(fill == Fill::Picture);
	pictureName_->setText(QFileInfo(QString::fromStdString(style_.picture)).fileName());
	text_->setColour(toQColor(style_.text));
	accent_->setColour(toQColor(style_.accent));
	size_->setText(QStringLiteral("%1%").arg(style_.sizePercent));
}

} // namespace laycue::slides
