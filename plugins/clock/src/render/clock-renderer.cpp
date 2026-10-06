#include "render/clock-renderer.hpp"

#include <QFile>
#include <QFontDatabase>
#include <QFontMetricsF>
#include <QLinearGradient>
#include <QPainter>
#include <QPainterPath>

#include <algorithm>
#include <cmath>

namespace laycue::clock {

namespace {

constexpr double kDesignWidth = 1920.0;
constexpr double kMarginX = 110.0;
constexpr double kMarginTop = 64.0;
constexpr double kMarginBottom = 90.0;
constexpr double kShadowPad = 28.0; // room around a design for its soft shadow

QString loadFamily(const QString &path, int &loaded)
{
	if (!QFile::exists(path))
		return QString();
	const int id = QFontDatabase::addApplicationFont(path);
	if (id < 0)
		return QString();
	const QStringList families = QFontDatabase::applicationFontFamilies(id);
	if (families.isEmpty())
		return QString();
	++loaded;
	return families.first();
}

} // namespace

ClockFonts::ClockFonts(const QString &folder)
{
	digitsFamily_ = loadFamily(folder + QStringLiteral("/BarlowCondensed-Bold.ttf"), loaded_);
	labelFamily_ = loadFamily(folder + QStringLiteral("/Outfit-Bold.ttf"), loaded_);
}

QFont ClockFonts::font(Face face, double pixelSize) const
{
	QFont f(face == Face::Digits ? digitsFamily_ : labelFamily_);
	f.setPixelSize(std::max(1, int(std::lround(pixelSize))));
	f.setWeight(QFont::Bold);
	f.setStyleStrategy(QFont::PreferAntialias);
	return f;
}

const char *designKey(Design design)
{
	static const char *const keys[kDesignCount] = {"Laycue.Clock.Design.0", "Laycue.Clock.Design.1",
						       "Laycue.Clock.Design.2", "Laycue.Clock.Design.3",
						       "Laycue.Clock.Design.4", "Laycue.Clock.Design.5",
						       "Laycue.Clock.Design.6", "Laycue.Clock.Design.7"};
	return keys[std::clamp(int(design), 0, kDesignCount - 1)];
}

DesignDefaults defaultsFor(Design design)
{
	switch (design) {
	case Design::BigDigits:
		return {Background::None, 1, 1};
	case Design::CornerPill:
		return {Background::Card, 2, 0};
	case Design::LowerBar:
		return {Background::Card, 0, 2};
	case Design::Ring:
		return {Background::None, 1, 1};
	case Design::FlipCards:
		return {Background::Card, 1, 1};
	case Design::ProgressBar:
		return {Background::Card, 1, 2};
	case Design::StartingSoon:
		return {Background::None, 1, 1};
	case Design::Minimal:
		return {Background::None, 2, 0};
	}
	return {Background::None, 1, 1};
}

namespace {

// Everything a design needs while drawing, in design pixels (1920 wide).
struct Ctx {
	QPainter &p;
	const ClockFonts &fonts;
	const RenderRequest &r;
	QColor main;   // the colour of the digits and bar for the current level
	QColor accent; // the highlight for the current level
};

QColor levelColour(const RenderRequest &r, const QColor &normal)
{
	switch (r.level) {
	case Level::Warning:
		return r.warning;
	case Level::Over:
		return r.over;
	default:
		return normal;
	}
}

double cellWidth(const QFont &font)
{
	return QFontMetricsF(font).horizontalAdvance(QLatin1Char('0'));
}

// Each character sits in its own fixed cell, so the digits never jiggle as they change.
double timeWidth(const QFont &font, const QString &s)
{
	const double cell = cellWidth(font);
	double w = 0.0;
	for (const QChar c : s)
		w += c.isDigit() ? cell : (c == QLatin1Char(':') ? cell * 0.42 : cell * 0.62);
	return w;
}

void drawGlyphs(QPainter &p, const QFont &font, const QString &s, double x, double baselineY, const QColor &colour)
{
	const double cell = cellWidth(font);
	p.setFont(font);
	p.setPen(colour);
	for (const QChar c : s) {
		const double w = c.isDigit() ? cell : (c == QLatin1Char(':') ? cell * 0.42 : cell * 0.62);
		const double gw = QFontMetricsF(font).horizontalAdvance(c);
		p.drawText(QPointF(x + (w - gw) / 2.0, baselineY), QString(c));
		x += w;
	}
}

// A soft drop shadow, then the digits.
void drawTime(const Ctx &c, const QFont &font, const QString &s, double x, double top, bool shadow = true)
{
	const QFontMetricsF m(font);
	const double baseline = top + m.ascent() + (m.height() - m.ascent() - m.descent()) / 2.0;
	if (shadow) {
		drawGlyphs(c.p, font, s, x, baseline + 6, QColor(0, 0, 0, 70));
		drawGlyphs(c.p, font, s, x, baseline + 3, QColor(0, 0, 0, 110));
	}
	drawGlyphs(c.p, font, s, x, baseline, c.main);
}

double timeHeight(const QFont &font)
{
	return QFontMetricsF(font).height();
}

void drawLabel(const Ctx &c, const QString &text, const QFont &font, const QRectF &box, Qt::Alignment align,
	       const QColor &colour, bool shadow = true)
{
	if (text.isEmpty())
		return;
	c.p.setFont(font);
	if (shadow) {
		c.p.setPen(QColor(0, 0, 0, 120));
		c.p.drawText(box.translated(0, 3), int(align) | Qt::AlignVCenter, text);
	}
	c.p.setPen(colour);
	c.p.drawText(box, int(align) | Qt::AlignVCenter, text);
}

double labelWidth(const QFont &font, const QString &text)
{
	return text.isEmpty() ? 0.0 : QFontMetricsF(font).horizontalAdvance(text);
}

void card(const Ctx &c, const QRectF &box, double radius)
{
	c.p.setPen(Qt::NoPen);
	c.p.setBrush(QColor(0, 0, 0, 70));
	c.p.drawRoundedRect(box.translated(0, 8), radius, radius);
	c.p.setBrush(c.r.panel);
	c.p.drawRoundedRect(box, radius, radius);
	c.p.setBrush(Qt::NoBrush);
	c.p.setPen(QPen(QColor(255, 255, 255, 28), 2));
	c.p.drawRoundedRect(box.adjusted(1, 1, -1, -1), radius, radius);
}

struct Size {
	double w = 0.0;
	double h = 0.0;
};

// ---- The eight designs: each has a measure (its size) and a draw (inside 0..size, padded by kShadowPad) ----

// 0. Big digits: huge numbers, a small caption above. Made to sit on top of video.
Size measureBig(const Ctx &c)
{
	const QFont f = c.fonts.font(ClockFonts::Face::Digits, 300);
	const QFont fl = c.fonts.font(ClockFonts::Face::Label, 54);
	const double w = std::max(timeWidth(f, c.r.time), labelWidth(fl, c.r.label.toUpper()));
	const bool cardOn = c.r.background == Background::Card;
	return {w + (cardOn ? 120 : 0), timeHeight(f) * 0.86 + (c.r.label.isEmpty() ? 0 : 76) + (cardOn ? 80 : 0)};
}

void drawBig(const Ctx &c, const Size &s)
{
	const bool cardOn = c.r.background == Background::Card;
	if (cardOn)
		card(c, QRectF(0, 0, s.w, s.h), 40);
	const double padX = cardOn ? 60 : 0, padY = cardOn ? 40 : 0;
	const QFont f = c.fonts.font(ClockFonts::Face::Digits, 300);
	const QFont fl = c.fonts.font(ClockFonts::Face::Label, 54);
	double y = padY;
	if (!c.r.label.isEmpty()) {
		drawLabel(c, c.r.label.toUpper(), fl, QRectF(0, y, s.w, 60), Qt::AlignHCenter, c.r.text);
		y += 76;
	}
	const double tw = timeWidth(f, c.r.time);
	drawTime(c, f, c.r.time, padX + (s.w - 2 * padX - tw) / 2.0, y - timeHeight(f) * 0.1);
}

// 1. Corner pill: a pill with the caption on the left and the digits on the right.
Size measurePill(const Ctx &c)
{
	const QFont f = c.fonts.font(ClockFonts::Face::Digits, 82);
	const QFont fl = c.fonts.font(ClockFonts::Face::Label, 30);
	const double lw = labelWidth(fl, c.r.label.toUpper());
	return {44 + (lw > 0 ? lw + 36 : 0) + timeWidth(f, c.r.time) + 44, 112};
}

void drawPill(const Ctx &c, const Size &s)
{
	if (c.r.background != Background::None)
		card(c, QRectF(0, 0, s.w, s.h), s.h / 2.0);
	const QFont f = c.fonts.font(ClockFonts::Face::Digits, 82);
	const QFont fl = c.fonts.font(ClockFonts::Face::Label, 30);
	double x = 44;
	const QString label = c.r.label.toUpper();
	if (!label.isEmpty()) {
		drawLabel(c, label, fl, QRectF(x, 0, labelWidth(fl, label) + 4, s.h), Qt::AlignLeft,
			  QColor(c.r.text.red(), c.r.text.green(), c.r.text.blue(), 215));
		x += labelWidth(fl, label) + 18;
		c.p.setPen(Qt::NoPen);
		c.p.setBrush(c.accent);
		c.p.drawRoundedRect(QRectF(x, s.h * 0.26, 4, s.h * 0.48), 2, 2);
		x += 22;
	}
	drawTime(c, f, c.r.time, x, (s.h - timeHeight(f)) / 2.0 - 2);
}

// 2. Lower bar: a wide bar with a coloured cap, the caption left and the digits right.
Size measureBar(const Ctx &)
{
	return {960, 128};
}

void drawBar(const Ctx &c, const Size &s)
{
	const bool panel = c.r.background != Background::None;
	if (panel)
		card(c, QRectF(0, 0, s.w, s.h), 14);
	c.p.setPen(Qt::NoPen);
	c.p.setBrush(c.accent);
	c.p.drawRoundedRect(QRectF(0, 0, 16, s.h), 8, 8);
	const QFont f = c.fonts.font(ClockFonts::Face::Digits, 100);
	const QFont fl = c.fonts.font(ClockFonts::Face::Label, 40);
	drawLabel(c, c.r.label.toUpper(), fl, QRectF(52, 0, s.w - 340, s.h), Qt::AlignLeft, c.r.text);
	const double tw = timeWidth(f, c.r.time);
	drawTime(c, f, c.r.time, s.w - 44 - tw, (s.h - timeHeight(f)) / 2.0 - 2);
}

// 3. Ring: a circle that drains as time runs out, with the digits inside.
Size measureRing(const Ctx &)
{
	return {360, 360};
}

void drawRing(const Ctx &c, const Size &s)
{
	if (c.r.background == Background::Card)
		card(c, QRectF(0, 0, s.w, s.h), 56);
	const QPointF mid(s.w / 2.0, s.h / 2.0);
	const double radius = 144, stroke = 20;
	const QRectF ring(mid.x() - radius, mid.y() - radius, radius * 2, radius * 2);
	c.p.setBrush(Qt::NoBrush);
	c.p.setPen(QPen(QColor(0, 0, 0, 90), stroke + 6));
	c.p.drawEllipse(ring);
	c.p.setPen(QPen(QColor(c.r.text.red(), c.r.text.green(), c.r.text.blue(), 60), stroke));
	c.p.drawEllipse(ring);
	// A countdown drains from the full circle; a count up fills as the minute goes round.
	const double left = c.r.countdown ? std::clamp(1.0 - c.r.fraction, 0.0, 1.0)
					  : std::clamp(c.r.fraction, 0.0, 1.0);
	if (left > 0.002) {
		c.p.setPen(QPen(c.accent, stroke, Qt::SolidLine, Qt::RoundCap));
		c.p.drawArc(ring, 90 * 16, int(-left * 360.0 * 16.0));
	}
	const QFont f = c.fonts.font(ClockFonts::Face::Digits, 98);
	const QFont fl = c.fonts.font(ClockFonts::Face::Label, 28);
	const double tw = timeWidth(f, c.r.time);
	const double th = timeHeight(f);
	const bool hasLabel = !c.r.label.isEmpty();
	drawTime(c, f, c.r.time, mid.x() - tw / 2.0, mid.y() - th / 2.0 - (hasLabel ? 12 : 0));
	if (hasLabel) { // the caption must fit inside the ring: long ones are shortened with "..."
		const QString shown = QFontMetricsF(fl).elidedText(c.r.label.toUpper(), Qt::ElideRight, 210);
		drawLabel(c, shown, fl, QRectF(mid.x() - 105, mid.y() + th / 2.0 - 22, 210, 40), Qt::AlignHCenter,
			  c.r.text);
	}
}

// 4. Flip cards: a dark card for every digit, split across the middle like a flip clock.
Size measureFlip(const Ctx &c)
{
	int digits = 0;
	for (const QChar ch : c.r.time)
		digits += ch.isDigit() ? 1 : 0;
	int colons = 0;
	for (const QChar ch : c.r.time)
		colons += ch == QLatin1Char(':') ? 1 : 0;
	const bool hasLabel = !c.r.label.isEmpty();
	return {digits * 128.0 + (digits - 1) * 10.0 + colons * 36.0 + 8.0 * (c.r.time.size() > 0 ? 0 : 0) +
			((c.r.time.size() - digits - colons) > 0 ? (c.r.time.size() - digits - colons) * 56.0 : 0.0),
		184.0 + (hasLabel ? 74.0 : 0.0)};
}

void drawFlip(const Ctx &c, const Size &s)
{
	const QFont f = c.fonts.font(ClockFonts::Face::Digits, 150);
	const QFont fl = c.fonts.font(ClockFonts::Face::Label, 44);
	double y = 0;
	if (!c.r.label.isEmpty()) {
		drawLabel(c, c.r.label.toUpper(), fl, QRectF(0, 0, s.w, 58), Qt::AlignHCenter, c.r.text);
		y = 74;
	}
	QColor cardColour = c.r.panel;
	cardColour.setAlpha(std::max(cardColour.alpha(), 235)); // the cards are the design: always solid enough
	double x = 0;
	for (const QChar ch : c.r.time) {
		if (ch.isDigit()) {
			const QRectF box(x, y, 128, 170);
			c.p.setPen(Qt::NoPen);
			c.p.setBrush(QColor(0, 0, 0, 80));
			c.p.drawRoundedRect(box.translated(0, 7), 18, 18);
			QLinearGradient g(box.topLeft(), box.bottomLeft());
			g.setColorAt(0.0, cardColour.lighter(125));
			g.setColorAt(0.5, cardColour);
			g.setColorAt(0.5001, cardColour.darker(115));
			g.setColorAt(1.0, cardColour);
			c.p.setBrush(g);
			c.p.drawRoundedRect(box, 18, 18);
			c.p.setFont(f);
			c.p.setPen(c.main);
			const QFontMetricsF m(f);
			c.p.drawText(QPointF(box.center().x() - m.horizontalAdvance(ch) / 2.0,
					     box.center().y() + (m.ascent() - m.descent()) / 2.0),
				     QString(ch));
			c.p.setPen(QPen(QColor(0, 0, 0, 150), 3));
			c.p.drawLine(QPointF(box.left(), box.center().y()), QPointF(box.right(), box.center().y()));
			x += 128 + 10;
		} else if (ch == QLatin1Char(':')) {
			x += 4;
			c.p.setPen(Qt::NoPen);
			c.p.setBrush(c.main);
			c.p.drawEllipse(QPointF(x + 12, y + 70), 8, 8);
			c.p.drawEllipse(QPointF(x + 12, y + 100), 8, 8);
			x += 32;
		} else { // "+" or "h" etc.
			c.p.setFont(f);
			c.p.setPen(c.main);
			c.p.drawText(QPointF(x, y + 120), QString(ch));
			x += 56;
		}
	}
}

// 5. Progress bar: the digits over a bar that empties as the time runs out.
Size measureProgress(const Ctx &c)
{
	const QFont f = c.fonts.font(ClockFonts::Face::Digits, 190);
	const bool cardOn = c.r.background == Background::Card;
	return {std::max(1000.0, timeWidth(f, c.r.time) + 100),
		timeHeight(f) * 0.84 + 74 + (c.r.label.isEmpty() ? 0 : 66) + (cardOn ? 50 : 0)};
}

void drawProgress(const Ctx &c, const Size &s)
{
	const bool cardOn = c.r.background == Background::Card;
	if (cardOn)
		card(c, QRectF(0, 0, s.w, s.h), 36);
	const double padX = cardOn ? 48 : 0, padY = cardOn ? 24 : 0;
	const QFont f = c.fonts.font(ClockFonts::Face::Digits, 190);
	const QFont fl = c.fonts.font(ClockFonts::Face::Label, 42);
	double y = padY;
	if (!c.r.label.isEmpty()) {
		drawLabel(c, c.r.label.toUpper(), fl, QRectF(padX, y, s.w - 2 * padX, 56), Qt::AlignHCenter, c.r.text);
		y += 66;
	}
	const double tw = timeWidth(f, c.r.time);
	drawTime(c, f, c.r.time, (s.w - tw) / 2.0, y - timeHeight(f) * 0.08);
	y += timeHeight(f) * 0.84 + 8;
	const QRectF track(padX, y, s.w - 2 * padX, 22);
	c.p.setPen(Qt::NoPen);
	c.p.setBrush(QColor(0, 0, 0, 100));
	c.p.drawRoundedRect(track.adjusted(-2, -2, 2, 4), 13, 13);
	c.p.setBrush(QColor(c.r.text.red(), c.r.text.green(), c.r.text.blue(), 60));
	c.p.drawRoundedRect(track, 11, 11);
	const double left = c.r.countdown ? std::clamp(1.0 - c.r.fraction, 0.0, 1.0)
					  : std::clamp(c.r.fraction, 0.0, 1.0);
	if (left > 0.004) {
		c.p.setBrush(c.accent);
		c.p.drawRoundedRect(QRectF(track.left(), track.top(), std::max(22.0, track.width() * left),
					   track.height()),
				    11, 11);
	}
}

// 6. Starting soon: a big caption, a short accent line, then the digits. Made for a waiting scene.
Size measureSoon(const Ctx &c)
{
	const QFont f = c.fonts.font(ClockFonts::Face::Digits, 250);
	const QFont fl = c.fonts.font(ClockFonts::Face::Label, 92);
	const bool cardOn = c.r.background == Background::Card;
	const double w = std::max(timeWidth(f, c.r.time), labelWidth(fl, c.r.label.toUpper()));
	return {w + (cardOn ? 140 : 0), timeHeight(f) * 0.84 + (c.r.label.isEmpty() ? 0 : 138) + (cardOn ? 90 : 0)};
}

void drawSoon(const Ctx &c, const Size &s)
{
	const bool cardOn = c.r.background == Background::Card;
	if (cardOn)
		card(c, QRectF(0, 0, s.w, s.h), 44);
	const double padY = cardOn ? 45 : 0;
	const QFont f = c.fonts.font(ClockFonts::Face::Digits, 250);
	const QFont fl = c.fonts.font(ClockFonts::Face::Label, 92);
	double y = padY;
	if (!c.r.label.isEmpty()) {
		drawLabel(c, c.r.label.toUpper(), fl, QRectF(0, y, s.w, 104), Qt::AlignHCenter, c.r.text);
		y += 112;
		c.p.setPen(Qt::NoPen);
		c.p.setBrush(c.accent);
		c.p.drawRoundedRect(QRectF(s.w / 2.0 - 80, y, 160, 8), 4, 4);
		y += 26;
	}
	const double tw = timeWidth(f, c.r.time);
	drawTime(c, f, c.r.time, (s.w - tw) / 2.0, y - timeHeight(f) * 0.1);
}

// 7. Minimal: just small digits, with a tiny caption. For a corner of a busy picture.
Size measureMinimal(const Ctx &c)
{
	const QFont f = c.fonts.font(ClockFonts::Face::Digits, 104);
	const QFont fl = c.fonts.font(ClockFonts::Face::Label, 26);
	const bool cardOn = c.r.background == Background::Card;
	const double w = std::max(timeWidth(f, c.r.time), labelWidth(fl, c.r.label.toUpper()));
	return {w + (cardOn ? 64 : 0), timeHeight(f) * 0.84 + (c.r.label.isEmpty() ? 0 : 34) + (cardOn ? 36 : 0)};
}

void drawMinimal(const Ctx &c, const Size &s)
{
	const bool cardOn = c.r.background == Background::Card;
	if (cardOn)
		card(c, QRectF(0, 0, s.w, s.h), 22);
	const double padX = cardOn ? 32 : 0, padY = cardOn ? 18 : 0;
	const QFont f = c.fonts.font(ClockFonts::Face::Digits, 104);
	const QFont fl = c.fonts.font(ClockFonts::Face::Label, 26);
	double y = padY;
	if (!c.r.label.isEmpty()) {
		drawLabel(c, c.r.label.toUpper(), fl, QRectF(padX, y, s.w - 2 * padX, 30), Qt::AlignRight, c.r.text);
		y += 34;
	}
	const double tw = timeWidth(f, c.r.time);
	drawTime(c, f, c.r.time, s.w - padX - tw, y - timeHeight(f) * 0.08);
}

Size measure(const Ctx &c)
{
	switch (c.r.design) {
	case Design::BigDigits:
		return measureBig(c);
	case Design::CornerPill:
		return measurePill(c);
	case Design::LowerBar:
		return measureBar(c);
	case Design::Ring:
		return measureRing(c);
	case Design::FlipCards:
		return measureFlip(c);
	case Design::ProgressBar:
		return measureProgress(c);
	case Design::StartingSoon:
		return measureSoon(c);
	case Design::Minimal:
		return measureMinimal(c);
	}
	return measureBig(c);
}

void draw(const Ctx &c, const Size &s)
{
	switch (c.r.design) {
	case Design::BigDigits:
		return drawBig(c, s);
	case Design::CornerPill:
		return drawPill(c, s);
	case Design::LowerBar:
		return drawBar(c, s);
	case Design::Ring:
		return drawRing(c, s);
	case Design::FlipCards:
		return drawFlip(c, s);
	case Design::ProgressBar:
		return drawProgress(c, s);
	case Design::StartingSoon:
		return drawSoon(c, s);
	case Design::Minimal:
		return drawMinimal(c, s);
	}
}

} // namespace

Rendered renderClock(const ClockFonts &fonts, const RenderRequest &request)
{
	Rendered out;
	if (request.canvasWidth <= 0 || request.canvasHeight <= 0)
		return out;
	const double scale =
		double(request.canvasWidth) / kDesignWidth * std::clamp(request.sizePercent, 30.0, 200.0) / 100.0;
	const double canvasScale = double(request.canvasWidth) / kDesignWidth;
	const double designH = double(request.canvasHeight) / canvasScale; // the canvas height in design pixels

	// Measure first (no painting yet), with a throwaway painter on a 1x1 image.
	QImage probe(1, 1, QImage::Format_ARGB32_Premultiplied);
	QPainter probePainter(&probe);
	RenderRequest r = request;
	const QColor main = levelColour(r, r.text);
	const QColor accent = levelColour(r, r.accent);
	Ctx measureCtx{probePainter, fonts, r, main, accent};
	const Size size = measure(measureCtx);
	probePainter.end();

	const double userScale = std::clamp(request.sizePercent, 30.0, 200.0) / 100.0;
	const double w = size.w * userScale, h = size.h * userScale; // in design pixels on the canvas
	double x = r.align == 0 ? kMarginX : r.align == 2 ? kDesignWidth - kMarginX - w : (kDesignWidth - w) / 2.0;
	double y = r.vertical == 0 ? kMarginTop : r.vertical == 2 ? designH - kMarginBottom - h : (designH - h) / 2.0;
	x = std::clamp(x, 0.0, std::max(0.0, kDesignWidth - w));
	y = std::clamp(y, 0.0, std::max(0.0, designH - h));
	(void)scale;

	if (r.background == Background::FullScreen) {
		QImage image(request.canvasWidth, request.canvasHeight, QImage::Format_ARGB32_Premultiplied);
		image.fill(Qt::transparent);
		QPainter p(&image);
		p.setRenderHint(QPainter::Antialiasing, true);
		p.setRenderHint(QPainter::TextAntialiasing, true);
		p.fillRect(image.rect(), r.panel);
		p.scale(canvasScale, canvasScale);
		p.translate(x, y);
		p.scale(userScale, userScale);
		RenderRequest drawReq = r;
		drawReq.background = Background::None; // the screen is the background, no card on top
		Ctx c{p, fonts, drawReq, main, accent};
		draw(c, size);
		p.end();
		out.image = std::move(image);
		return out;
	}

	const int pad = int(std::ceil(kShadowPad * canvasScale * userScale));
	const int pw = std::max(1, int(std::ceil(w * canvasScale))) + 2 * pad;
	const int ph = std::max(1, int(std::ceil(h * canvasScale))) + 2 * pad;
	QImage image(pw, ph, QImage::Format_ARGB32_Premultiplied);
	image.fill(Qt::transparent);
	QPainter p(&image);
	p.setRenderHint(QPainter::Antialiasing, true);
	p.setRenderHint(QPainter::TextAntialiasing, true);
	p.translate(pad, pad);
	p.scale(canvasScale * userScale, canvasScale * userScale);
	Ctx c{p, fonts, r, main, accent};
	draw(c, size);
	p.end();
	out.image = std::move(image);
	out.x = int(std::lround(x * canvasScale)) - pad;
	out.y = int(std::lround(y * canvasScale)) - pad;
	return out;
}

} // namespace laycue::clock
