// Draws a Scripture or Lyrics slide into a canvas-sized picture, from its style (slide-style.hpp).
#pragma once

#include "slides/slide-style.hpp"

#include <QImage>
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

// Registers the bundled fonts (fonts/Outfit-*.ttf in the plugin's data) once. Call on the UI thread.
void loadSlideFonts();

// ALL CAPS, or a capital at the start of every line ("first letter").
QString applyCase(const QString &text, TextCase textCase);

// The whole canvas: transparent except the slide. An empty request gives a transparent picture. Never throws.
QImage renderSlide(const SlideRequest &request);

} // namespace laycue::slides
