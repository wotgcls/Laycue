// Draws one frame of a title into a transparent image. Qt only (QImage, QPainter): the same code
// runs inside OBS and in the preview tool, so what the gallery shows is exactly what goes on air.
//
// Everything is designed on a 1920-pixel-wide canvas and scaled to the real canvas size.
#pragma once

#include "core/title-templates.hpp"
#include "render/font-set.hpp"

#include <QImage>
#include <QString>

namespace laycue::titles {

// The words in a title. Which of them a template uses is in TitleTemplate (usesLine2, usesTag).
struct TitleText {
	QString line1;
	QString line2;
	QString tag;
};

// Where the title is in its animation: filled from TitleClock (in, out) and TickerState (tickerPx).
struct FrameState {
	double in = 1.0;
	double out = 0.0;
	double tickerPx = 0.0; // how far a ticker has scrolled, in design pixels
};

struct RenderRequest {
	TitleTemplate tpl;
	TitleText text;
	FrameState frame;
	int canvasWidth = 1920;
	int canvasHeight = 1080;
	bool useAccentOverride = false; // the operator's brand colour replaces the template's accent
	Rgba accentOverride;
};

struct RenderedTitle {
	QImage image;  // premultiplied ARGB, canvasWidth wide
	int x = 0;     // where the image goes on the canvas
	int y = 0;
};

// Renders one frame. Never throws; on any problem returns an empty image.
RenderedTitle renderTitle(const FontSet &fonts, const RenderRequest &request);

// Width in design pixels of the repeating strip of a ticker (text plus the gap after it), so the
// caller can set up TickerState. Zero for templates that are not tickers.
double tickerStripWidth(const FontSet &fonts, const TitleTemplate &tpl, const TitleText &text);

// The height in design pixels of the part of the picture a template draws in.
double regionDesignHeight(const TitleTemplate &tpl, int canvasWidth, int canvasHeight);

} // namespace laycue::titles
