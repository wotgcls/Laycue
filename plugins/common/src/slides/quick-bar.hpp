// The quick strip above Show / Hide in Scripture and Lyrics: the few things changed during a service without opening
// the look editor. Lines on each slide (songs only), Full screen or Lower third, and the background on or off with its
// colour. It only reports choices; the panel applies them to the look in use.
#pragma once

#include "slides/slide-style.hpp"

#include <QWidget>

namespace laycue::ui {
class Segmented;
}

namespace laycue::slides {

class ColourSwatch;

class QuickBar : public QWidget {
	Q_OBJECT

public:
	explicit QuickBar(bool withLines, QWidget *parent = nullptr);

	// Shows the look in use (and the song's lines); does not emit anything.
	void setState(const SlideStyle &style, int lines);

signals:
	void linesChosen(int lines);
	void fullScreenChosen(bool fullScreen);
	void backgroundChosen(bool on);
	void colourChosen(uint32_t rgb);

private:
	ui::Segmented *lines_ = nullptr;
	ui::Segmented *mode_ = nullptr;
	ui::Segmented *background_ = nullptr;
	ColourSwatch *colour_ = nullptr;
};

// What the quick strip's choices do to a look. `original` is the template's own look, so turning the background back
// on, or leaving full screen, returns to what the template had. Pure.
SlideStyle withFullScreen(SlideStyle style, bool fullScreen, const SlideStyle &original);
SlideStyle withBackground(SlideStyle style, bool on, const SlideStyle &original);
SlideStyle withColour(SlideStyle style, uint32_t rgb);

} // namespace laycue::slides
