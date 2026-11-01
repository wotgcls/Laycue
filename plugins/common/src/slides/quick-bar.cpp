#include "slides/quick-bar.hpp"

#include "slides/style-editor.hpp"
#include "ui/controls.hpp"
#include "ui/pages/helpers.hpp"
#include "ui/text.hpp"
#include "ui/tokens.hpp"

#include <QGridLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QVBoxLayout>

#include <algorithm>

namespace laycue::slides {

using namespace laycue::ui;
using namespace laycue::ui::page;

namespace {

QLabel *caption(QWidget *parent, const QString &text)
{
	return innerLabel(parent, text, 10, 600, color::textTertiary);
}

} // namespace

QuickBar::QuickBar(bool withLines, QWidget *parent) : QWidget(parent)
{
	auto *v = new QVBoxLayout(this);
	v->setContentsMargins(0, 0, 0, 0);
	v->setSpacing(4);
	if (withLines) {
		v->addWidget(caption(this, lt("Laycue.Slides.QuickLines")));
		lines_ = new Segmented(this);
		lines_->setItems({QStringLiteral("1"), QStringLiteral("2"), QStringLiteral("3"), QStringLiteral("4"),
				  QStringLiteral("5"), QStringLiteral("6")});
		lines_->setAccessibleName(lt("Laycue.Slides.QuickLines"));
		connect(lines_, &Segmented::changed, this, [this](int index) { emit linesChosen(index + 1); });
		v->addWidget(lines_);
	}
	auto *grid = new QGridLayout();
	grid->setContentsMargins(0, 0, 0, 0);
	grid->setHorizontalSpacing(size::gapSmall);
	grid->setVerticalSpacing(4);
	grid->addWidget(caption(this, lt("Laycue.Slides.QuickMode")), 0, 0);
	grid->addWidget(caption(this, lt("Laycue.Slides.QuickBackground")), 0, 1, 1, 2);
	mode_ = new Segmented(this);
	mode_->setItems({lt("Laycue.Slides.QuickFull"), lt("Laycue.Slides.QuickLower")});
	mode_->setAccessibleName(lt("Laycue.Slides.QuickMode"));
	mode_->setToolTip(lt("Laycue.Slides.QuickModeTip"));
	connect(mode_, &Segmented::changed, this, [this](int index) { emit fullScreenChosen(index == 0); });
	grid->addWidget(mode_, 1, 0);
	background_ = new Segmented(this);
	background_->setItems({lt("Laycue.Slides.QuickOn"), lt("Laycue.Slides.QuickOff")});
	background_->setAccessibleName(lt("Laycue.Slides.QuickBackground"));
	connect(background_, &Segmented::changed, this, [this](int index) { emit backgroundChosen(index == 0); });
	grid->addWidget(background_, 1, 1);
	colour_ = new ColourSwatch(QString(), this);
	colour_->setAccessibleName(lt("Laycue.Slides.QuickColour"));
	colour_->setToolTip(lt("Laycue.Slides.QuickColour"));
	connect(colour_, &ColourSwatch::colourChosen, this,
		[this](const QColor &c) { emit colourChosen(uint32_t(c.rgb() & 0xFFFFFF)); });
	grid->addWidget(colour_, 1, 2);
	grid->setColumnStretch(0, 1);
	grid->setColumnStretch(1, 1);
	v->addLayout(grid);
}

void QuickBar::setState(const SlideStyle &style, int lines)
{
	if (lines_)
		lines_->setCurrent(std::clamp(lines, 1, 6) - 1);
	mode_->setCurrent(style.layout == int(Layout::FullScreen) ? 0 : 1);
	background_->setCurrent(style.fill == int(Fill::None) ? 1 : 0);
	colour_->setColour(
		QColor(int((style.color1 >> 16) & 0xFF), int((style.color1 >> 8) & 0xFF), int(style.color1 & 0xFF)));
}

SlideStyle withFullScreen(SlideStyle style, bool fullScreen, const SlideStyle &original)
{
	if (fullScreen) {
		style.layout = int(Layout::FullScreen);
	} else if (style.layout == int(Layout::FullScreen)) {
		style.layout = original.layout == int(Layout::FullScreen) ? int(Layout::LowerThird) : original.layout;
	}
	return style;
}

SlideStyle withBackground(SlideStyle style, bool on, const SlideStyle &original)
{
	if (!on) {
		style.fill = int(Fill::None);
	} else if (style.fill == int(Fill::None)) {
		style.fill = original.fill == int(Fill::None) ? int(Fill::Solid) : original.fill;
	}
	return style;
}

SlideStyle withColour(SlideStyle style, uint32_t rgb)
{
	style.color1 = rgb & 0xFFFFFF;
	if (style.fill == int(Fill::Gradient)) {
		// The gradient keeps its depth: it runs from the colour to a darker shade of it.
		const auto dark = [rgb](int shift) {
			return uint32_t(((rgb >> shift) & 0xFF) * 55 / 100) << shift;
		};
		style.color2 = dark(16) | dark(8) | dark(0);
	} else {
		style.color2 = style.color1;
	}
	if (style.fill == int(Fill::None))
		style.fill = int(Fill::Solid); // picking a colour means wanting it
	return style;
}

} // namespace laycue::slides
