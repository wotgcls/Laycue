#include "slides/style-editor.hpp"

#include "slides/theme-from-image.hpp"
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

// The fonts by row, and the name each shows in (its own family name, so you see what you get).
constexpr int kFontRows[3][3] = {
	{int(Font::Display), int(Font::Anton), int(Font::Compact)},
	{int(Font::Modern), int(Font::Clean), int(Font::Archivo)},
	{int(Font::Classic), int(Font::Playfair), int(Font::Cormorant)},
};
constexpr const char *kFontNames[kFontCount] = {"Outfit", "Barlow",  "Condensed", "Baskerville", "Bebas",
						"Anton",  "Archivo", "Playfair",  "Cormorant"};

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
	// Two rows that share one choice.
	layout_->setItems({lt("Laycue.Slides.LowerThird"), lt("Laycue.Slides.Card"), lt("Laycue.Slides.FullScreen"),
			   lt("Laycue.Slides.Tab")});
	connect(layout_, &Segmented::changed, this, [this](int index) {
		style_.layout = index;
		emitChanged();
	});
	v->addWidget(layout_);
	layout2_ = new Segmented(this);
	layout2_->setItems({lt("Laycue.Slides.Rule"), lt("Laycue.Slides.Quote"), lt("Laycue.Slides.Split"),
			    lt("Laycue.Slides.Torn")});
	connect(layout2_, &Segmented::changed, this, [this](int index) {
		style_.layout = 4 + index;
		emitChanged();
	});
	v->addWidget(layout2_);

	v->addWidget(innerLabel(this, lt("Laycue.Slides.Behind"), 11, 600, color::textTertiary));
	fill_ = new Segmented(this);
	fill_->setItems({lt("Laycue.Slides.FillNone"), lt("Laycue.Slides.FillColour"), lt("Laycue.Slides.FillFaded"),
			 lt("Laycue.Slides.FillGradient"), lt("Laycue.Slides.FillPicture"),
			 lt("Laycue.Slides.FillMoving")});
	connect(fill_, &Segmented::changed, this, [this](int index) {
		if ((index == int(Fill::Picture) || index == int(Fill::Moving)) && style_.picture.empty()) {
			// a picture fill needs a picture, and a moving glow takes its theme from one
			const int previous = style_.fill;
			style_.fill = index;
			choosePicture();
			if (style_.picture.empty()) { // cancelled: the choice goes back where it was
				style_.fill = previous;
				refresh();
			}
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
	colour3_ = new ColourSwatch(lt("Laycue.Slides.ColourThird"), this);
	connect(colour3_, &ColourSwatch::colourChosen, this, [this](const QColor &c) {
		style_.color3 = fromQColor(c);
		emitChanged();
	});
	colours->addWidget(colour3_, 1);

	QHBoxLayout *picture = nullptr;
	pictureRow_ = row(this, picture);
	auto *choose = button(pictureRow_, lt("Laycue.Slides.ChooseBackground"), "laycueSecondary");
	connect(choose, &QAbstractButton::clicked, this, &StyleEditor::choosePicture);
	picture->addWidget(choose);
	auto *match = button(pictureRow_, lt("Laycue.Slides.MatchPicture"), "laycueSecondary");
	connect(match, &QAbstractButton::clicked, this, [this]() {
		// The picture's colours laid onto the moving glow: the theme is read out and the gradient's
		// two colours, its third colour and the accent all follow the picture.
		applyTheme(style_, themeFromImage(QString::fromStdString(style_.picture)));
		emitChanged();
	});
	picture->addWidget(match);
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

	v->addWidget(innerLabel(this, lt("Laycue.Slides.Line"), 11, 600, color::textTertiary));
	// The thin line the templates put on the band (or a side rule): exactly as it comes, a gradient along it,
	// the gradient with a quiet light that travels along it, or no line at all.
	line_ = new Segmented(this);
	line_->setItems({lt("Laycue.Slides.LineFlat"), lt("Laycue.Slides.LineGradient"), lt("Laycue.Slides.LineSweep"),
			 lt("Laycue.Slides.LineNone")});
	connect(line_, &Segmented::changed, this, [this](int index) {
		style_.line = index;
		emitChanged();
	});
	v->addWidget(line_);

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

	v->addWidget(innerLabel(this, lt("Laycue.Slides.Align"), 11, 600, color::textTertiary));
	align_ = new Segmented(this);
	align_->setItems(
		{lt("Laycue.Slides.AlignLeft"), lt("Laycue.Slides.AlignCentre"), lt("Laycue.Slides.AlignRight")});
	connect(align_, &Segmented::changed, this, [this](int index) {
		style_.align = index;
		emitChanged();
	});
	v->addWidget(align_);

	v->addWidget(innerLabel(this, lt("Laycue.Slides.Font"), 11, 600, color::textTertiary));
	// Three rows that share one choice: bold broadcast capitals, modern sans, premium serif.
	for (int group = 0; group < 3; ++group) {
		auto *row = new Segmented(this);
		QStringList names;
		for (int f : kFontRows[group])
			names << QString::fromLatin1(kFontNames[f]);
		row->setItems(names);
		connect(row, &Segmented::changed, this, [this, group](int index) {
			style_.font = kFontRows[group][index];
			emitChanged();
		});
		v->addWidget(row);
		(group == 0 ? font_ : group == 1 ? font2_ : font3_) = row;
	}

	v->addWidget(innerLabel(this, lt("Laycue.Slides.Between"), 11, 600, color::textTertiary));
	transition_ = new Segmented(this);
	transition_->setItems({lt("Laycue.Slides.BetweenFade"), lt("Laycue.Slides.BetweenCut")});
	connect(transition_, &Segmented::changed, this, [this](int index) {
		style_.transition = index;
		emitChanged();
	});
	v->addWidget(transition_);

	decors_ = options.decors;
	if (!decors_.empty()) {
		static const char *const names[kDecorCount] = {
			"Laycue.Slides.AnimationNone",      "Laycue.Slides.AnimationOpenBible",
			"Laycue.Slides.AnimationGlowBible", "Laycue.Slides.AnimationScroll",
			"Laycue.Slides.AnimationDove",      "Laycue.Slides.AnimationCross",
			"Laycue.Slides.AnimationSunrise",   "Laycue.Slides.AnimationNotes",
			"Laycue.Slides.AnimationEqualizer", "Laycue.Slides.AnimationVinyl"};
		v->addWidget(innerLabel(this, lt("Laycue.Slides.Animation"), 11, 600, color::textTertiary));
		QStringList first, rest;
		for (size_t i = 0; i < decors_.size(); ++i)
			(i < 4 ? first : rest) << lt(names[std::clamp(decors_[i], 0, kDecorCount - 1)]);
		decor_ = new Segmented(this);
		decor_->setItems(first);
		connect(decor_, &Segmented::changed, this, [this](int index) {
			style_.decor = decors_[size_t(index)];
			emitChanged();
		});
		v->addWidget(decor_);
		if (!rest.isEmpty()) {
			decor2_ = new Segmented(this);
			decor2_->setItems(rest);
			connect(decor2_, &Segmented::changed, this, [this](int index) {
				style_.decor = decors_[size_t(index) + 4];
				emitChanged();
			});
			v->addWidget(decor2_);
		}
	}

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
	// The fill stays what the caller chose (Picture when picked from the row, or the moving glow when
	// the switcher sent the user here). The picture's colours come with it: the theme is read out and
	// laid on, so the slide and the picture were made for each other the moment it is chosen.
	applyTheme(style_, themeFromImage(file));
	emitChanged();
}

void StyleEditor::refresh()
{
	layout_->setCurrent(style_.layout < 4 ? style_.layout : -1);
	layout2_->setCurrent(style_.layout >= 4 ? style_.layout - 4 : -1);
	fill_->setCurrent(style_.fill);
	if (case_)
		case_->setCurrent(style_.textCase);
	align_->setCurrent(style_.align);
	Segmented *fontRows[3] = {font_, font2_, font3_};
	for (int group = 0; group < 3; ++group) {
		int position = -1;
		for (int i = 0; i < 3; ++i) {
			if (kFontRows[group][i] == style_.font)
				position = i;
		}
		fontRows[group]->setCurrent(position);
	}
	transition_->setCurrent(style_.transition);
	line_->setCurrent(std::clamp(style_.line, 0, kAccentLineCount - 1));
	if (decor_) {
		int position = -1;
		for (size_t i = 0; i < decors_.size(); ++i) {
			if (decors_[i] == style_.decor)
				position = int(i);
		}
		decor_->setCurrent(position >= 0 && position < 4 ? position : -1);
		if (decor2_)
			decor2_->setCurrent(position >= 4 ? position - 4 : -1);
	}
	const Fill fill = Fill(style_.fill);
	colour1_->setColour(toQColor(style_.color1));
	colour2_->setColour(toQColor(style_.color2));
	colour3_->setColour(toQColor(style_.color3));
	colour1_->setVisible(fill == Fill::Solid || fill == Fill::SeeThrough || fill == Fill::Gradient ||
			     fill == Fill::Moving);
	colour2_->setVisible(fill == Fill::Gradient || fill == Fill::Moving);
	colour3_->setVisible(fill == Fill::Moving);
	pictureRow_->setVisible(fill == Fill::Picture || fill == Fill::Moving);
	pictureName_->setText(QFileInfo(QString::fromStdString(style_.picture)).fileName());
	text_->setColour(toQColor(style_.text));
	accent_->setColour(toQColor(style_.accent));
	size_->setText(QStringLiteral("%1%").arg(style_.sizePercent));
}

} // namespace laycue::slides
