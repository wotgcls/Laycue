// Draws Laycue Clock: eight designs, each usable as an overlay on video (no background), on a card, or on a full
// screen colour. Pure Qt painting (no OBS), so it can be tried out without OBS.
#pragma once

#include "core/clock-core.hpp"

#include <QColor>
#include <QFont>
#include <QImage>
#include <QString>

namespace laycue::clock {

constexpr int kDesignCount = 8;
enum class Design {
	BigDigits = 0,
	CornerPill = 1,
	LowerBar = 2,
	Ring = 3,
	FlipCards = 4,
	ProgressBar = 5,
	StartingSoon = 6,
	Minimal = 7
};

enum class Background { None = 0, Card = 1, FullScreen = 2 };

// The bundled fonts, loaded once from the plugin's data folder (a missing file falls back to a system font).
class ClockFonts {
public:
	explicit ClockFonts(const QString &folder);
	enum class Face { Digits, Label };
	QFont font(Face face, double pixelSize) const;
	int loadedCount() const { return loaded_; }

private:
	QString digitsFamily_;
	QString labelFamily_;
	int loaded_ = 0;
};

struct RenderRequest {
	Design design = Design::BigDigits;
	Background background = Background::None;
	QString label; // "Service starts in", or empty
	QString time;  // already formatted, "04:32" or "+00:12"
	bool countdown = true;
	Level level = Level::Normal;
	double fraction = 0.0; // countdown: share used up; count up: seconds into the minute / 60
	QColor text{255, 255, 255};
	QColor accent{255, 140, 40};
	QColor warning{255, 179, 26};
	QColor over{255, 77, 46};
	QColor panel{17, 17, 16, 220}; // card or full screen colour, with its own transparency
	int align = 1;                 // 0 left, 1 centre, 2 right
	int vertical = 1;              // 0 top, 1 middle, 2 bottom
	double sizePercent = 100.0;
	int canvasWidth = 1920;
	int canvasHeight = 1080;
};

struct Rendered {
	QImage image; // premultiplied ARGB
	int x = 0;    // where it goes on the canvas
	int y = 0;
};

Rendered renderClock(const ClockFonts &fonts, const RenderRequest &request);

// A design's own name key for the picker, "Laycue.Clock.Design.<n>".
const char *designKey(Design design);

// What a design looks like by default: whether it starts with a card, and where it sits.
struct DesignDefaults {
	Background background;
	int align;
	int vertical;
};
DesignDefaults defaultsFor(Design design);

} // namespace laycue::clock
