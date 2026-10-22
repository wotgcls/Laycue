// Draws a Scripture or Lyrics slide into a canvas-sized picture, from its style (slide-style.hpp).
#pragma once

#include "slides/slide-style.hpp"

#include <QImage>
#include <QRect>
#include <QSize>
#include <QString>

namespace laycue::slides {

struct SlideRequest {
	QString text;    // the verse, or the song's lines
	QString caption; // "John 3:16 (KJV)" or the song name; may be empty
	SlideStyle style;
	QImage picture; // the loaded style.picture, when the fill is Picture
	int canvasWidth = 1920;
	int canvasHeight = 1080;
};

// Registers the bundled fonts (Outfit, Barlow, Barlow Condensed, Libre Baskerville, Bebas Neue: the plugin's data/fonts)
// once. Call on the UI thread.
void loadSlideFonts();

// ALL CAPS, or a capital at the start of every line ("first letter").
QString applyCase(const QString &text, TextCase textCase);

// Where the looping Bible picture goes on a canvas of this size (an empty rectangle when the style has none), and one
// frame of it `seconds` into the loop, drawn at `opacity`. The slide itself (renderSlide) leaves that room free.
QRect decorRect(const SlideStyle &style, int canvasWidth, int canvasHeight);
QImage renderDecor(const SlideStyle &style, double seconds, const QSize &size, double opacity);

// The moving colours (Fill::Moving): where they go on the canvas (empty for any other fill), and one frame of them
// drawn at `scale` of full size (they are soft, so a quarter is plenty) at `opacity`, to be stretched over that place.
QRect backdropRect(const SlideStyle &style, int canvasWidth, int canvasHeight);
QImage renderBackdrop(const SlideStyle &style, double seconds, int canvasWidth, int canvasHeight, double scale,
		      double opacity);

// The whole canvas: transparent except the slide. An empty request gives a transparent picture. Never throws.
QImage renderSlide(const SlideRequest &request);

} // namespace laycue::slides
