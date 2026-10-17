// The on-screen picture for Laycue Scripture and Laycue Lyrics: a lower third (three looks) or a full screen
// with the operator's own background picture. Drawn with Qt into a canvas-sized picture.
#pragma once

#include <QImage>
#include <QString>

namespace laycue::slides {

enum class Look { Bar = 0, Card = 1, Clean = 2, Full = 3 };
constexpr int kLookCount = 4;

struct SlideRequest {
	QString text;    // the verse or the two lines of the song
	QString caption; // "John 3:16 (KJV)" or the song title; may be empty
	Look look = Look::Bar;
	QImage background; // for Look::Full; a dark gradient when empty
	int canvasWidth = 1920;
	int canvasHeight = 1080;
};

// Registers the bundled fonts (fonts/Outfit-*.ttf in the plugin's data) once. Call on the UI thread.
void loadSlideFonts();

// The whole canvas: transparent except the look. Never throws; an empty request gives a transparent picture.
QImage renderSlide(const SlideRequest &request);

} // namespace laycue::slides
