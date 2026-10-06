// Draws one frame of a title into a transparent image. Qt only (QImage, QPainter): the same code
// runs inside OBS and in the preview tool, so what the gallery shows is exactly what goes on air.
//
// Everything is designed on a 1920-pixel-wide canvas and scaled to the real canvas size.
#pragma once

#include "core/title-templates.hpp"
#include "render/font-set.hpp"

#include <QImage>
#include <QList>
#include <QPair>
#include <QString>

namespace laycue::titles {

// The words in a title. Which of them a template uses is in TitleTemplate (usesLine2, usesTag).
struct TitleText {
	QString line1;
	QString line2;
	QString tag;
	QString note;     // an extra line under the title (sort code...)
	QString logoPath; // a logo picture, or empty
	int logoCut = 0;  // mark/wordmark cut (see TitleContent); 0 = one piece
	bool logoStacked = false;
	bool logoMarkSecond = false;
	QList<QPair<QString, QString>> rows; // label and value pairs (bank accounts)
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
	int align = 0;      // Align: where the title sits across the picture (titles that do not span it)
	double scale = 1.0; // size, 0.5 to 1.5
	int fit = 0;        // shape width for name templates: 0 the template's own, 1 hug the text, 2 manual
	int fitWidth = 700; // the manual width, in design pixels
};

struct RenderedTitle {
	QImage image; // premultiplied ARGB, canvasWidth wide
	int x = 0;    // where the image goes on the canvas
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
