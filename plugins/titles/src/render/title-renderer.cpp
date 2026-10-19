#include "render/title-renderer.hpp"

#include "core/easing.hpp"
#include "core/qr.hpp"
#include "render/brand-icons.hpp"

#include <QFileInfo>
#include <QFontMetricsF>
#include <QHash>
#include <QImage>
#include <QLinearGradient>
#include <QPainter>
#include <QPainterPath>
#include <QPolygonF>
#include <QStringList>
#include <QTime>
#include <QTransform>

#include <algorithm>
#include <cmath>
#include <cstring>
#include <functional>
#include <vector>

namespace laycue::titles {

namespace {

constexpr double kPi = 3.14159265358979323846; // M_PI is not defined by default on MSVC
constexpr double kDesignWidth = 1920.0;
constexpr double kLeft = 110.0;         // left margin (inside the broadcast title-safe area)
constexpr double kBottom = 96.0;        // distance from the bottom of the picture
constexpr double kRegionHeight = 440.0; // room above the title for it to grow when made bigger
constexpr double kTallRegionHeight = 560.0;

QColor toQ(const Rgba &c)
{
	return QColor(c.r, c.g, c.b, c.a);
}

QColor withOpacity(QColor c, double factor)
{
	c.setAlphaF(std::clamp(c.alphaF() * factor, 0.0, 1.0));
	return c;
}

// Everything a style needs while drawing one frame. All lengths are in design pixels: the painter
// is scaled so a style never thinks about the real canvas size.
struct Ctx {
	QPainter &p;
	const TitleTemplate &t;
	const TitleText &x;
	const FontSet &fonts;
	double W = kDesignWidth;  // region width
	double H = kRegionHeight; // region height
	double in = 1.0;
	double out = 0.0;
	double tickerPx = 0.0;
	QColor panel;
	QColor accent;
	QColor text;
	QColor sub;
	QColor shape2;
	QColor shape3;
	QColor ink;
	int fit = 0;       // shape width: 0 the template's own, 1 hug the text, 2 a width the operator set
	double fitW = 700; // that width, in design pixels (fit == 2)
	// Side padding: a little more generous when the shape hugs the text.
	double pad(double x) const { return fit == 1 ? x * 1.15 : x; }
	QColor palette_ink() const { return ink; }

	// How far one element has got (0..1). Elements enter one after another (`delay`, `length` are
	// shares of the enter animation) but all leave together.
	double part(double delay, double length) const
	{
		if (out > 0.0)
			return 1.0 - easeInCubic(out);
		return easeOutExpo(stage(in, delay, length));
	}
	// The same, with a small overshoot.
	double partBack(double delay, double length) const
	{
		if (out > 0.0)
			return 1.0 - easeInCubic(out);
		return easeOutBack(stage(in, delay, length));
	}

	QFont font(FontFace face, double px) const { return fonts.font(face, px); }
	double bottom() const { return H - kBottom; }
	bool has(const QString &s) const { return !s.trimmed().isEmpty(); }
};

double textWidth(const QFont &font, const QString &s)
{
	return QFontMetricsF(font).horizontalAdvance(s);
}

// The biggest size, not above `px` and not below `minPx`, at which `s` fits in `maxWidth`.
QFont fitFont(const Ctx &c, FontFace face, double px, double minPx, const QString &s, double maxWidth)
{
	QFont font = c.font(face, px);
	for (double size = px; size > minPx && textWidth(font, s) > maxWidth; size *= 0.96)
		font = c.font(face, size);
	return font;
}

// A name's font for a shape with `chrome` pixels of padding and decoration beside the text. With a width the
// operator set, the text shrinks (down to a small size) to fit inside the shape instead of the shape growing.
QFont fitFontFor(const Ctx &c, FontFace face, double px, double minPx, const QString &s, double maxWidth, double chrome)
{
	if (c.fit == 2)
		return fitFont(c, face, px, std::min(minPx, 14.0), s, std::max(60.0, c.fitW - chrome));
	return fitFont(c, face, px, minPx, s, maxWidth);
}

// The width of a name's shape. `templateMin` is the template's own minimum; `textW` the text it holds;
// `share` scales a manual width for the smaller of two stacked shapes.
double shapeWidth(const Ctx &c, double templateMin, double chrome, double textW, double share = 1.0)
{
	switch (c.fit) {
	case 1:
		return std::max(220.0 * share, chrome + textW); // hugs the text, with the padding around it
	case 2:
		return c.fitW * share;
	default:
		return std::max(templateMin, chrome + textW);
	}
}

QString elided(const QFont &font, const QString &s, double maxWidth)
{
	return QFontMetricsF(font).elidedText(s, Qt::ElideRight, maxWidth);
}

// Words wrapped to `maxWidth`; explicit line breaks are kept.
QStringList wrapLines(const QFont &font, const QString &s, double maxWidth)
{
	QStringList lines;
	const QFontMetricsF fm(font);
	for (const QString &paragraph : s.split('\n')) {
		QString line;
		for (const QString &word : paragraph.split(' ', Qt::SkipEmptyParts)) {
			const QString candidate = line.isEmpty() ? word : line + ' ' + word;
			if (!line.isEmpty() && fm.horizontalAdvance(candidate) > maxWidth) {
				lines << line;
				line = word;
			} else {
				line = candidate;
			}
		}
		lines << line;
	}
	return lines;
}

struct Wrapped {
	QFont font;
	QStringList lines;
	double lineHeight = 0;
};

// Wraps `s` at the largest size (down to `minPx`) at which it needs at most `maxLines` lines.
Wrapped fitWrapped(const Ctx &c, FontFace face, double px, double minPx, const QString &s, double maxWidth,
		   int maxLines, double leading)
{
	Wrapped w;
	for (double size = px;; size *= 0.94) {
		w.font = c.font(face, size);
		w.lines = wrapLines(w.font, s, maxWidth);
		w.lineHeight = size * leading;
		if (int(w.lines.size()) <= maxLines || size * 0.94 < minPx)
			break;
	}
	if (int(w.lines.size()) > maxLines) {
		w.lines = w.lines.mid(0, maxLines);
		w.lines.last() = elided(w.font, w.lines.last() + QStringLiteral(" …"), maxWidth);
	}
	return w;
}

void drawText(QPainter &p, const QRectF &rect, const QString &s, const QFont &font, const QColor &colour,
	      Qt::Alignment align = Qt::AlignLeft | Qt::AlignVCenter, double opacity = 1.0, double tracking = 0.0)
{
	if (s.isEmpty() || opacity <= 0.0)
		return;
	QFont f = font;
	if (tracking != 0.0)
		f.setLetterSpacing(QFont::AbsoluteSpacing, tracking);
	p.save();
	p.setOpacity(p.opacity() * opacity);
	p.setFont(f);
	p.setPen(colour);
	p.drawText(rect, int(align) | Qt::TextSingleLine, s);
	p.restore();
}

// Text on bare video: a soft dark shadow underneath keeps it readable on any picture.
// Large text with five shadow passes costs tens of milliseconds, and during an animation only its position
// and opacity change. So the text and its shadow are drawn once into a small picture, kept per thread
// (the panel preview and OBS's graphics thread both render), and that picture is placed on each frame.
void drawShadowText(QPainter &p, const QRectF &rect, const QString &s, const QFont &font, const QColor &colour,
		    Qt::Alignment align = Qt::AlignLeft | Qt::AlignVCenter, double opacity = 1.0, double tracking = 0.0)
{
	if (s.isEmpty() || opacity <= 0.0 || rect.width() <= 0.0 || rect.height() <= 0.0)
		return;
	static const QPointF offsets[] = {{0, 3}, {0, 6}, {3, 4}, {-3, 4}, {0, 9}};
	constexpr double pad = 12.0; // room for the shadow around the text box
	const double scale = p.transform().m11();
	const QSize size(int(std::ceil((rect.width() + 2 * pad) * scale)),
			 int(std::ceil((rect.height() + 2 * pad) * scale)));
	const QString key = QStringLiteral("%1|%2|%3|%4|%5|%6x%7|%8")
				    .arg(s, font.key(), QString::number(colour.rgba(), 16), QString::number(int(align)),
					 QString::number(tracking), QString::number(size.width()),
					 QString::number(size.height()), QString::number(scale));
	thread_local QHash<QString, QImage> cache;
	auto it = cache.find(key);
	if (it == cache.end()) {
		if (cache.size() > 24)
			cache.clear();
		QImage img(size, QImage::Format_ARGB32_Premultiplied);
		img.fill(Qt::transparent);
		QPainter q(&img);
		q.setRenderHints(p.renderHints());
		q.scale(scale, scale);
		q.translate(pad, pad);
		const QRectF box(0, 0, rect.width(), rect.height());
		const QColor shadow(0, 0, 0, 70);
		for (const QPointF &o : offsets)
			drawText(q, box.translated(o), s, font, shadow, align);
		drawText(q, box, s, font, colour, align, 1.0, tracking);
		q.end();
		it = cache.insert(key, img);
	}
	const QPointF device = p.transform().map(QPointF(rect.left() - pad, rect.top() - pad));
	p.save();
	p.resetTransform();
	p.setOpacity(p.opacity() * opacity);
	p.drawImage(QPoint(int(std::lround(device.x())), int(std::lround(device.y()))), it.value());
	p.restore();
}

QPainterPath roundedRect(const QRectF &r, double radius)
{
	QPainterPath path;
	radius = std::min(radius, std::min(r.width(), r.height()) / 2.0);
	path.addRoundedRect(r, radius, radius);
	return path;
}

void fillRect(QPainter &p, const QRectF &r, const QColor &colour, double radius = 0.0)
{
	if (r.width() <= 0.0 || r.height() <= 0.0 || colour.alpha() == 0)
		return;
	p.save();
	p.setPen(Qt::NoPen);
	p.setBrush(colour);
	if (radius > 0.0)
		p.drawPath(roundedRect(r, radius));
	else
		p.drawRect(r);
	p.restore();
}

// A panel with a soft shadow under it, so it lifts off the picture like a broadcast graphic.
void fillPanel(Ctx &c, const QRectF &r, double radius = 0.0)
{
	if (c.panel.alpha() == 0 || r.width() <= 0.0 || r.height() <= 0.0)
		return;
	for (int i = 5; i >= 1; --i)
		fillRect(c.p, r.adjusted(-i * 2.0, -i * 1.0 + 6.0, i * 2.0, i * 2.0 + 6.0),
			 QColor(0, 0, 0, 9 + (6 - i) * 2), radius > 0.0 ? radius + i * 2.0 : i * 2.0);
	fillRect(c.p, r, c.panel, radius);
}

// A bottom scrim: darkens the lower part of the picture so bare text stays readable.
void drawScrim(Ctx &c, double heightShare, double strength, double amount)
{
	const double top = c.H * (1.0 - heightShare);
	QLinearGradient g(0, top, 0, c.H);
	g.setColorAt(0.0, QColor(0, 0, 0, 0));
	g.setColorAt(1.0, QColor(0, 0, 0, int(255 * strength * amount)));
	c.p.save();
	c.p.setPen(Qt::NoPen);
	c.p.setBrush(g);
	c.p.drawRect(QRectF(0, top, c.W, c.H - top));
	c.p.restore();
}

// ---- The group movement: how the whole title arrives and leaves --------------------------------

struct Group {
	MotionKind kind = MotionKind::Slide;
	double amount = 1.0; // 0 = away, 1 = on screen
};

Group groupFor(const Ctx &c)
{
	Group g;
	if (c.out > 0.0) {
		g.kind = c.t.motionOut;
		g.amount = 1.0 - easeInCubic(c.out);
	} else {
		g.kind = c.t.motionIn;
		g.amount = g.kind == MotionKind::Pop ? easeOutBack(c.in) : easeOutExpo(c.in);
	}
	return g;
}

void applyGroup(Ctx &c, const Group &g, bool verticalSlide, QPointF pivot)
{
	QPainter &p = c.p;
	const double a = g.amount;
	switch (g.kind) {
	case MotionKind::Slide:
		if (verticalSlide)
			p.translate(0, 140.0 * (1.0 - a));
		else
			p.translate(-640.0 * (1.0 - a), 0);
		p.setOpacity(std::clamp(a * 2.5, 0.0, 1.0));
		break;
	case MotionKind::Rise:
		p.translate(0, 38.0 * (1.0 - a));
		p.setOpacity(std::clamp(a, 0.0, 1.0));
		break;
	case MotionKind::Fade:
		p.setOpacity(std::clamp(a, 0.0, 1.0));
		break;
	case MotionKind::Pop: {
		const double scale = 0.88 + 0.12 * a;
		p.translate(pivot);
		p.scale(scale, scale);
		p.translate(-pivot);
		p.setOpacity(std::clamp(a * 1.6, 0.0, 1.0));
		break;
	}
	case MotionKind::Wipe: {
		const double w = std::clamp(a, 0.0, 1.0);
		if (c.out > 0.0)
			p.setClipRect(QRectF(c.W * (1.0 - w), 0, c.W * w + 1.0, c.H));
		else
			p.setClipRect(QRectF(0, 0, c.W * w + 1.0, c.H));
		break;
	}
	}
}

// ---- The 20 styles -----------------------------------------------------------------------------

using Style = std::function<void(Ctx &)>;

// 1. Clean Bar: a dark bar with an accent cap; the name grows out of it.
void cleanBar(Ctx &c)
{
	const double padX = c.pad(40), capW = 12;
	const double chrome = capW + 2 * padX;
	const bool two = c.has(c.x.line2);
	const double h = two ? 132 : 98;
	const QFont f1 = fitFontFor(c, c.t.display, 62, 40, c.x.line1, 1250, chrome);
	const QFont f2 = fitFontFor(c, c.t.body, 30, 22, c.x.line2, 1250, chrome);
	const double w = shapeWidth(c, 560.0, chrome, std::max(textWidth(f1, c.x.line1), textWidth(f2, c.x.line2)));
	const double y = c.bottom() - h;
	const double pw = w * c.part(0.0, 0.6);
	fillPanel(c, QRectF(kLeft, y, pw, h));
	fillRect(c.p, QRectF(kLeft, y + h * (1.0 - c.part(0.0, 0.4)), capW, h * c.part(0.0, 0.4)), c.accent);
	c.p.save();
	c.p.setClipRect(QRectF(kLeft, y, pw, h));
	const double e1 = c.part(0.28, 0.5), e2 = c.part(0.4, 0.5);
	const double tx = kLeft + capW + padX;
	if (two) {
		drawText(c.p, QRectF(tx + 28 * (1 - e1), y + 12, w, 70), c.x.line1, f1, c.text,
			 Qt::AlignLeft | Qt::AlignVCenter, e1);
		drawText(c.p, QRectF(tx + 28 * (1 - e2), y + 80, w, 40), c.x.line2, f2, c.sub,
			 Qt::AlignLeft | Qt::AlignVCenter, e2);
	} else {
		drawText(c.p, QRectF(tx + 28 * (1 - e1), y, w, h), c.x.line1, f1, c.text,
			 Qt::AlignLeft | Qt::AlignVCenter, e1);
	}
	c.p.restore();
}

// 2. Accent Edge: a light card behind a coloured edge that draws down first.
void accentEdge(Ctx &c)
{
	const double padX = c.pad(44), edge = 9;
	const double chrome = edge + 2 * padX;
	const bool two = c.has(c.x.line2);
	const double h = two ? 138 : 104;
	const QFont f1 = fitFontFor(c, c.t.display, 64, 40, c.x.line1, 1250, chrome);
	const QFont f2 = fitFontFor(c, c.t.body, 30, 22, c.x.line2, 1250, chrome);
	const double w = shapeWidth(c, 600.0, chrome, std::max(textWidth(f1, c.x.line1), textWidth(f2, c.x.line2)));
	const double y = c.bottom() - h;
	const double eEdge = c.part(0.0, 0.4), eCard = c.part(0.18, 0.6);
	fillRect(c.p, QRectF(kLeft, y, edge, h * eEdge), c.accent, c.t.cornerRadius);
	c.p.save();
	c.p.setClipRect(QRectF(kLeft + edge, y, (w - edge) * eCard, h));
	fillPanel(c, QRectF(kLeft + edge, y, w - edge, h));
	c.p.restore();
	c.p.save();
	c.p.setClipRect(QRectF(kLeft + edge, y, (w - edge) * eCard, h));
	const double e1 = c.part(0.3, 0.5), e2 = c.part(0.42, 0.5);
	const double tx = kLeft + edge + padX;
	if (two) {
		drawText(c.p, QRectF(tx, y + 14 + 18 * (1 - e1), w, 68), c.x.line1, f1, c.text,
			 Qt::AlignLeft | Qt::AlignVCenter, e1);
		drawText(c.p, QRectF(tx, y + 82 + 18 * (1 - e2), w, 40), c.x.line2, f2, c.sub,
			 Qt::AlignLeft | Qt::AlignVCenter, e2);
	} else {
		drawText(c.p, QRectF(tx, y + 18 * (1 - e1), w, h), c.x.line1, f1, c.text,
			 Qt::AlignLeft | Qt::AlignVCenter, e1);
	}
	c.p.restore();
}

// 3. Split Name: two blocks stepping down; the name in colour, the role in dark.
void splitBlocks(Ctx &c)
{
	const double padX = c.pad(36);
	const QFont f1 = fitFontFor(c, c.t.display, 62, 40, c.x.line1, 1200, 2 * padX);
	const QFont f2 = fitFontFor(c, c.t.body, 30, 22, c.x.line2, 1200, 2 * padX + 0.25 * c.fitW);
	const double h1 = 88, h2 = c.has(c.x.line2) ? 58 : 0;
	const double w1 = shapeWidth(c, 380.0, 2 * padX, textWidth(f1, c.x.line1));
	const double w2 = shapeWidth(c, 300.0, 2 * padX, textWidth(f2, c.x.line2), 0.75);
	const double y1 = c.bottom() - h1 - h2;
	const double e1 = c.part(0.0, 0.55), e2 = c.part(0.14, 0.55);
	c.p.save();
	c.p.setClipRect(QRectF(0, 0, kLeft + w1 * e1, c.H));
	fillRect(c.p, QRectF(kLeft, y1, w1, h1), c.accent);
	drawText(c.p, QRectF(kLeft + padX, y1, w1, h1), c.x.line1, f1, c.text, Qt::AlignLeft | Qt::AlignVCenter,
		 c.part(0.3, 0.45));
	c.p.restore();
	if (h2 > 0) {
		c.p.save();
		c.p.setClipRect(QRectF(0, 0, kLeft + 44 + w2 * e2, c.H));
		fillPanel(c, QRectF(kLeft + 44, y1 + h1, w2, h2));
		drawText(c.p, QRectF(kLeft + 44 + padX, y1 + h1, w2, h2), c.x.line2, f2, c.sub,
			 Qt::AlignLeft | Qt::AlignVCenter, c.part(0.42, 0.45));
		c.p.restore();
	}
}

// 4. Underline: no panel, a line draws under the name. For busy pictures.
void underline(Ctx &c)
{
	const QFont f1 = fitFont(c, c.t.display, 78, 44, c.x.line1, 1300);
	const QFont f2 = fitFont(c, c.t.body, 34, 24, c.x.line2, 1300);
	const double w = std::max(textWidth(f1, c.x.line1), textWidth(f2, c.x.line2)) + 8;
	const bool two = c.has(c.x.line2);
	const double base = c.bottom();
	const double y2 = base - (two ? 46 : 0);
	const double lineY = y2 - 8;
	const double y1 = lineY - 92;
	drawShadowText(c.p, QRectF(kLeft, y1, w, 88), c.x.line1, f1, c.text, Qt::AlignLeft | Qt::AlignVCenter,
		       c.part(0.0, 0.5));
	fillRect(c.p, QRectF(kLeft, lineY, w * c.part(0.12, 0.6), 5), c.accent);
	if (two)
		drawShadowText(c.p, QRectF(kLeft, lineY + 10, w, 44), c.x.line2, f2, c.sub,
			       Qt::AlignLeft | Qt::AlignVCenter, c.part(0.3, 0.5));
}

// 5. Box Pop: a rounded card with a tag chip across its top edge. It pops in.
void popCard(Ctx &c)
{
	const double padX = c.pad(44);
	const bool two = c.has(c.x.line2);
	const QFont f1 = fitFontFor(c, c.t.display, 58, 38, c.x.line1, 1200, 2 * padX);
	const QFont f2 = fitFontFor(c, c.t.body, 30, 22, c.x.line2, 1200, 2 * padX);
	const QFont ft = c.font(c.t.body, 22);
	const double h = two ? 148 : 112;
	const double w = shapeWidth(c, 520.0, 2 * padX, std::max(textWidth(f1, c.x.line1), textWidth(f2, c.x.line2)));
	const double y = c.bottom() - h;
	fillPanel(c, QRectF(kLeft, y, w, h), c.t.cornerRadius);
	const double e1 = c.part(0.18, 0.5), e2 = c.part(0.3, 0.5);
	if (two) {
		drawText(c.p, QRectF(kLeft + padX, y + 22, w, 66), c.x.line1, f1, c.text,
			 Qt::AlignLeft | Qt::AlignVCenter, e1);
		drawText(c.p, QRectF(kLeft + padX, y + 88, w, 40), c.x.line2, f2, c.sub,
			 Qt::AlignLeft | Qt::AlignVCenter, e2);
	} else {
		drawText(c.p, QRectF(kLeft + padX, y, w, h), c.x.line1, f1, c.text, Qt::AlignLeft | Qt::AlignVCenter,
			 e1);
	}
	if (c.has(c.x.tag)) {
		const QString tag = c.x.tag.toUpper();
		const double tw = textWidth(ft, tag) + 22 * 2 + tag.size() * 2.4;
		const double ey = c.partBack(0.1, 0.5);
		fillRect(c.p, QRectF(kLeft + padX - 8, y - 17 + 8 * (1 - ey), tw, 34), c.accent, 8);
		drawText(c.p, QRectF(kLeft + padX - 8, y - 17 + 8 * (1 - ey), tw, 34), tag, ft, c.ink, Qt::AlignCenter,
			 c.part(0.2, 0.4), 2.4);
	}
}

// 6. Corner Tag: a slanted block with a tag label above its corner.
void cornerTag(Ctx &c)
{
	const double padX = c.pad(52), skew = 26;
	const bool two = c.has(c.x.line2);
	const QFont f1 = fitFontFor(c, c.t.display, 66, 40, c.x.line1, 1200, 2 * padX);
	const QFont f2 = fitFontFor(c, c.t.body, 30, 22, c.x.line2, 1200, 2 * padX);
	const QFont ft = c.font(c.t.display, 26);
	const double h = two ? 138 : 104;
	const double w = shapeWidth(c, 560.0, 2 * padX, std::max(textWidth(f1, c.x.line1), textWidth(f2, c.x.line2)));
	const double y = c.bottom() - h;
	const double e = c.part(0.0, 0.6);
	c.p.save();
	c.p.setClipRect(QRectF(0, 0, kLeft + (w + skew) * e + 2, c.H));
	QPolygonF block;
	block << QPointF(kLeft + skew, y) << QPointF(kLeft + w + skew, y) << QPointF(kLeft + w, y + h)
	      << QPointF(kLeft, y + h);
	c.p.setPen(Qt::NoPen);
	c.p.setBrush(c.panel);
	c.p.drawPolygon(block);
	if (c.has(c.x.tag)) {
		const QString tag = c.x.tag.toUpper();
		const double tw = textWidth(ft, tag) + 2 * 26 + tag.size() * 3;
		QPolygonF chip;
		chip << QPointF(kLeft + skew + 26, y - 44) << QPointF(kLeft + skew + 26 + tw, y - 44)
		     << QPointF(kLeft + 26 + tw, y) << QPointF(kLeft + 26, y);
		c.p.setBrush(c.accent);
		c.p.drawPolygon(chip);
		drawText(c.p, QRectF(kLeft + skew + 8, y - 44, tw, 44), tag, ft, c.ink, Qt::AlignCenter,
			 c.part(0.3, 0.4), 3.0);
	}
	const double e1 = c.part(0.28, 0.5), e2 = c.part(0.4, 0.5);
	const double tx = kLeft + skew + padX - 20;
	if (two) {
		drawText(c.p, QRectF(tx + 24 * (1 - e1), y + 14, w, 70), c.x.line1, f1, c.text,
			 Qt::AlignLeft | Qt::AlignVCenter, e1);
		drawText(c.p, QRectF(tx + 24 * (1 - e2), y + 82, w, 40), c.x.line2, f2, c.sub,
			 Qt::AlignLeft | Qt::AlignVCenter, e2);
	} else {
		drawText(c.p, QRectF(tx + 24 * (1 - e1), y, w, h), c.x.line1, f1, c.text,
			 Qt::AlignLeft | Qt::AlignVCenter, e1);
	}
	c.p.restore();
}

// 7. Stacked: the name on a light panel, the role on a coloured one tucked under it.
void stackedPanels(Ctx &c)
{
	const double padX = c.pad(40);
	const QFont f1 = fitFontFor(c, c.t.display, 64, 40, c.x.line1, 1200, 2 * padX);
	const QFont f2 = fitFontFor(c, c.t.body, 30, 22, c.x.line2, 1200, 2 * padX + 0.25 * c.fitW);
	const double h1 = 92, h2 = c.has(c.x.line2) ? 56 : 0;
	const double w1 = shapeWidth(c, 420.0, 2 * padX, textWidth(f1, c.x.line1));
	const double w2 = shapeWidth(c, 300.0, 2 * padX, textWidth(f2, c.x.line2), 0.75);
	const double y1 = c.bottom() - h1 - h2;
	const double e1 = c.part(0.0, 0.55), e2 = c.part(0.16, 0.55);
	fillPanel(c, QRectF(kLeft, y1 + 22 * (1 - e1), w1, h1));
	drawText(c.p, QRectF(kLeft + padX, y1 + 22 * (1 - e1), w1, h1), c.x.line1, f1, c.text,
		 Qt::AlignLeft | Qt::AlignVCenter, c.part(0.22, 0.45));
	if (h2 > 0) {
		fillRect(c.p, QRectF(kLeft + 70, y1 + h1 + 18 * (1 - e2), w2, h2), c.accent);
		drawText(c.p, QRectF(kLeft + 70 + padX, y1 + h1 + 18 * (1 - e2), w2, h2), c.x.line2, f2, c.sub,
			 Qt::AlignLeft | Qt::AlignVCenter, c.part(0.36, 0.45));
	}
}

// 8. Minimal Line: a hairline above quiet text.
void hairline(Ctx &c)
{
	const QFont f1 = fitFont(c, c.t.display, 58, 38, c.x.line1, 1300);
	const QFont f2 = fitFont(c, c.t.body, 30, 22, c.x.line2, 1300);
	const double w = std::max(textWidth(f1, c.x.line1), textWidth(f2, c.x.line2)) + 8;
	const bool two = c.has(c.x.line2);
	const double base = c.bottom();
	const double y2 = base - (two ? 46 : 0);
	const double y1 = y2 - 70;
	const double lineY = y1 - 14;
	fillRect(c.p, QRectF(kLeft, lineY, w * c.part(0.0, 0.7), 2), c.panel.alpha() ? c.panel : c.accent);
	drawShadowText(c.p, QRectF(kLeft, y1, w, 66), c.x.line1, f1, c.text, Qt::AlignLeft | Qt::AlignVCenter,
		       c.part(0.2, 0.5));
	if (two)
		drawShadowText(c.p, QRectF(kLeft, y2, w, 42), c.x.line2, f2, c.sub, Qt::AlignLeft | Qt::AlignVCenter,
			       c.part(0.34, 0.5));
}

// 11. Sermon Series: a tall condensed title with a vertical accent bar. For the start of a message.
void seriesTitle(Ctx &c)
{
	const QFont f1 = fitFont(c, c.t.display, 150, 70, c.x.line1.toUpper(), 1300);
	const QFont f2 = fitFont(c, c.t.body, 40, 26, c.x.line2, 1300);
	const bool two = c.has(c.x.line2);
	const double h1 = 128, h2 = two ? 56 : 0;
	const double y = c.bottom() - h1 - h2;
	const double e = c.part(0.0, 0.5);
	fillRect(c.p, QRectF(kLeft, y + (h1 + h2) * (1 - e), 12, (h1 + h2) * e), c.accent);
	c.p.save();
	c.p.setClipRect(QRectF(kLeft + 30, y - 10, (c.W - kLeft) * c.part(0.12, 0.7), h1 + h2 + 20));
	drawShadowText(c.p, QRectF(kLeft + 36, y, c.W, h1), c.x.line1.toUpper(), f1, c.text,
		       Qt::AlignLeft | Qt::AlignVCenter, 1.0, 2.0);
	if (two)
		drawShadowText(c.p, QRectF(kLeft + 40, y + h1, c.W, h2), c.x.line2, f2, c.sub,
			       Qt::AlignLeft | Qt::AlignVCenter, 1.0, 1.0);
	c.p.restore();
}

// 12. Song Lyric: large centred lines over a soft scrim. Built for readability.
void songLyric(Ctx &c)
{
	const double w = c.W - 2 * 170.0;
	drawScrim(c, 0.62, 0.62, c.part(0.0, 0.8));
	QStringList lines;
	if (c.has(c.x.line1))
		lines << c.x.line1;
	if (c.has(c.x.line2))
		lines << c.x.line2;
	if (lines.isEmpty())
		return;
	const QFont f = fitFont(c, c.t.display, 72, 40,
				lines.size() > 1 && textWidth(c.font(c.t.display, 72), lines[0]) <
							    textWidth(c.font(c.t.display, 72), lines[1])
					? lines[1]
					: lines[0],
				w);
	const double lh = f.pixelSize() * 1.3;
	const double top = c.bottom() - lh * lines.size();
	for (int i = 0; i < lines.size(); ++i) {
		const double e = c.part(0.1 + 0.12 * i, 0.6);
		drawShadowText(c.p, QRectF(170, top + i * lh + 10 * (1 - e), w, lh), elided(f, lines[i], w), f, c.text,
			       Qt::AlignHCenter | Qt::AlignVCenter, e);
	}
}

// 13. Announcement: a wide banner with a tag block.
void announcement(Ctx &c)
{
	const double h = 124, padX = 44;
	const double w = c.W - 2 * kLeft;
	const bool two = c.has(c.x.line2);
	const QFont ft = c.font(c.t.display, 32);
	const QString tag = (c.has(c.x.tag) ? c.x.tag : QString()).toUpper();
	const double tagW = tag.isEmpty() ? 0 : textWidth(ft, tag) + 2 * 36 + tag.size() * 3.0;
	const double y = c.bottom() - h;
	fillPanel(c, QRectF(kLeft, y, w, h));
	if (tagW > 0) {
		const double et = c.part(0.0, 0.5);
		fillRect(c.p, QRectF(kLeft, y, tagW * et, h), c.accent);
		c.p.save();
		c.p.setClipRect(QRectF(kLeft, y, tagW * et, h));
		drawText(c.p, QRectF(kLeft, y, tagW, h), tag, ft, c.ink, Qt::AlignCenter, 1.0, 3.0);
		c.p.restore();
	}
	const QFont f1 = fitFont(c, c.t.display, 58, 36, c.x.line1, w - tagW - 2 * padX);
	const QFont f2 = fitFont(c, c.t.body, 28, 20, c.x.line2, w - tagW - 2 * padX);
	const double tx = kLeft + tagW + padX;
	const double e1 = c.part(0.22, 0.5), e2 = c.part(0.34, 0.5);
	if (two) {
		drawText(c.p, QRectF(tx + 24 * (1 - e1), y + 8, w - tagW - padX, 66), c.x.line1, f1, c.text,
			 Qt::AlignLeft | Qt::AlignVCenter, e1);
		drawText(c.p, QRectF(tx + 24 * (1 - e2), y + 74, w - tagW - padX, 38), c.x.line2, f2, c.sub,
			 Qt::AlignLeft | Qt::AlignVCenter, e2);
	} else {
		drawText(c.p, QRectF(tx + 24 * (1 - e1), y, w - tagW - padX, h), c.x.line1, f1, c.text,
			 Qt::AlignLeft | Qt::AlignVCenter, e1);
	}
}

// 14. Breaking: a light bar with a red label that flashes as it lands.
void breaking(Ctx &c)
{
	const double h = 116, padX = 40;
	const double w = c.W - 2 * kLeft;
	const bool two = c.has(c.x.line2);
	const QFont ft = c.font(c.t.display, 36);
	const QString tag = (c.has(c.x.tag) ? c.x.tag : QStringLiteral("Breaking")).toUpper();
	const double tagW = textWidth(ft, tag) + 2 * 38 + tag.size() * 3.0;
	const double y = c.bottom() - h;
	const double eBar = c.part(0.1, 0.6), eTag = c.part(0.0, 0.45);
	fillPanel(c, QRectF(kLeft + tagW, y, (w - tagW) * eBar, h));
	fillRect(c.p, QRectF(kLeft, y, tagW * eTag, h), c.accent);
	// a brief white flash across the label as it arrives
	if (c.out <= 0.0) {
		const double flash = 1.0 - stage(c.in, 0.25, 0.3);
		if (flash > 0.0 && c.in > 0.25)
			fillRect(c.p, QRectF(kLeft, y, tagW, h), QColor(255, 255, 255, int(160 * flash)));
	}
	drawText(c.p, QRectF(kLeft, y, tagW, h), tag, ft, c.ink, Qt::AlignCenter, c.part(0.2, 0.4), 3.0);
	const QFont f1 = fitFont(c, c.t.display, 56, 34, c.x.line1, w - tagW - 2 * padX);
	const QFont f2 = fitFont(c, c.t.body, 28, 20, c.x.line2, w - tagW - 2 * padX);
	const double e1 = c.part(0.3, 0.5), e2 = c.part(0.4, 0.5);
	const double tx = kLeft + tagW + padX;
	if (two) {
		drawText(c.p, QRectF(tx + 24 * (1 - e1), y + 8, w - tagW - padX, 62), c.x.line1, f1, c.text,
			 Qt::AlignLeft | Qt::AlignVCenter, e1);
		drawText(c.p, QRectF(tx + 24 * (1 - e2), y + 70, w - tagW - padX, 38), c.x.line2, f2, c.sub,
			 Qt::AlignLeft | Qt::AlignVCenter, e2);
	} else {
		drawText(c.p, QRectF(tx + 24 * (1 - e1), y, w - tagW - padX, h), c.x.line1, f1, c.text,
			 Qt::AlignLeft | Qt::AlignVCenter, e1);
	}
}

// 15. News Headline: a dark bar with a coloured "live" label.
void newsBar(Ctx &c)
{
	const double h = 112, padX = 40;
	const double w = c.W - 2 * kLeft;
	const bool two = c.has(c.x.line2);
	const QFont ft = c.font(c.t.display, 32);
	const QString tag = (c.has(c.x.tag) ? c.x.tag : QStringLiteral("Live")).toUpper();
	const double tagW = textWidth(ft, tag) + 2 * 34 + 34 + tag.size() * 3.0;
	const double y = c.bottom() - h;
	fillPanel(c, QRectF(kLeft, y, w * c.part(0.0, 0.65), h));
	const double et = c.part(0.1, 0.5);
	fillRect(c.p, QRectF(kLeft, y, tagW * et, h), c.accent);
	c.p.save();
	c.p.setClipRect(QRectF(kLeft, y, tagW * et, h));
	c.p.setPen(Qt::NoPen);
	c.p.setBrush(QColor(255, 255, 255));
	c.p.drawEllipse(QPointF(kLeft + 38, y + h / 2), 8, 8);
	drawText(c.p, QRectF(kLeft + 56, y, tagW, h), tag, ft, c.ink, Qt::AlignLeft | Qt::AlignVCenter, 1.0, 3.0);
	c.p.restore();
	const QFont f1 = fitFont(c, c.t.display, 54, 34, c.x.line1, w - tagW - 2 * padX);
	const QFont f2 = fitFont(c, c.t.body, 28, 20, c.x.line2, w - tagW - 2 * padX);
	const double e1 = c.part(0.3, 0.5), e2 = c.part(0.4, 0.5);
	const double tx = kLeft + tagW + padX;
	if (two) {
		drawText(c.p, QRectF(tx + 24 * (1 - e1), y + 8, w - tagW - padX, 62), c.x.line1, f1, c.text,
			 Qt::AlignLeft | Qt::AlignVCenter, e1);
		drawText(c.p, QRectF(tx + 24 * (1 - e2), y + 68, w - tagW - padX, 38), c.x.line2, f2, c.sub,
			 Qt::AlignLeft | Qt::AlignVCenter, e2);
	} else {
		drawText(c.p, QRectF(tx + 24 * (1 - e1), y, w - tagW - padX, h), c.x.line1, f1, c.text,
			 Qt::AlignLeft | Qt::AlignVCenter, e1);
	}
}

// 16. Social Handle: a small pill for a name or handle, with the platform on the right.
void handlePill(Ctx &c)
{
	const double h = 78, padL = 30, padR = 34;
	const QFont f1 = fitFont(c, c.t.display, 36, 24, c.x.line1, 900);
	const QFont ft = c.font(c.t.body, 24);
	const QString tag = c.x.tag.toUpper();
	const double tagW = tag.isEmpty() ? 0 : textWidth(ft, tag) + tag.size() * 2.0 + 30;
	const double w = padL + 34 + textWidth(f1, c.x.line1) + (tagW > 0 ? tagW + 22 : 0) + padR;
	const double y = c.bottom() - h;
	fillPanel(c, QRectF(kLeft, y, w, h), h / 2.0);
	const double e = c.partBack(0.15, 0.5);
	c.p.save();
	c.p.setPen(Qt::NoPen);
	c.p.setBrush(c.accent);
	c.p.drawEllipse(QPointF(kLeft + padL + 8, y + h / 2), 9 * e, 9 * e);
	c.p.restore();
	drawText(c.p, QRectF(kLeft + padL + 34, y, w, h), c.x.line1, f1, c.text, Qt::AlignLeft | Qt::AlignVCenter,
		 c.part(0.2, 0.5));
	if (tagW > 0) {
		const double dx = kLeft + padL + 34 + textWidth(f1, c.x.line1) + 20;
		fillRect(c.p, QRectF(dx, y + 22, 2, h - 44), withOpacity(c.sub, 0.5));
		drawText(c.p, QRectF(dx + 20, y, tagW, h), tag, ft, c.sub, Qt::AlignLeft | Qt::AlignVCenter,
			 c.part(0.3, 0.5), 2.0);
	}
}

// 17. Thank You: centred closing words over a dimmed picture.
void thankYou(Ctx &c)
{
	const double dim = c.part(0.0, 0.9);
	c.p.save();
	c.p.setOpacity(1.0);
	fillRect(c.p, QRectF(0, 0, c.W, c.H), QColor(0, 0, 0, int(115 * dim)));
	c.p.restore();
	const QFont f1 = fitFont(c, c.t.display, 200, 90, c.x.line1.toUpper(), c.W - 2 * 200.0);
	const QFont f2 = fitFont(c, c.t.body, 46, 28, c.x.line2, c.W - 2 * 260.0);
	const bool two = c.has(c.x.line2);
	const double h1 = 190, rule = 34, h2 = two ? 60 : 0;
	const double top = (c.H - (h1 + rule + h2)) / 2.0;
	drawShadowText(c.p, QRectF(0, top + 16 * (1 - c.part(0.1, 0.6)), c.W, h1), c.x.line1.toUpper(), f1, c.text,
		       Qt::AlignCenter, c.part(0.1, 0.6), 4.0);
	fillRect(c.p, QRectF(c.W / 2 - 60 * c.part(0.25, 0.5), top + h1 + rule / 2 - 2, 120 * c.part(0.25, 0.5), 4),
		 c.accent);
	if (two)
		drawShadowText(c.p, QRectF(0, top + h1 + rule + 12 * (1 - c.part(0.35, 0.5)), c.W, h2), c.x.line2, f2,
			       c.sub, Qt::AlignCenter, c.part(0.35, 0.5));
}

// ---- Live: the stream is on air ------------------------------------------------------------------

// Where the pulse is, 0..1, from the steadily growing position the renderer is given (100 px per second).
double pulsePhase(const Ctx &c)
{
	const double seconds = c.t.pulseSeconds > 0.0 ? c.t.pulseSeconds : 1.4;
	return std::fmod(c.tickerPx / 100.0 / seconds, 1.0);
}

// The light: a solid dot that breathes, with a ring that grows and fades once per pulse.
void drawLiveDot(Ctx &c, QPointF centre, double radius, const QColor &colour, double appear)
{
	const double ph = pulsePhase(c);
	const double breathe = 0.78 + 0.22 * (0.5 + 0.5 * std::cos(ph * 2.0 * kPi));
	c.p.save();
	c.p.setPen(Qt::NoPen);
	QColor ring = colour;
	ring.setAlphaF(std::clamp((1.0 - ph) * 0.55, 0.0, 1.0) * appear);
	c.p.setBrush(ring);
	const double rr = radius * (1.0 + 1.1 * ph);
	c.p.drawEllipse(centre, rr, rr);
	QColor dot = colour;
	dot.setAlphaF(breathe * appear);
	c.p.setBrush(dot);
	c.p.drawEllipse(centre, radius * appear, radius * appear);
	c.p.restore();
}

// 21. Live Badge: a small red block with a pulsing light in the top corner; the word is the operator's.
void liveBadge(Ctx &c)
{
	const double h = 64, top = 64, padL = 22, dotArea = 34, padR = 26;
	const QString word = (c.has(c.x.line1) ? c.x.line1 : QStringLiteral("LIVE")).toUpper();
	const QFont fw = c.font(c.t.display, 40);
	const double ww = textWidth(fw, word) + word.size() * 3.0;
	const double bw = padL + dotArea + ww + padR;
	const double e = c.part(0.0, 0.5);
	const QRectF block(kLeft, top, bw * e, h);
	fillPanel(c, block, 12);
	c.p.save();
	c.p.setClipRect(block);
	fillRect(c.p, QRectF(kLeft, top, bw, h), c.accent, 12);
	drawLiveDot(c, QPointF(kLeft + padL + 10, top + h / 2), 10, QColor(255, 255, 255), c.part(0.15, 0.4));
	drawText(c.p, QRectF(kLeft + padL + dotArea, top, ww + 10, h), word, fw, c.ink,
		 Qt::AlignLeft | Qt::AlignVCenter, c.part(0.2, 0.4), 3.0);
	c.p.restore();
	if (c.has(c.x.line2)) {
		const QFont fn = fitFont(c, c.t.body, 30, 22, c.x.line2, 760);
		const double nw = textWidth(fn, c.x.line2) + 2 * 26;
		const double e2 = c.part(0.3, 0.5);
		const QRectF pill(kLeft + bw - 6, top, (nw + 6) * e2, h);
		c.p.save();
		c.p.setClipRect(pill);
		fillRect(c.p, QRectF(kLeft + bw - 12, top, nw + 12, h), c.panel, 12);
		drawText(c.p, QRectF(kLeft + bw + 20, top, nw, h), c.x.line2, fn, c.text,
			 Qt::AlignLeft | Qt::AlignVCenter, c.part(0.4, 0.4));
		c.p.restore();
	}
}

// 22. On Air: a lower third with a red LIVE block, the broadcast's title and a line under it.
void onAir(Ctx &c)
{
	const double padX = 36, blockW = 214;
	const bool two = c.has(c.x.line2);
	const double h = two ? 124 : 96;
	const QString word = (c.has(c.x.tag) ? c.x.tag : QStringLiteral("LIVE")).toUpper();
	const QFont fw = c.font(c.t.display, 44);
	const QFont f1 = fitFont(c, c.t.display, 56, 36, c.x.line1, 1150);
	const QFont f2 = fitFont(c, c.t.body, 28, 22, c.x.line2, 1150);
	const double w =
		std::max(700.0, blockW + 2 * padX + std::max(textWidth(f1, c.x.line1), textWidth(f2, c.x.line2)));
	const double y = c.bottom() - h;
	const double pw = w * c.part(0.0, 0.6);
	fillPanel(c, QRectF(kLeft, y, pw, h));
	c.p.save();
	c.p.setClipRect(QRectF(kLeft, y, pw, h));
	fillRect(c.p, QRectF(kLeft, y, blockW, h), c.accent);
	drawLiveDot(c, QPointF(kLeft + 46, y + h / 2), 11, QColor(255, 255, 255), c.part(0.2, 0.4));
	drawText(c.p, QRectF(kLeft + 74, y, blockW - 74, h), word, fw, c.ink, Qt::AlignLeft | Qt::AlignVCenter,
		 c.part(0.25, 0.4), 3.0);
	const double e1 = c.part(0.3, 0.5), e2 = c.part(0.42, 0.5);
	const double tx = kLeft + blockW + padX;
	if (two) {
		drawText(c.p, QRectF(tx + 24 * (1 - e1), y + 10, w, 66), c.x.line1, f1, c.text,
			 Qt::AlignLeft | Qt::AlignVCenter, e1);
		drawText(c.p, QRectF(tx + 24 * (1 - e2), y + 76, w, 36), c.x.line2, f2, c.sub,
			 Qt::AlignLeft | Qt::AlignVCenter, e2);
	} else {
		drawText(c.p, QRectF(tx + 24 * (1 - e1), y, w, h), c.x.line1, f1, c.text,
			 Qt::AlignLeft | Qt::AlignVCenter, e1);
	}
	c.p.restore();
}

// ---- Social: where to find the operator -------------------------------------------------------------

enum class Platform { YouTube, Instagram, TikTok, Facebook, Mixlr };

void fillBrandIcon(QPainter &p, Brand brand, const QRectF &box, const QColor &colour)
{
	p.save();
	p.translate(box.topLeft());
	p.scale(box.width() / 24.0, box.height() / 24.0);
	p.setPen(Qt::NoPen);
	p.setBrush(colour);
	p.drawPath(brandIconPath(brand));
	p.restore();
}

// The platform's tile: its colour (Instagram's gradient) with its icon on top. Mixlr's logo is not in any
// open icon set, so its dots are described in brand-icons.cpp from the logo file the operator supplied.
void drawPlatformTile(Ctx &c, Platform platform, const QRectF &tile)
{
	c.p.save();
	c.p.setPen(Qt::NoPen);
	const double r = 16;
	switch (platform) {
	case Platform::YouTube:
		c.p.setBrush(QColor(0xFF, 0x00, 0x00));
		c.p.drawRoundedRect(tile, r, r);
		fillBrandIcon(c.p, Brand::YouTube, tile.adjusted(12, 12, -12, -12), QColor(255, 255, 255));
		break;
	case Platform::Instagram: {
		QLinearGradient g(tile.bottomLeft(), tile.topRight());
		g.setColorAt(0.0, QColor(0xFE, 0xDA, 0x77));
		g.setColorAt(0.25, QColor(0xF5, 0x85, 0x29));
		g.setColorAt(0.5, QColor(0xDD, 0x2A, 0x7B));
		g.setColorAt(0.75, QColor(0x81, 0x34, 0xAF));
		g.setColorAt(1.0, QColor(0x51, 0x5B, 0xD4));
		c.p.setBrush(g);
		c.p.drawRoundedRect(tile, r, r);
		fillBrandIcon(c.p, Brand::Instagram, tile.adjusted(13, 13, -13, -13), QColor(255, 255, 255));
		break;
	}
	case Platform::TikTok: {
		c.p.setBrush(QColor(0, 0, 0));
		c.p.drawRoundedRect(tile, r, r);
		const QRectF box = tile.adjusted(14, 14, -14, -14);
		fillBrandIcon(c.p, Brand::TikTok, box.translated(-2.2, -2.2), QColor(0x25, 0xF4, 0xEE));
		fillBrandIcon(c.p, Brand::TikTok, box.translated(2.2, 2.2), QColor(0xFE, 0x2C, 0x55));
		fillBrandIcon(c.p, Brand::TikTok, box, QColor(255, 255, 255));
		break;
	}
	case Platform::Facebook:
		c.p.setBrush(QColor(0x08, 0x66, 0xFF));
		c.p.drawRoundedRect(tile, r, r);
		fillBrandIcon(c.p, Brand::Facebook, tile.adjusted(10, 10, -10, -10), QColor(255, 255, 255));
		break;
	case Platform::Mixlr: {
		// Mixlr's own mark: its red with a ring of dots.
		c.p.setBrush(QColor((kMixlrRed >> 16) & 0xFF, (kMixlrRed >> 8) & 0xFF, kMixlrRed & 0xFF));
		c.p.drawRoundedRect(tile, r, r);
		for (const LogoDot &d : mixlrDots()) {
			c.p.setBrush(QColor(255, 255, 255, d.faint ? 128 : 255));
			const double rad = kMixlrDotRadius * tile.width();
			c.p.drawEllipse(QPointF(tile.left() + d.x * tile.width(), tile.top() + d.y * tile.height()),
					rad, rad);
		}
		break;
	}
	}
	c.p.restore();
}

// 23-27. Social cards: the platform's tile, its name, the operator's handle and a call to action.
void socialCard(Ctx &c, Platform platform)
{
	const double h = 100, pad = 14, tileS = h - 2 * pad, gapT = 26, padR = 28;
	const QString platformName = c.has(c.x.tag) ? c.x.tag.toUpper() : QString();
	const QFont fh = fitFont(c, c.t.display, 46, 28, c.x.line1, 760);
	const QFont ftag = c.font(c.t.body, 20);
	const QFont fcta = c.font(c.t.display, 28);
	const bool cta = c.has(c.x.line2);
	const QString ctaText = c.x.line2.toUpper();
	const double ctaW = cta ? textWidth(fcta, ctaText) + ctaText.size() * 2.0 + 44 : 0.0;
	const double textW =
		std::max(textWidth(fh, c.x.line1), textWidth(ftag, platformName) + platformName.size() * 3.0);
	const double w = pad + tileS + gapT + textW + (cta ? 30 + ctaW : 0.0) + padR;
	const double y = c.bottom() - h;
	const double e = c.part(0.0, 0.55);
	fillPanel(c, QRectF(kLeft, y, w * e, h), c.t.cornerRadius);
	c.p.save();
	c.p.setClipRect(QRectF(kLeft, y, w * e + 1, h));
	fillRect(c.p, QRectF(kLeft, y, w, h), c.panel, c.t.cornerRadius);
	// The tile pops in a little after the card starts to open.
	const double pop = c.partBack(0.08, 0.45);
	const QRectF tile(kLeft + pad, y + pad, tileS, tileS);
	c.p.save();
	c.p.translate(tile.center());
	c.p.scale(std::max(pop, 0.001), std::max(pop, 0.001));
	c.p.translate(-tile.center());
	drawPlatformTile(c, platform, tile);
	c.p.restore();
	const double tx = kLeft + pad + tileS + gapT;
	const double e1 = c.part(0.25, 0.5), e2 = c.part(0.34, 0.5);
	if (!platformName.isEmpty()) {
		drawText(c.p, QRectF(tx + 20 * (1 - e1), y + 12, textW + 40, 30), platformName, ftag, c.sub,
			 Qt::AlignLeft | Qt::AlignVCenter, e1, 3.0);
		drawText(c.p, QRectF(tx + 20 * (1 - e2), y + 40, textW + 40, 52), c.x.line1, fh, c.text,
			 Qt::AlignLeft | Qt::AlignVCenter, e2);
	} else {
		drawText(c.p, QRectF(tx + 20 * (1 - e2), y, textW + 40, h), c.x.line1, fh, c.text,
			 Qt::AlignLeft | Qt::AlignVCenter, e2);
	}
	if (cta) {
		const double e3 = c.partBack(0.45, 0.4);
		const QRectF chip(kLeft + w - padR - ctaW, y + (h - 52) / 2.0, ctaW, 52);
		c.p.save();
		c.p.translate(chip.center());
		c.p.scale(std::max(e3, 0.001), std::max(e3, 0.001));
		c.p.translate(-chip.center());
		fillRect(c.p, chip, c.accent, 12);
		drawText(c.p, chip, ctaText, fcta, QColor(17, 17, 16), Qt::AlignCenter, 1.0, 2.0);
		c.p.restore();
	}
	c.p.restore();
}

// ---- Social call to action: subscribe, bell, like, share ------------------------------------------------

// Small icons in a 24 x 24 box, drawn by hand from simple shapes.
QPainterPath bellPath()
{
	QPainterPath p;
	p.moveTo(4.5, 17.5);
	p.lineTo(6.5, 15.5);
	p.lineTo(6.5, 10.5);
	p.cubicTo(6.5, 7.2, 8.7, 5.2, 12.0, 5.2);
	p.cubicTo(15.3, 5.2, 17.5, 7.2, 17.5, 10.5);
	p.lineTo(17.5, 15.5);
	p.lineTo(19.5, 17.5);
	p.closeSubpath();
	p.addEllipse(QPointF(12.0, 4.4), 1.3, 1.3);
	p.addEllipse(QPointF(12.0, 20.0), 2.2, 1.7);
	return p;
}

QPainterPath thumbPath()
{
	QPainterPath p;
	p.addRoundedRect(QRectF(3.2, 10.4, 4.2, 10.2), 1.0, 1.0);
	p.moveTo(9.0, 10.6);
	p.lineTo(12.0, 4.4);
	p.cubicTo(13.4, 3.4, 15.0, 4.6, 14.6, 6.2);
	p.lineTo(13.7, 9.3);
	p.lineTo(18.4, 9.3);
	p.cubicTo(19.9, 9.3, 21.0, 10.7, 20.6, 12.1);
	p.lineTo(19.2, 18.7);
	p.cubicTo(18.9, 19.8, 18.0, 20.6, 16.9, 20.6);
	p.lineTo(9.0, 20.6);
	p.closeSubpath();
	return p;
}

QPainterPath sharePath()
{
	QPainterPath p;
	p.moveTo(14.0, 4.6);
	p.lineTo(20.8, 10.8);
	p.lineTo(14.0, 17.0);
	p.lineTo(14.0, 13.3);
	p.cubicTo(9.6, 13.3, 6.4, 14.8, 4.0, 19.2);
	p.cubicTo(4.6, 13.2, 8.2, 9.2, 14.0, 8.4);
	p.closeSubpath();
	return p;
}

void fillIcon(QPainter &p, const QPainterPath &path, const QRectF &box, const QColor &colour)
{
	p.save();
	p.translate(box.topLeft());
	p.scale(box.width() / 24.0, box.height() / 24.0);
	p.setPen(Qt::NoPen);
	p.setBrush(colour);
	p.drawPath(path);
	p.restore();
}

double between(double ph, double from, double length)
{
	return std::clamp((ph - from) / length, 0.0, 1.0);
}

// The mouse pointer: white with a dark edge, its tip at the origin.
void drawPointer(QPainter &p, QPointF tip, double scale, double opacity)
{
	if (opacity <= 0.0)
		return;
	QPainterPath arrow;
	arrow.moveTo(0, 0);
	arrow.lineTo(0, 24);
	arrow.lineTo(6.0, 18.6);
	arrow.lineTo(9.8, 27.4);
	arrow.lineTo(13.6, 25.8);
	arrow.lineTo(9.9, 17.3);
	arrow.lineTo(17.8, 17.3);
	arrow.closeSubpath();
	p.save();
	p.setOpacity(p.opacity() * opacity);
	p.translate(tip);
	p.scale(scale, scale);
	p.setPen(QPen(QColor(17, 17, 16), 2.4, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
	p.setBrush(QColor(255, 255, 255));
	p.drawPath(arrow);
	p.restore();
}

// 30. Subscribe + Bell: a channel card whose buttons get clicked by a cursor, one after another: Subscribe turns
// to Subscribed, the bell rings and says notifications are on, the like thumb pops, the share arrow nudges.
// The round of clicks repeats every `pulseSeconds` while the title is up.
void socialSubscribe(Ctx &c)
{
	const double h = 100, pad = 14, avatar = h - 2 * pad, circle = 54, gap = 14;
	const QString channel = c.x.line1;
	const QString message = c.x.line2;
	const QString word = (c.has(c.x.tag) ? c.x.tag.trimmed() : QStringLiteral("Subscribe")).toUpper();
	const QString doneWord = word.startsWith(QLatin1String("SUBSCRIBE")) ? QStringLiteral("SUBSCRIBED") : word;
	const QFont fname = fitFont(c, c.t.display, 40, 26, channel, 520);
	const QFont fmsg = c.font(c.t.body, 20);
	const QFont fbtn = c.font(c.t.display, 26);
	const QFont fchip = c.font(c.t.display, 20);
	const double textW = std::max(textWidth(fname, channel), c.has(message) ? textWidth(fmsg, message) : 0.0);
	const double btnW = std::max(textWidth(fbtn, word), textWidth(fbtn, doneWord)) + (word.size() + 2) * 2.0 + 64;
	const double w = pad + avatar + 22 + textW + 34 + btnW + 3 * (gap + circle) + pad;
	const double y = c.bottom() - h;
	const double e = c.part(0.0, 0.55);
	const double ph = pulsePhase(c);
	const double alive = c.out > 0.0 ? 0.0 : 1.0;

	// Where everything sits.
	const double bx = kLeft + pad + avatar + 22 + textW + 34;
	const QRectF btn(bx, y + (h - 54) / 2.0, btnW, 54);
	const QRectF bellBox(btn.right() + gap, y + (h - circle) / 2.0, circle, circle);
	const QRectF likeBox(bellBox.right() + gap, bellBox.top(), circle, circle);
	const QRectF shareBox(likeBox.right() + gap, bellBox.top(), circle, circle);

	// The clicks, as positions in the round (0..1), and the cursor's stops between them.
	const double clickSub = 0.18, clickBell = 0.40, clickLike = 0.64, clickShare = 0.84;
	const double reset = 1.0 - between(ph, 0.95, 0.04); // everything goes back to how it started
	const auto since = [&](double t0, double len) {
		return alive * between(ph, t0, len);
	};
	const auto pressAt = [&](double t0) {
		return alive * std::max(0.0, 1.0 - std::abs(ph - t0 - 0.012) / 0.03);
	};

	fillPanel(c, QRectF(kLeft, y, w * e, h), c.t.cornerRadius);
	c.p.save();
	c.p.setClipRect(QRectF(kLeft, y, w * e + 1, h));
	fillRect(c.p, QRectF(kLeft, y, w, h), c.panel, c.t.cornerRadius);

	// The channel: a round avatar with its first letter, the name, the message.
	const double pop = c.partBack(0.08, 0.45);
	const QRectF av(kLeft + pad, y + pad, avatar, avatar);
	c.p.save();
	c.p.translate(av.center());
	c.p.scale(std::max(pop, 0.001), std::max(pop, 0.001));
	c.p.translate(-av.center());
	c.p.setPen(Qt::NoPen);
	c.p.setBrush(c.accent);
	c.p.drawEllipse(av);
	QString initial;
	for (const QChar ch : channel) {
		if (ch.isLetterOrNumber()) {
			initial = QString(ch.toUpper());
			break;
		}
	}
	drawText(c.p, av, initial, c.font(c.t.display, 38), QColor(255, 255, 255), Qt::AlignCenter);
	c.p.restore();
	const double tx = kLeft + pad + avatar + 22;
	const double e1 = c.part(0.25, 0.5), e2 = c.part(0.34, 0.5);
	if (c.has(message)) {
		drawText(c.p, QRectF(tx + 20 * (1 - e1), y + 12, textW + 40, 44), channel, fname, c.text,
			 Qt::AlignLeft | Qt::AlignVCenter, e1);
		drawText(c.p, QRectF(tx + 20 * (1 - e2), y + 56, textW + 40, 30), message, fmsg, c.sub,
			 Qt::AlignLeft | Qt::AlignVCenter, e2);
	} else {
		drawText(c.p, QRectF(tx + 20 * (1 - e1), y, textW + 40, h), channel, fname, c.text,
			 Qt::AlignLeft | Qt::AlignVCenter, e1);
	}

	// The buttons pop in one after another.
	const double b0 = c.partBack(0.40, 0.35), b1 = c.partBack(0.46, 0.35), b2 = c.partBack(0.52, 0.35),
		     b3 = c.partBack(0.58, 0.35);
	const auto popped = [&](const QRectF &r, double amount, double extra, auto draw) {
		c.p.save();
		c.p.translate(r.center());
		const double s = std::max(amount, 0.001) * extra;
		c.p.scale(s, s);
		c.p.translate(-r.center());
		draw();
		c.p.restore();
	};

	// Subscribe: red, then grey with "Subscribed" once clicked.
	const double done = between(ph, clickSub + 0.012, 0.03) * reset * alive;
	popped(btn, b0, 1.0 - 0.06 * pressAt(clickSub), [&]() {
		const QColor red(0xFF, 0x00, 0x00), grey(255, 255, 255, 46);
		QColor fill(int(red.red() + (grey.red() - red.red()) * done),
			    int(red.green() + (grey.green() - red.green()) * done),
			    int(red.blue() + (grey.blue() - red.blue()) * done),
			    int(red.alpha() + (grey.alpha() - red.alpha()) * done));
		fillRect(c.p, btn, fill, 27);
		if (done > 0.5) {
			// A check mark before the word.
			const double tw = textWidth(fbtn, doneWord) + doneWord.size() * 2.0;
			const double cx = btn.center().x() - tw / 2.0 - 4;
			c.p.save();
			c.p.setPen(QPen(c.text, 3.4, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
			c.p.drawPolyline(QPolygonF({QPointF(cx - 22, btn.center().y() + 1),
						    QPointF(cx - 15, btn.center().y() + 8),
						    QPointF(cx - 3, btn.center().y() - 7)}));
			c.p.restore();
			drawText(c.p, btn.adjusted(26, 0, 0, 0), doneWord, fbtn, c.text, Qt::AlignCenter, 1.0, 2.0);
		} else {
			drawText(c.p, btn, word, fbtn, QColor(255, 255, 255), Qt::AlignCenter, 1.0, 2.0);
		}
	});

	// The round buttons.
	const auto disc = [&](const QRectF &r, const QColor &fill) {
		c.p.setPen(Qt::NoPen);
		c.p.setBrush(fill);
		c.p.drawEllipse(r);
	};

	// Bell: after its click it fills, swings a few times, and a red dot lands on it.
	const double bellOn = between(ph, clickBell + 0.012, 0.03) * reset * alive;
	const double ringU = since(clickBell + 0.012, 0.12);
	const double swing = (ringU > 0.0 && ringU < 1.0)
				     ? 26.0 * (1.0 - ringU) * (1.0 - ringU) * std::sin(ringU * 3.0 * 2.0 * kPi)
				     : 0.0;
	popped(bellBox, b1, 1.0 - 0.06 * pressAt(clickBell), [&]() {
		disc(bellBox, bellOn > 0.5 ? c.accent : QColor(255, 255, 255, 36));
		c.p.save();
		c.p.translate(bellBox.center().x(), bellBox.top() + circle * 0.28); // swings from the top
		c.p.rotate(swing);
		c.p.translate(-bellBox.center().x(), -(bellBox.top() + circle * 0.28));
		fillIcon(c.p, bellPath(), bellBox.adjusted(10, 10, -10, -10),
			 bellOn > 0.5 ? QColor(255, 255, 255) : c.text);
		c.p.restore();
	});
	const double dot = easeOutBack(since(clickBell + 0.045, 0.05)) * reset;
	if (dot > 0.01) {
		const QPointF at(bellBox.right() - 8, bellBox.top() + 8);
		c.p.save();
		c.p.translate(at);
		c.p.scale(dot, dot);
		c.p.setPen(QPen(QColor(17, 17, 16), 3.0));
		c.p.setBrush(QColor(0xFF, 0x30, 0x30));
		c.p.drawEllipse(QPointF(0, 0), 10.5, 10.5);
		c.p.restore();
		drawText(c.p, QRectF(at.x() - 10, at.y() - 10, 20, 20), QStringLiteral("1"), c.font(c.t.display, 16),
			 QColor(255, 255, 255), Qt::AlignCenter, std::clamp(dot, 0.0, 1.0));
	}
	// Like: the thumb turns to the accent colour and pops; "+1" floats up.
	const double likeOn = between(ph, clickLike + 0.012, 0.03) * reset * alive;
	const double likePop = std::sin(since(clickLike + 0.01, 0.07) * kPi);
	popped(likeBox, b2, 1.0 - 0.06 * pressAt(clickLike) + 0.22 * likePop * alive, [&]() {
		disc(likeBox, likeOn > 0.5 ? c.accent : QColor(255, 255, 255, 36));
		fillIcon(c.p, thumbPath(), likeBox.adjusted(11, 11, -11, -11),
			 likeOn > 0.5 ? QColor(255, 255, 255) : c.text);
	});
	const double plus = since(clickLike + 0.015, 0.12);
	if (plus > 0.0 && plus < 1.0) {
		drawText(c.p, QRectF(likeBox.left(), likeBox.top() - 40 - 26 * plus, circle, 34), QStringLiteral("+1"),
			 c.font(c.t.display, 28), c.accent, Qt::AlignCenter, std::sin(plus * kPi) * reset);
	}

	// Share: the arrow nudges forward and a ring spreads.
	const double shareU = since(clickShare + 0.012, 0.08);
	const double nudge = std::sin(shareU * kPi) * 7.0;
	popped(shareBox, b3, 1.0 - 0.06 * pressAt(clickShare), [&]() {
		disc(shareBox, QColor(255, 255, 255, 36 + int(60 * std::sin(shareU * kPi))));
		fillIcon(c.p, sharePath(), shareBox.adjusted(11, 11, -11, -11).translated(nudge, 0), c.text);
	});

	// Rings where a click lands.
	const auto ring = [&](double t0, QPointF at) {
		const double u = since(t0, 0.07);
		if (u <= 0.0 || u >= 1.0)
			return;
		c.p.save();
		c.p.setBrush(Qt::NoBrush);
		c.p.setPen(QPen(QColor(255, 255, 255, int(150 * (1.0 - u))), 3.0));
		c.p.drawEllipse(at, 12 + 34 * u, 12 + 34 * u);
		c.p.restore();
	};
	ring(clickSub, btn.center() + QPointF(btnW * 0.15, 6));
	ring(clickBell, bellBox.center() + QPointF(6, 8));
	ring(clickLike, likeBox.center() + QPointF(6, 8));
	ring(clickShare, shareBox.center() + QPointF(6, 8));
	c.p.restore();

	// "Notifications on" rises from the bell, then goes.
	const double chipIn = easeOutCubic(since(clickBell + 0.02, 0.04));
	const double chipOut = between(ph, clickBell + 0.15, 0.04);
	const double chipA = chipIn * (1.0 - chipOut);
	if (chipA > 0.01 && e >= 0.98) {
		const QString note = QStringLiteral("NOTIFICATIONS ON");
		const double nw = textWidth(fchip, note) + note.size() * 1.5 + 36;
		const QRectF chip(bellBox.center().x() - nw / 2.0, bellBox.top() - 52 - 8 * chipIn, nw, 38);
		c.p.save();
		c.p.setOpacity(c.p.opacity() * chipA);
		fillRect(c.p, chip, QColor(241, 235, 221), 19);
		drawText(c.p, chip, note, fchip, QColor(17, 17, 16), Qt::AlignCenter, 1.0, 1.5);
		c.p.restore();
	}

	// The cursor, drawn last so it can pass over everything. Its tip is where it clicks.
	if (e >= 0.98 && alive > 0.0) {
		const QPointF start(kLeft + w + 30, y + h + 70), end(kLeft + w + 30, y + h + 70);
		const QPointF pSub = btn.center() + QPointF(btnW * 0.15, 6), pBell = bellBox.center() + QPointF(6, 8),
			      pLike = likeBox.center() + QPointF(6, 8), pShare = shareBox.center() + QPointF(6, 8);
		struct Stop {
			double from, to;
			QPointF a, b;
		};
		const Stop legs[] = {{0.05, 0.16, start, pSub},
				     {0.28, 0.38, pSub, pBell},
				     {0.52, 0.62, pBell, pLike},
				     {0.72, 0.82, pLike, pShare},
				     {0.88, 0.96, pShare, end}};
		QPointF at = start;
		for (const Stop &leg : legs) {
			if (ph >= leg.from) {
				const double u = easeInOutCubic(between(ph, leg.from, leg.to - leg.from));
				at = leg.a + (leg.b - leg.a) * u;
			}
		}
		const double fade = between(ph, 0.03, 0.04) * (1.0 - between(ph, 0.94, 0.04));
		const double press =
			std::max({pressAt(clickSub), pressAt(clickBell), pressAt(clickLike), pressAt(clickShare)});
		drawPointer(c.p, at, 1.0 - 0.14 * press, fade);
	}
}

// ---- News: tabbed headline bars -------------------------------------------------------------------------

QColor darker(const QColor &c, int percent)
{
	return c.darker(percent);
}

// A tab sitting on top of a bar: a label in a small solid block.
void drawTab(Ctx &c, const QRectF &rect, const QString &label, const QFont &font, const QColor &fill, const QColor &ink,
	     double amount)
{
	if (rect.width() <= 0.0)
		return;
	c.p.save();
	c.p.translate(0, -16 * (1.0 - amount));
	fillRect(c.p, rect, fill);
	drawText(c.p, rect, label, font, ink, Qt::AlignCenter, amount, 1.5);
	c.p.restore();
}

// 28. News Flash: an amber tab over a blue headline bar.
void newsFlash(Ctx &c)
{
	const double padX = 34, h = 92, tabH = 44;
	const QFont f1 = fitFont(c, c.t.display, 60, 38, c.x.line1.toUpper(), 1300);
	const QFont ft = c.font(c.t.display, 30);
	const QString tag = c.x.tag.toUpper();
	const double w = std::max(560.0, 2 * padX + textWidth(f1, c.x.line1.toUpper()) + 40);
	const double y = c.bottom() - h;
	const double pw = w * c.part(0.0, 0.6);
	if (c.has(tag))
		drawTab(c, QRectF(kLeft, y - tabH, textWidth(ft, tag) + 2 * 26 + tag.size() * 1.5, tabH), tag, ft,
			c.accent, c.sub, c.part(0.25, 0.4));
	QLinearGradient g(kLeft, 0, kLeft + w, 0);
	g.setColorAt(0.0, c.panel);
	g.setColorAt(1.0, darker(c.panel, 170));
	c.p.save();
	c.p.setPen(Qt::NoPen);
	c.p.setBrush(g);
	c.p.drawRect(QRectF(kLeft, y, pw, h));
	c.p.setClipRect(QRectF(kLeft, y, pw, h));
	const double e = c.part(0.2, 0.5);
	drawText(c.p, QRectF(kLeft + padX + 24 * (1 - e), y, w, h), c.x.line1.toUpper(), f1, c.text,
		 Qt::AlignLeft | Qt::AlignVCenter, e);
	c.p.restore();
}

// 29. News Report: a red tab, a light bar with headline and detail, and a red LIVE block on the right.
void newsReport(Ctx &c)
{
	const double padX = 30, h = 116, tabH = 44, blockW = 200;
	const bool two = c.has(c.x.line2);
	const QFont f1 = fitFont(c, c.t.display, 62, 38, c.x.line1.toUpper(), 1200);
	const QFont f2 = fitFont(c, c.t.body, 26, 20, c.x.line2.toUpper(), 1200);
	const QFont ft = c.font(c.t.display, 30);
	const QFont fw = c.font(c.t.display, 38);
	const QString tag = c.x.tag.toUpper();
	const double w = std::max(
		900.0,
		2 * padX + std::max(textWidth(f1, c.x.line1.toUpper()), textWidth(f2, c.x.line2.toUpper())) + blockW);
	const double y = c.bottom() - h;
	const double pw = w * c.part(0.0, 0.6);
	if (c.has(tag))
		drawTab(c, QRectF(kLeft, y - tabH, textWidth(ft, tag) + 2 * 26 + tag.size() * 1.5, tabH), tag, ft,
			c.accent, QColor(255, 255, 255), c.part(0.25, 0.4));
	c.p.save();
	c.p.setClipRect(QRectF(kLeft, y, pw, h + 6));
	QLinearGradient g(0, y, 0, y + h);
	g.setColorAt(0.0, c.panel.lighter(112));
	g.setColorAt(1.0, c.panel.darker(118));
	c.p.setPen(Qt::NoPen);
	c.p.setBrush(g);
	c.p.drawRect(QRectF(kLeft, y, w, h));
	fillRect(c.p, QRectF(kLeft, y + h - 4, w - blockW, 4), c.accent);
	const double e1 = c.part(0.3, 0.5), e2 = c.part(0.4, 0.5);
	const double tx = kLeft + padX;
	if (two) {
		drawText(c.p, QRectF(tx + 24 * (1 - e1), y + 6, w, 70), c.x.line1.toUpper(), f1, c.text,
			 Qt::AlignLeft | Qt::AlignVCenter, e1);
		drawText(c.p, QRectF(tx + 24 * (1 - e2), y + 72, w, 34), c.x.line2.toUpper(), f2, c.sub,
			 Qt::AlignLeft | Qt::AlignVCenter, e2);
	} else {
		drawText(c.p, QRectF(tx + 24 * (1 - e1), y, w, h), c.x.line1.toUpper(), f1, c.text,
			 Qt::AlignLeft | Qt::AlignVCenter, e1);
	}
	const QRectF block(kLeft + w - blockW, y, blockW, h);
	fillRect(c.p, block, c.accent);
	drawLiveDot(c, QPointF(block.left() + 44, block.center().y()), 10, QColor(255, 255, 255), c.part(0.5, 0.4));
	drawText(c.p, QRectF(block.left() + 70, y, blockW - 70, h), QStringLiteral("LIVE"), fw, c.ink,
		 Qt::AlignLeft | Qt::AlignVCenter, c.part(0.5, 0.4), 2.0);
	c.p.restore();
}

// 30. News Desk: a blue LIVE block, a white headline bar with a tab above it and a detail strip under it.
void newsDesk(Ctx &c)
{
	const double padX = 36, h = 96, blockW = 230, stripH = 38, tabH = 44;
	const bool strip = c.has(c.x.line2);
	const QFont f1 = fitFont(c, c.t.display, 62, 38, c.x.line1.toUpper(), 1200);
	const QFont f2 = c.font(c.t.body, 22);
	const QFont ft = c.font(c.t.display, 30);
	const QFont fw = c.font(c.t.display, 42);
	const QString tag = c.x.tag.toUpper();
	const double totalH = h + (strip ? stripH : 0.0);
	const double w = std::max(900.0, blockW + 2 * padX + textWidth(f1, c.x.line1.toUpper()));
	const double y = c.bottom() - totalH;
	const double pw = w * c.part(0.0, 0.6);
	if (c.has(tag))
		drawTab(c, QRectF(kLeft + blockW, y - tabH, textWidth(ft, tag) + 2 * 26 + tag.size() * 1.5, tabH), tag,
			ft, c.accent, QColor(255, 255, 255), c.part(0.25, 0.4));
	c.p.save();
	c.p.setClipRect(QRectF(kLeft, y, pw, totalH));
	fillRect(c.p, QRectF(kLeft + blockW, y, w - blockW, h), c.panel);
	fillRect(c.p, QRectF(kLeft, y, blockW, totalH), c.accent.darker(125));
	drawLiveDot(c, QPointF(kLeft + 58, y + h / 2), 12, QColor(255, 255, 255), c.part(0.2, 0.4));
	drawText(c.p, QRectF(kLeft + 88, y, blockW - 88, h), QStringLiteral("LIVE"), fw, c.ink,
		 Qt::AlignLeft | Qt::AlignVCenter, c.part(0.25, 0.4), 2.0);
	const double e1 = c.part(0.3, 0.5);
	drawText(c.p, QRectF(kLeft + blockW + padX + 24 * (1 - e1), y, w, h), c.x.line1.toUpper(), f1, c.text,
		 Qt::AlignLeft | Qt::AlignVCenter, e1);
	if (strip) {
		const double e2 = c.part(0.4, 0.5);
		fillRect(c.p, QRectF(kLeft + blockW, y + h, w - blockW, stripH), c.accent.lighter(135));
		drawText(c.p, QRectF(kLeft + blockW + padX + 24 * (1 - e2), y + h, w, stripH),
			 QStringLiteral("\u25B6  ") + c.x.line2.toUpper(), f2, c.sub, Qt::AlignLeft | Qt::AlignVCenter,
			 e2, 1.0);
	}
	c.p.restore();
}

// ---- Guest and giving cards -------------------------------------------------------------------------------

// Logo pictures are read from disk once and kept (per thread), at a size that is plenty for a title.
QImage logoImage(const QString &path)
{
	if (path.isEmpty())
		return QImage();
	thread_local QHash<QString, QImage> cache;
	const QFileInfo info(path);
	const QString key = path + QLatin1Char('|') + QString::number(info.lastModified().toSecsSinceEpoch()) +
			    QLatin1Char('|') + QString::number(info.size());
	const auto found = cache.constFind(key);
	if (found != cache.constEnd())
		return found.value();
	if (cache.size() > 8)
		cache.clear();
	QImage img(path);
	if (!img.isNull() && (img.width() > 900 || img.height() > 900))
		img = img.scaled(900, 900, Qt::KeepAspectRatio, Qt::SmoothTransformation);
	if (!img.isNull())
		img = img.convertToFormat(QImage::Format_ARGB32_Premultiplied);
	cache.insert(key, img);
	return img;
}

// A logo, fitted inside `box` and centred.
void drawLogo(QPainter &p, const QImage &logo, const QRectF &box, double opacity)
{
	if (logo.isNull() || opacity <= 0.0)
		return;
	const double s = std::min(box.width() / logo.width(), box.height() / logo.height());
	const QSizeF size(logo.width() * s, logo.height() * s);
	const QRectF target(box.center().x() - size.width() / 2, box.center().y() - size.height() / 2, size.width(),
			    size.height());
	p.save();
	p.setOpacity(p.opacity() * opacity);
	p.setRenderHint(QPainter::SmoothPixmapTransform, true);
	p.drawImage(target, logo);
	p.restore();
}

// A small heart, for the "giving" tab.
void drawHeart(QPainter &p, const QPointF &centre, double size, const QColor &colour)
{
	QPainterPath path;
	const double s = size / 2.0;
	path.moveTo(centre.x(), centre.y() + s * 0.9);
	path.cubicTo(centre.x() - s * 1.6, centre.y() - s * 0.2, centre.x() - s * 0.7, centre.y() - s * 1.2, centre.x(),
		     centre.y() - s * 0.35);
	path.cubicTo(centre.x() + s * 0.7, centre.y() - s * 1.2, centre.x() + s * 1.6, centre.y() - s * 0.2, centre.x(),
		     centre.y() + s * 0.9);
	p.save();
	p.setPen(Qt::NoPen);
	p.setBrush(colour);
	p.drawPath(path);
	p.restore();
}

// 31. Guest Minister: an orange tab over a blue bar (logo and name), a white tab under it.
void guestMinister(Ctx &c)
{
	const double h = 100, padX = c.pad(30), tabH = 42, tab2H = 38;
	const QImage logo = logoImage(c.x.logoPath);
	const double logoW = logo.isNull() ? 0.0 : h - 24;
	const QString name = c.x.line1.toUpper();
	const double chrome = 2 * padX + (logo.isNull() ? 0.0 : logoW + 26);
	const QFont f1 = fitFontFor(c, c.t.display, 66, 38, name, 1150, chrome);
	const QFont ft = c.font(c.t.display, 28);
	const QFont fb = c.font(c.t.display, 26);
	const double w = shapeWidth(c, 520.0, chrome, textWidth(f1, name));
	const bool hasBottom = c.has(c.x.line2);
	const double y = c.bottom() - h - (hasBottom ? tab2H : 0.0);
	const double e = c.part(0.0, 0.6);
	QLinearGradient g(kLeft, 0, kLeft + w, 0);
	g.setColorAt(0.0, c.panel);
	g.setColorAt(1.0, c.panel.darker(150));
	c.p.save();
	c.p.setPen(Qt::NoPen);
	c.p.setBrush(g);
	fillPanel(c, QRectF(kLeft, y, w * e, h));
	c.p.drawRect(QRectF(kLeft, y, w * e, h));
	c.p.setClipRect(QRectF(kLeft, y, w * e, h));
	double tx = kLeft + padX;
	if (!logo.isNull()) {
		drawLogo(c.p, logo, QRectF(tx, y + 12, logoW, h - 24), c.part(0.15, 0.4));
		tx += logoW + 26;
	}
	drawText(c.p, QRectF(tx + 24 * (1 - c.part(0.25, 0.5)), y, w, h), name, f1, c.text,
		 Qt::AlignLeft | Qt::AlignVCenter, c.part(0.25, 0.5));
	c.p.restore();
	const QString tag = c.x.tag.toUpper();
	if (c.has(tag)) {
		const double tw = textWidth(ft, tag) + 2 * 28 + tag.size() * 1.5;
		const double tabX = kLeft + std::min(w - tw, padX + (logo.isNull() ? 20.0 : logoW + 34));
		drawTab(c, QRectF(tabX, y - tabH, tw, tabH), tag, ft, c.accent, c.palette_ink(), c.part(0.25, 0.4));
	}
	if (hasBottom) {
		const QString note = c.x.line2.toUpper();
		const double tw = textWidth(fb, note) + 2 * 30 + note.size() * 1.5;
		const double a = c.part(0.4, 0.4);
		c.p.save();
		c.p.translate(0, 12 * (1.0 - a));
		fillRect(c.p, QRectF(kLeft + (w - tw) / 2, y + h, tw, tab2H), c.shape2);
		drawText(c.p, QRectF(kLeft + (w - tw) / 2, y + h, tw, tab2H), note, fb, c.ink, Qt::AlignCenter, a, 1.5);
		c.p.restore();
	}
}

// 32. Giving: where to give. The account holder, then one column per account the operator added; the bar
// grows with them and the numbers shrink to keep everything on the screen.
void giving(Ctx &c)
{
	QList<QPair<QString, QString>> rows;
	for (const auto &r : c.x.rows)
		if (c.has(r.second))
			rows.append(r);
	const double h = 116, tabH = 44, noteH = 36, padX = 30, maxTotal = kDesignWidth - 2 * kLeft;
	const QImage logo = logoImage(c.x.logoPath);
	const double blockW = logo.isNull() ? 0.0 : h;
	const bool hasName = c.has(c.x.line1) || c.has(c.x.line2);
	const QString bank = c.x.line2.toUpper();
	const QFont fBank = c.font(c.t.body, 20);
	const Wrapped name = c.has(c.x.line1) ? fitWrapped(c, c.t.display, 38, 24, c.x.line1.toUpper(), 340, 2, 1.0)
					      : Wrapped();
	double nameW = 0.0;
	if (hasName) {
		nameW = std::max(textWidth(fBank, bank) + bank.size() * 2.5, 0.0);
		for (const QString &line : name.lines)
			nameW = std::max(nameW, textWidth(name.font, line));
		nameW += 2 * padX;
	}
	// The numbers shrink, one step at a time, until the whole bar fits.
	double px = 68.0;
	QFont fNum = c.font(c.t.display, px);
	const QFont fLabel = c.font(c.t.body, 20);
	std::vector<double> colW;
	double total = 0.0;
	const auto measure = [&]() {
		colW.clear();
		total = blockW + nameW;
		for (const auto &r : rows) {
			const double wCol = std::max(textWidth(fNum, r.second),
						     textWidth(fLabel, r.first.toUpper()) + r.first.size() * 2.5) +
					    2 * 28.0;
			colW.push_back(wCol);
			total += wCol;
		}
	};
	measure();
	while (total > maxTotal && px > 28.0) {
		px *= 0.94;
		fNum = c.font(c.t.display, px);
		measure();
	}
	if (total < 460.0)
		total = 460.0; // an empty card still has a body
	const double y = c.bottom() - h - (c.has(c.x.note) ? noteH : 0.0);
	const double e = c.part(0.0, 0.6);
	const QRectF bar(kLeft, y, total, h);
	QLinearGradient g(bar.left(), 0, bar.right(), 0);
	g.setColorAt(0.0, c.panel);
	g.setColorAt(0.5, c.panel.lighter(112));
	g.setColorAt(1.0, c.panel.darker(150));
	fillPanel(c, QRectF(kLeft, y, total * e, h));
	c.p.save();
	c.p.setClipRect(QRectF(kLeft, y, total * e, h));
	c.p.setPen(Qt::NoPen);
	c.p.setBrush(g);
	c.p.drawRect(bar);
	fillRect(c.p, QRectF(kLeft, y + h - 4, total, 4), c.sub);
	double x = kLeft;
	if (!logo.isNull()) {
		fillRect(c.p, QRectF(x, y, blockW, h - 4), c.shape3);
		drawLogo(c.p, logo, QRectF(x + 12, y + 12, blockW - 24, h - 28), c.part(0.1, 0.4));
		x += blockW;
	}
	if (hasName) {
		const double en = c.part(0.2, 0.5);
		if (c.has(bank))
			drawText(c.p, QRectF(x + padX, y + 12, nameW, 26), bank, fBank, c.sub,
				 Qt::AlignLeft | Qt::AlignVCenter, en, 2.5);
		const double top = y + (c.has(bank) ? 40 : 0);
		const double lh = name.lineHeight;
		const double blockH = lh * name.lines.size();
		const double avail = (y + h - 4) - top;
		for (int i = 0; i < name.lines.size(); ++i)
			drawText(c.p, QRectF(x + padX, top + (avail - blockH) / 2 + i * lh, nameW, lh), name.lines[i],
				 name.font, c.text, Qt::AlignLeft | Qt::AlignVCenter, en);
		x += nameW;
	}
	for (int i = 0; i < rows.size(); ++i) {
		const double ec = c.part(0.3 + 0.08 * i, 0.4);
		fillRect(c.p, QRectF(x, y + 16, 1.5, h - 36), QColor(255, 255, 255, 40));
		drawText(c.p, QRectF(x + 28, y + 14, colW[i], 28), rows[i].first.toUpper(), fLabel, c.sub,
			 Qt::AlignLeft | Qt::AlignVCenter, ec, 2.5);
		drawText(c.p, QRectF(x + 28, y + 38 + 6 * (1 - ec), colW[i], h - 46), rows[i].second, fNum, c.text,
			 Qt::AlignLeft | Qt::AlignVCenter, ec);
		x += colW[i];
	}
	c.p.restore();
	// The tab above the bar: a heart and the words.
	const QString tag = c.x.tag.toUpper();
	if (c.has(tag)) {
		const QFont ft = c.font(c.t.body, 24);
		const double tw = textWidth(ft, tag) + tag.size() * 4.0 + 2 * 28 + 40;
		const double a = c.part(0.25, 0.4);
		const QRectF tab(kLeft + (total - tw) / 2, y - tabH, tw, tabH);
		c.p.save();
		c.p.translate(0, -16 * (1.0 - a));
		fillRect(c.p, tab, c.accent);
		c.p.setOpacity(c.p.opacity() * a);
		drawHeart(c.p, QPointF(tab.left() + 30, tab.center().y()), 20, c.ink);
		drawText(c.p, QRectF(tab.left() + 52, tab.top(), tab.width() - 60, tabH), tag, ft, c.ink,
			 Qt::AlignLeft | Qt::AlignVCenter, 1.0, 4.0);
		c.p.restore();
	}
	// The note tab under the bar.
	if (c.has(c.x.note)) {
		const QFont fn = c.font(c.t.body, 22);
		const QString note = c.x.note.toUpper();
		const double tw = textWidth(fn, note) + note.size() * 4.0 + 2 * 30;
		const double a = c.part(0.45, 0.4);
		const QRectF tab(kLeft + (total - tw) / 2, y + h, tw, noteH);
		c.p.save();
		c.p.translate(0, 12 * (1.0 - a));
		fillRect(c.p, tab, c.shape2);
		drawText(c.p, tab, note, fn, c.ink, Qt::AlignCenter, a, 4.0);
		c.p.restore();
	}
}

// The scrolling strip shared by both tickers.
// A ticker's text is a list of items: pressing Enter while typing starts a new one. When there is more than one, a
// small accent diamond sits between them, so it is clear that what follows is new.
constexpr double kTickerGap = 180.0;     // one item on its own: the space before it repeats
constexpr double kTickerItemGap = 120.0; // several items: the space between them, with the diamond in the middle

QStringList tickerItems(const QString &raw)
{
	QStringList items;
	for (const QString &line : raw.split(QLatin1Char('\n'))) {
		const QString item = line.simplified();
		if (!item.isEmpty())
			items << item;
	}
	return items;
}

double tickerStripLength(const QFont &font, const QStringList &items)
{
	if (items.isEmpty())
		return 0.0;
	if (items.size() == 1)
		return textWidth(font, items.first()) + kTickerGap;
	double total = 0.0;
	for (const QString &item : items)
		total += textWidth(font, item) + kTickerItemGap;
	return total;
}

void drawTickerTextOf(Ctx &c, const QString &raw, const QRectF &area, const QFont &font, const QColor &colour)
{
	const QStringList items = tickerItems(raw);
	if (items.isEmpty())
		return;
	const bool marked = items.size() > 1;
	const double strip = tickerStripLength(font, items);
	const double gap = marked ? kTickerItemGap : kTickerGap;
	const double mark = QFontMetricsF(font).height() * 0.17;
	c.p.save();
	c.p.setClipRect(area);
	double xPos = area.left() - std::fmod(c.tickerPx, strip);
	while (xPos < area.right()) {
		for (const QString &item : items) {
			const double tw = textWidth(font, item);
			if (xPos + tw >= area.left() - 4 && xPos <= area.right())
				drawText(c.p, QRectF(xPos, area.top(), tw + 4, area.height()), item, font, colour,
					 Qt::AlignLeft | Qt::AlignVCenter);
			xPos += tw;
			if (marked) {
				const QPointF at(xPos + gap / 2.0, area.center().y());
				QPolygonF diamond;
				diamond << QPointF(at.x(), at.y() - mark) << QPointF(at.x() + mark, at.y())
					<< QPointF(at.x(), at.y() + mark) << QPointF(at.x() - mark, at.y());
				c.p.save();
				c.p.setPen(Qt::NoPen);
				c.p.setBrush(c.accent);
				c.p.drawPolygon(diamond);
				c.p.restore();
			}
			xPos += gap;
			if (xPos >= area.right())
				break;
		}
	}
	c.p.restore();
}

void drawTickerText(Ctx &c, const QRectF &area, const QFont &font, const QColor &colour)
{
	drawTickerTextOf(c, c.x.line1, area, font, colour);
}

// 18. Crawl Ticker: a full-width bar with a label block and scrolling text.
void crawlTicker(Ctx &c)
{
	const double h = 84;
	const double y = c.H - 44 - h;
	const QFont ft = c.font(c.t.body, 30);
	const QString tag = (c.has(c.x.tag) ? c.x.tag : QStringLiteral("Updates")).toUpper();
	const double tagW = textWidth(ft, tag) + 2 * 40 + tag.size() * 3.0;
	fillPanel(c, QRectF(0, y, c.W, h));
	fillRect(c.p, QRectF(0, y, tagW, h), c.accent);
	drawText(c.p, QRectF(0, y, tagW, h), tag, ft, c.sub, Qt::AlignCenter, 1.0, 3.0);
	drawTickerText(c, QRectF(tagW + 30, y, c.W - tagW - 30, h), c.font(c.t.display, 42), c.text);
}

// 19. Headline Ticker: a thin tag strip over a taller scrolling bar.
void headlineTicker(Ctx &c)
{
	const double hTop = 40, hBar = 76;
	const double y = c.H - 44 - hTop - hBar;
	const QFont ft = c.font(c.t.body, 24);
	const QString tag = (c.has(c.x.tag) ? c.x.tag : QStringLiteral("Headlines")).toUpper();
	const double tagW = textWidth(ft, tag) + 2 * 30 + tag.size() * 3.0;
	fillRect(c.p, QRectF(kLeft - 40, y, tagW, hTop), c.accent);
	drawText(c.p, QRectF(kLeft - 40, y, tagW, hTop), tag, ft, c.sub, Qt::AlignCenter, 1.0, 3.0);
	fillPanel(c, QRectF(0, y + hTop, c.W, hBar));
	drawTickerText(c, QRectF(kLeft - 40, y + hTop, c.W - 2 * (kLeft - 40), hBar), c.font(c.t.display, 40), c.text);
}

// 20. Big Quote: a pull quote with an oversized quotation mark and the speaker below.
void bigQuote(Ctx &c)
{
	const double w = 1260;
	const Wrapped q = fitWrapped(c, c.t.display, 58, 38, c.x.line1, w, 4, 1.34);
	const QFont fa = c.font(c.t.body, 38);
	const double attrH = c.has(c.x.line2) ? 62 : 0;
	const double h = q.lines.size() * q.lineHeight + attrH;
	const double top = c.bottom() - h;
	const double eq = c.part(0.0, 0.5);
	drawShadowText(c.p, QRectF(kLeft - 14, top - 118 + 16 * (1 - eq), 200, 190), QStringLiteral("“"),
		       c.font(c.t.display, 230), withOpacity(c.accent, 0.95), Qt::AlignLeft | Qt::AlignTop, eq);
	for (int i = 0; i < q.lines.size(); ++i) {
		const double e = c.part(0.15 + 0.06 * i, 0.5);
		drawShadowText(c.p, QRectF(kLeft + 6, top + i * q.lineHeight + 14 * (1 - e), w, q.lineHeight),
			       q.lines[i], q.font, c.text, Qt::AlignLeft | Qt::AlignVCenter, e);
	}
	if (attrH > 0) {
		const double e = c.part(0.4, 0.5);
		drawShadowText(c.p, QRectF(kLeft + 6, top + q.lines.size() * q.lineHeight + 8, w, attrH - 8),
			       QStringLiteral("—  ") + c.x.line2.toUpper(), fa, c.accent,
			       Qt::AlignLeft | Qt::AlignVCenter, e, 3.0);
	}
}

// ======================================================================================================
// More lower thirds: modern rounded cards, a glass bar, "desk" news bars, network bars, an angular bar,
// logo animations and sermon notes.
// ======================================================================================================

// A soft shadow under a shape, without filling it.
void shadowUnder(Ctx &c, const QRectF &r, double radius)
{
	for (int i = 4; i >= 1; --i)
		fillRect(c.p, r.adjusted(-i * 2.0, -i * 1.0 + 6.0, i * 2.0, i * 2.0 + 6.0),
			 QColor(0, 0, 0, 10 + (5 - i) * 2), radius + i * 2.0);
}

// A shape with a soft shadow and a fill.
void solidShape(Ctx &c, const QRectF &r, const QColor &fill, double radius)
{
	if (r.width() <= 0.0 || r.height() <= 0.0)
		return;
	shadowUnder(c, r, radius);
	fillRect(c.p, r, fill, radius);
}

// The words of `s` on two lines at most ("LIVE NEWS" -> "LIVE", "NEWS").
QStringList twoLines(const QString &s)
{
	const QStringList words = s.simplified().split(' ', Qt::SkipEmptyParts);
	if (words.size() <= 1)
		return words;
	const int half = int(words.size() + 1) / 2;
	return {words.mid(0, half).join(' '), words.mid(half).join(' ')};
}

// The logo, or a stand-in so a template looks finished before a picture is chosen. Draws on `p`.
void drawLogoOrMarkOn(QPainter &p, const Ctx &c, const QImage &logo, const QRectF &box, const QColor &markFill,
		      const QColor &markInk, double opacity)
{
	if (!logo.isNull()) {
		drawLogo(p, logo, box, opacity);
		return;
	}
	p.save();
	p.setOpacity(p.opacity() * opacity);
	p.setPen(Qt::NoPen);
	p.setBrush(markFill);
	const double d = std::min(box.width(), box.height());
	const QRectF circle(box.center().x() - d / 2, box.center().y() - d / 2, d, d);
	p.drawEllipse(circle);
	drawText(p, circle, QStringLiteral("LOGO"), c.font(FontFace::OutfitExtraBold, d * 0.26), markInk,
		 Qt::AlignCenter);
	p.restore();
}

void drawLogoOrMark(Ctx &c, const QImage &logo, const QRectF &box, const QColor &markFill, const QColor &markInk,
		    double opacity)
{
	drawLogoOrMarkOn(c.p, c, logo, box, markFill, markInk, opacity);
}

QString clockText()
{
	return QTime::currentTime().toString(QStringLiteral("hh:mm AP"));
}

// ---- Modern: rounded, friendly cards (orange and cream) ---------------------------------------------

// Modern Pill: an orange name pill with a cream role tab tucked under it.
void modernPill(Ctx &c)
{
	const double padX = c.pad(40);
	const QFont f1 = fitFontFor(c, c.t.display, 58, 34, c.x.line1, 1100, 2 * padX);
	const QFont f2 = c.font(c.t.body, 30);
	const double h = 88, tabH = 50;
	const double w = shapeWidth(c, 0.0, 2 * padX, textWidth(f1, c.x.line1));
	const bool two = c.has(c.x.line2);
	const double y = c.bottom() - h - (two ? tabH + 6 : 0.0);
	const double e = c.part(0.0, 0.55);
	c.p.save();
	c.p.translate(kLeft + w * 0.5, 0);
	c.p.scale(std::max(e, 0.001), 1.0);
	c.p.translate(-(kLeft + w * 0.5), 0);
	solidShape(c, QRectF(kLeft, y, w, h), c.panel, 24);
	c.p.restore();
	drawText(c.p, QRectF(kLeft + padX, y, w, h), c.x.line1, f1, c.text, Qt::AlignLeft | Qt::AlignVCenter,
		 c.part(0.2, 0.5));
	if (two) {
		const double tw = textWidth(f2, c.x.line2) + 2 * 28;
		const double a = c.partBack(0.3, 0.45);
		const QRectF tab(kLeft + 56, y + h + 6, tw, tabH);
		c.p.save();
		c.p.translate(tab.center());
		c.p.scale(std::max(a, 0.001), std::max(a, 0.001));
		c.p.translate(-tab.center());
		fillRect(c.p, tab.translated(3, 4), c.panel, 14); // the offset edge behind the tab
		fillRect(c.p, tab, c.shape2, 14);
		drawText(c.p, tab, c.x.line2, f2, c.ink, Qt::AlignCenter);
		c.p.restore();
	}
}

// Modern Badge: a round logo, the name in big type, and the role inside an orange pill under it.
void modernBadge(Ctx &c)
{
	const QImage logo = logoImage(c.x.logoPath);
	const double d = 104, pillH = 56;
	const QFont f1 = fitFont(c, c.t.display, 56, 34, c.x.line1, 900);
	const QFont f2 = c.font(c.t.body, 28);
	const double nameW = textWidth(f1, c.x.line1);
	const double pillW = std::max({340.0, nameW + d * 0.5, textWidth(f2, c.x.line2) + d + 70});
	const double pillY = c.bottom() - pillH;
	const double e = c.part(0.0, 0.55);
	c.p.save();
	c.p.setClipRect(QRectF(0, 0, kLeft + (pillW + d) * e, c.H));
	solidShape(c, QRectF(kLeft + d * 0.35, pillY, pillW, pillH), c.panel, pillH / 2);
	drawText(c.p, QRectF(kLeft + d + 14, pillY, pillW, pillH), c.x.line2, f2, c.sub,
		 Qt::AlignLeft | Qt::AlignVCenter, c.part(0.3, 0.5));
	c.p.restore();
	const QRectF badge(kLeft, pillY - d * 0.62, d, d);
	const double pop = c.partBack(0.1, 0.45);
	c.p.save();
	c.p.translate(badge.center());
	c.p.scale(std::max(pop, 0.001), std::max(pop, 0.001));
	c.p.translate(-badge.center());
	shadowUnder(c, badge, d / 2);
	c.p.setPen(Qt::NoPen);
	c.p.setBrush(c.shape2);
	c.p.drawEllipse(badge);
	drawLogoOrMark(c, logo, badge.adjusted(14, 14, -14, -14), c.panel, c.shape2, 1.0);
	c.p.restore();
	drawText(c.p, QRectF(kLeft + d + 14, pillY - 68, nameW + 40, 60), c.x.line1, f1, c.text,
		 Qt::AlignLeft | Qt::AlignVCenter, c.part(0.25, 0.5));
}

// Modern Card: a big cream card with an orange block offset behind it.
void modernCard(Ctx &c)
{
	const double pad = c.pad(56), h = c.has(c.x.line2) ? 200 : 150;
	const QFont f1 = fitFontFor(c, c.t.display, 68, 38, c.x.line1, 1000, 2 * pad);
	const QFont f2 = fitFontFor(c, c.t.body, 38, 24, c.x.line2, 1000, 2 * pad);
	const double w = shapeWidth(c, 620.0, 2 * pad, std::max(textWidth(f1, c.x.line1), textWidth(f2, c.x.line2)));
	const double y = c.bottom() - h - 24;
	const double e = c.part(0.0, 0.6);
	const QRectF card(kLeft, y, w, h);
	c.p.save();
	c.p.setClipRect(QRectF(0, 0, kLeft + (w + 30) * e, c.H));
	fillRect(c.p, card.translated(24, 24), c.accent, 38);
	solidShape(c, card, c.panel, 38);
	c.p.restore();
	if (c.has(c.x.line2)) {
		drawText(c.p, QRectF(kLeft + pad, y + 30, w, 84), c.x.line1, f1, c.text,
			 Qt::AlignLeft | Qt::AlignVCenter, c.part(0.25, 0.5));
		drawText(c.p, QRectF(kLeft + pad, y + 112, w, 60), c.x.line2, f2, c.sub,
			 Qt::AlignLeft | Qt::AlignVCenter, c.part(0.35, 0.5));
	} else {
		drawText(c.p, QRectF(kLeft + pad, y, w, h), c.x.line1, f1, c.text, Qt::AlignLeft | Qt::AlignVCenter,
			 c.part(0.25, 0.5));
	}
}

// Modern Stack: the name in huge stacked type with the role on a tag across the join.
void modernStack(Ctx &c)
{
	const QStringList lines = twoLines(c.x.line1);
	QString longest;
	for (const QString &l : lines)
		if (l.size() > longest.size())
			longest = l;
	const QFont f1 = fitFont(c, c.t.display, 104, 48, longest, 760);
	const double lh = QFontMetricsF(f1).height() * 0.86;
	const QFont f2 = c.font(c.t.body, 28);
	const double blockH = lh * std::max<int>(1, int(lines.size()));
	const double tagH = 44, tagRoom = c.has(c.x.line2) ? tagH + 10 : 0.0;
	const double top = c.bottom() - blockH - tagRoom;
	for (int i = 0; i < lines.size(); ++i) {
		const double a = c.part(0.1 + 0.15 * i, 0.5);
		drawShadowText(c.p, QRectF(kLeft + (i ? 56 : 0) + 30 * (1 - a), top + i * lh, 900, lh), lines[i], f1,
			       c.text, Qt::AlignLeft | Qt::AlignVCenter, a);
	}
	if (c.has(c.x.line2)) {
		const double tw = textWidth(f2, c.x.line2) + 2 * 22;
		const double a = c.partBack(0.4, 0.4);
		const QRectF tag(kLeft + 4, top + blockH + 6, tw, tagH);
		c.p.save();
		c.p.translate(tag.center());
		c.p.scale(std::max(a, 0.001), std::max(a, 0.001));
		c.p.translate(-tag.center());
		fillRect(c.p, tag, c.accent, 12);
		drawText(c.p, tag, c.x.line2, f2, c.sub, Qt::AlignCenter);
		c.p.restore();
	}
}

// Modern Logo: a round logo above an orange name pill, the role under it.
void modernLogo(Ctx &c)
{
	const QImage logo = logoImage(c.x.logoPath);
	const double d = 120, pillH = 74;
	const QFont f1 = fitFont(c, c.t.display, 46, 30, c.x.line1, 900);
	const QFont f2 = c.font(c.t.body, 28);
	const double w = std::max(380.0, textWidth(f1, c.x.line1) + 2 * 50);
	const bool two = c.has(c.x.line2);
	const double pillY = c.bottom() - pillH - (two ? 50 : 0.0);
	const double cx = kLeft + w / 2;
	const double e = c.part(0.0, 0.55);
	c.p.save();
	c.p.translate(cx, 0);
	c.p.scale(std::max(e, 0.001), 1.0);
	c.p.translate(-cx, 0);
	solidShape(c, QRectF(kLeft, pillY, w, pillH), c.panel, pillH / 2);
	c.p.restore();
	drawText(c.p, QRectF(kLeft, pillY, w, pillH), c.x.line1, f1, c.text, Qt::AlignCenter, c.part(0.25, 0.5));
	if (two)
		drawText(c.p, QRectF(kLeft, pillY + pillH + 4, w, 44), c.x.line2, f2, c.sub, Qt::AlignCenter,
			 c.part(0.35, 0.5));
	const QRectF badge(cx - d / 2, pillY - d + 8, d, d);
	const double pop = c.partBack(0.1, 0.45);
	c.p.save();
	c.p.translate(badge.center());
	c.p.scale(std::max(pop, 0.001), std::max(pop, 0.001));
	c.p.translate(-badge.center());
	shadowUnder(c, badge, d / 2);
	c.p.setPen(Qt::NoPen);
	c.p.setBrush(c.shape2);
	c.p.drawEllipse(badge);
	drawLogoOrMark(c, logo, badge.adjusted(16, 16, -16, -16), c.panel, c.shape2, 1.0);
	c.p.restore();
}

// ---- Glass bar -----------------------------------------------------------------------------------------

// A long glossy blue bar with a gold tab, in the style of broadcast graphics packages.
void glassBar(Ctx &c)
{
	const double w = kDesignWidth - 2 * kLeft, h = 86;
	const double y = c.bottom() - h;
	const double e = c.part(0.0, 0.65);
	const QRectF bar(kLeft, y, w * e, h);
	QLinearGradient g(0, y, 0, y + h);
	g.setColorAt(0.0, c.panel.lighter(135));
	g.setColorAt(0.5, c.panel);
	g.setColorAt(1.0, c.panel.darker(160));
	c.p.save();
	c.p.setClipRect(bar);
	shadowUnder(c, QRectF(kLeft, y, w, h), 0);
	c.p.setPen(Qt::NoPen);
	c.p.setBrush(g);
	c.p.drawRect(QRectF(kLeft, y, w, h));
	// The sheen: a bright band across the upper half, and light edges.
	QLinearGradient sheen(0, y, 0, y + h * 0.5);
	sheen.setColorAt(0.0, QColor(255, 255, 255, 60));
	sheen.setColorAt(1.0, QColor(255, 255, 255, 0));
	c.p.setBrush(sheen);
	c.p.drawRect(QRectF(kLeft, y, w, h * 0.5));
	fillRect(c.p, QRectF(kLeft, y, w, 2), QColor(255, 255, 255, 90));
	fillRect(c.p, QRectF(kLeft, y + h - 3, w, 3), c.accent);
	// The gold tab, then the words.
	fillRect(c.p, QRectF(kLeft + 62, y + 12, 12, h - 26), c.accent);
	const QFont f1 = fitFont(c, c.t.body, 42, 26, c.x.line1.toUpper(), w - 220);
	drawText(c.p, QRectF(kLeft + 100 + 40 * (1 - c.part(0.2, 0.5)), y, w, h), c.x.line1.toUpper(), f1, c.text,
		 Qt::AlignLeft | Qt::AlignVCenter, c.part(0.2, 0.5), 2.0);
	// A soft flare at each end.
	for (double fx : {kLeft + 8.0, kLeft + w - 8.0}) {
		QRadialGradient glow(fx, y + h, 120);
		glow.setColorAt(0.0, QColor(255, 255, 255, 120));
		glow.setColorAt(1.0, QColor(255, 255, 255, 0));
		c.p.setBrush(glow);
		c.p.drawEllipse(QPointF(fx, y + h), 120, 26);
	}
	c.p.restore();
	if (c.has(c.x.line2)) {
		const QFont f2 = c.font(c.t.body, 26);
		drawText(c.p, QRectF(kLeft + 100, y + h + 8, w - 120, 36), c.x.line2.toUpper(), f2, c.sub,
			 Qt::AlignLeft | Qt::AlignVCenter, c.part(0.4, 0.5), 1.5);
	}
}

// ---- Desk news bars (a dark LIVE block, a white bar, optional strips) -----------------------------------

enum class DeskKind { Two, One, Clock, Crawl };

void deskBar(Ctx &c, DeskKind kind)
{
	const double blockW = 150, padX = 32, barH = 84, stripH = 38;
	const QFont f1 = fitFont(c, c.t.display, 54, 32, c.x.line1.toUpper(), 1200);
	const QFont fs = c.font(c.t.body, 22);
	const QFont fb = c.font(c.t.display, 34);
	const bool strip = kind != DeskKind::One;
	const bool crawl = kind == DeskKind::Crawl;
	const double totalH = barH + (strip ? stripH : 0.0) + (crawl ? stripH : 0.0);
	const double w = std::max(900.0, std::min(kDesignWidth - 2 * kLeft,
						  blockW + 2 * padX + textWidth(f1, c.x.line1.toUpper()) + 140));
	const double y = c.bottom() - totalH;
	const double e = c.part(0.0, 0.6);
	c.p.save();
	c.p.setClipRect(QRectF(kLeft, y - 4, w * e, totalH + 8));
	shadowUnder(c, QRectF(kLeft, y, w, totalH), 0);
	fillRect(c.p, QRectF(kLeft + blockW, y, w - blockW, barH), c.panel);
	fillRect(c.p, QRectF(kLeft, y, blockW, totalH), c.shape3);
	// The label, on up to two lines.
	const QStringList label = twoLines(c.x.tag.toUpper());
	const double lh = 36;
	for (int i = 0; i < label.size(); ++i)
		drawText(c.p, QRectF(kLeft, y + (barH - lh * label.size()) / 2 + i * lh, blockW, lh), label[i], fb,
			 c.ink, Qt::AlignCenter, c.part(0.15, 0.4), 1.0);
	drawText(c.p, QRectF(kLeft + blockW + padX + 30 * (1 - c.part(0.25, 0.5)), y, w, barH), c.x.line1.toUpper(), f1,
		 c.text, Qt::AlignLeft | Qt::AlignVCenter, c.part(0.25, 0.5));
	if (strip) {
		const QRectF s(kLeft + blockW, y + barH, w - blockW, stripH);
		const QColor stripFill = crawl ? c.shape3.darker(115) : c.accent;
		fillRect(c.p, s, stripFill);
		const double timeW = kind == DeskKind::Two ? 0.0 : 150.0;
		drawText(c.p, QRectF(s.left() + padX, s.top(), s.width() - timeW - padX, stripH), c.x.line2.toUpper(),
			 fs, c.sub, Qt::AlignLeft | Qt::AlignVCenter, c.part(0.4, 0.5), 1.0);
		if (kind == DeskKind::Clock) {
			fillRect(c.p, QRectF(s.right() - timeW, s.top(), timeW, stripH), c.shape2);
			drawText(c.p, QRectF(s.right() - timeW, s.top(), timeW, stripH), clockText(), fs, c.text,
				 Qt::AlignCenter);
		}
	}
	if (crawl) {
		const QRectF s(kLeft + blockW, y + barH + stripH, w - blockW, stripH);
		fillRect(c.p, s, c.accent);
		const double timeW = 150.0;
		drawTickerTextOf(c, c.x.note, QRectF(s.left() + 10, s.top(), s.width() - timeW - 10, stripH), fs,
				 c.sub);
		fillRect(c.p, QRectF(s.right() - timeW, s.top(), timeW, stripH), c.shape2);
		drawText(c.p, QRectF(s.right() - timeW, s.top(), timeW, stripH), clockText(), fs, c.text,
			 Qt::AlignCenter);
	}
	c.p.restore();
}

// ---- Network bars -----------------------------------------------------------------------------------

// Network Name: a wide bar that fades from red to navy, the name in capitals, the role on a white label.
void networkName(Ctx &c)
{
	const QImage logo = logoImage(c.x.logoPath);
	const double w = kDesignWidth - 2 * kLeft, h = 96;
	const QFont f1 = fitFont(c, c.t.display, 58, 34, c.x.line1.toUpper(), w - 520);
	const QFont f2 = c.font(c.t.body, 26);
	const bool two = c.has(c.x.line2);
	const double y = c.bottom() - h - (two ? 52.0 : 0.0);
	const double e = c.part(0.0, 0.65);
	c.p.save();
	c.p.setClipRect(QRectF(kLeft, y - 4, w * e, h + 70));
	QLinearGradient g(kLeft, 0, kLeft + w, 0);
	g.setColorAt(0.0, c.accent);
	g.setColorAt(0.45, c.panel.lighter(115));
	g.setColorAt(1.0, c.panel.darker(130));
	shadowUnder(c, QRectF(kLeft, y, w, h), 0);
	c.p.setPen(Qt::NoPen);
	c.p.setBrush(g);
	c.p.drawRect(QRectF(kLeft, y, w, h));
	fillRect(c.p, QRectF(kLeft, y, w, 2), QColor(255, 255, 255, 80));
	drawText(c.p, QRectF(kLeft + 230 + 40 * (1 - c.part(0.2, 0.5)), y, w, h), c.x.line1.toUpper(), f1, c.text,
		 Qt::AlignLeft | Qt::AlignVCenter, c.part(0.2, 0.5), 2.0);
	if (two) {
		const double tw = textWidth(f2, c.x.line2.toUpper()) + 2 * 34;
		const QRectF tab(kLeft + 214, y + h - 2, tw, 52);
		fillRect(c.p, tab, c.shape2);
		drawText(c.p, tab, c.x.line2.toUpper(), f2, c.ink, Qt::AlignCenter, c.part(0.4, 0.4), 1.5);
	}
	if (!logo.isNull()) {
		const QRectF box(kLeft + w - 300, y + 10, 270, h - 20);
		fillRect(c.p, box, c.shape2, 4);
		drawLogo(c.p, logo, box.adjusted(14, 8, -14, -8), c.part(0.5, 0.4));
	}
	c.p.restore();
}

// Network Breaking: a red tab, a navy bar with a big headline and two small lines, a white logo tab.
void networkBreaking(Ctx &c)
{
	const QImage logo = logoImage(c.x.logoPath);
	const double w = kDesignWidth - 2 * kLeft, h = 128, tabH = 40;
	const QFont f1 = fitFont(c, c.t.display, 58, 34, c.x.line1.toUpper(), w - 420);
	const QFont fs = c.font(c.t.body, 22);
	const QFont ft = c.font(c.t.display, 28);
	const double y = c.bottom() - h;
	const double e = c.part(0.0, 0.65);
	c.p.save();
	c.p.setClipRect(QRectF(kLeft, y - tabH - 4, w * e, h + tabH + 8));
	QLinearGradient g(0, y, 0, y + h);
	g.setColorAt(0.0, c.panel.lighter(120));
	g.setColorAt(1.0, c.panel.darker(150));
	shadowUnder(c, QRectF(kLeft, y, w, h), 0);
	c.p.setPen(Qt::NoPen);
	c.p.setBrush(g);
	c.p.drawRect(QRectF(kLeft, y, w, h));
	fillRect(c.p, QRectF(kLeft + 90, y - tabH, w * 0.62, tabH), c.accent);
	drawText(c.p, QRectF(kLeft + 120, y - tabH, w * 0.6, tabH), c.x.tag.toUpper(), ft, c.ink,
		 Qt::AlignLeft | Qt::AlignVCenter, c.part(0.2, 0.4), 2.0);
	drawText(c.p, QRectF(kLeft + 40 + 40 * (1 - c.part(0.2, 0.5)), y + 6, w, 70), c.x.line1.toUpper(), f1, c.text,
		 Qt::AlignLeft | Qt::AlignVCenter, c.part(0.2, 0.5));
	drawText(c.p, QRectF(kLeft + 40, y + 74, w - 400, 28), c.x.line2.toUpper(), fs, c.sub,
		 Qt::AlignLeft | Qt::AlignVCenter, c.part(0.35, 0.5), 1.0);
	drawText(c.p, QRectF(kLeft + 40, y + 98, w - 400, 28), c.x.note.toUpper(), fs, c.sub,
		 Qt::AlignLeft | Qt::AlignVCenter, c.part(0.4, 0.5), 1.0);
	if (!logo.isNull()) {
		const QRectF box(kLeft + w - 330, y + h - 70, 300, 70);
		fillRect(c.p, box, c.shape2);
		drawLogo(c.p, logo, box.adjusted(16, 8, -16, -8), c.part(0.5, 0.4));
	}
	c.p.restore();
}

// ---- Angular bar ------------------------------------------------------------------------------------

// Angular Logo: a white faceted logo block over a black angular bar, the second line on a white facet.
void angularLogo(Ctx &c)
{
	const QImage logo = logoImage(c.x.logoPath);
	const double h = 150;
	const QFont f1 = fitFont(c, c.t.display, 50, 30, c.x.line1.toUpper(), 900);
	const QFont f2 = c.font(c.t.body, 30);
	const double textW = std::max(textWidth(f1, c.x.line1.toUpper()), textWidth(f2, c.x.line2) + 120);
	const double w = 330 + textW + 90;
	const double y = c.bottom() - h;
	const double e = c.part(0.0, 0.6);
	c.p.save();
	c.p.setClipRect(QRectF(kLeft, y - 20, w * e, h + 40));
	c.p.setPen(Qt::NoPen);
	c.p.setBrush(c.panel);
	QPolygonF bar;
	bar << QPointF(kLeft + 120, y + 24) << QPointF(kLeft + w, y + 12) << QPointF(kLeft + w - 56, y + 64)
	    << QPointF(kLeft + w - 20, y + 96) << QPointF(kLeft + 150, y + 92);
	c.p.drawPolygon(bar);
	c.p.setBrush(c.shape2);
	QPolygonF tail;
	tail << QPointF(kLeft + 300, y + 92) << QPointF(kLeft + w - 100, y + 96) << QPointF(kLeft + w - 150, y + h - 8)
	     << QPointF(kLeft + 262, y + h - 14);
	c.p.drawPolygon(tail);
	QPolygonF box;
	box << QPointF(kLeft, y + 10) << QPointF(kLeft + 200, y) << QPointF(kLeft + 236, y + 104)
	    << QPointF(kLeft + 24, y + 126);
	c.p.drawPolygon(box);
	drawLogoOrMark(c, logo, QRectF(kLeft + 40, y + 16, 150, 96), c.panel, c.shape2, c.part(0.2, 0.4));
	drawText(c.p, QRectF(kLeft + 290, y + 16, textW + 40, 70), c.x.line1.toUpper(), f1, c.text,
		 Qt::AlignLeft | Qt::AlignVCenter, c.part(0.3, 0.5), 1.0);
	drawText(c.p, QRectF(kLeft + 330, y + 94, textW, 44), c.x.line2, f2, c.ink, Qt::AlignLeft | Qt::AlignVCenter,
		 c.part(0.4, 0.5));
	c.p.restore();
}

// ---- Logo animations ---------------------------------------------------------------------------------

// Where in its repeat the animation is, 0..1 (a pulse of `pulseSeconds`, 100 px per second of position).
double logoPhase(const Ctx &c)
{
	return pulsePhase(c);
}

double easeInOut(double t)
{
	t = std::clamp(t, 0.0, 1.0);
	return t * t * (3.0 - 2.0 * t);
}

QRectF logoBox(const Ctx &)
{
	const double s = 150;
	return QRectF(kLeft, 64, s, s);
}

// Logo Light Sweep: the logo with a band of light that crosses it every few seconds, only over the logo itself.
void logoSweep(Ctx &c)
{
	const QImage logo = logoImage(c.x.logoPath);
	const QRectF box = logoBox(c);
	const double a = c.part(0.0, 0.6);
	const int size = int(std::ceil(box.width() * c.p.transform().m11()));
	QImage layer(std::max(size, 1), std::max(size, 1), QImage::Format_ARGB32_Premultiplied);
	layer.fill(Qt::transparent);
	{
		QPainter q(&layer);
		q.setRenderHint(QPainter::Antialiasing, true);
		q.setRenderHint(QPainter::SmoothPixmapTransform, true);
		q.scale(layer.width() / box.width(), layer.height() / box.height());
		drawLogoOrMarkOn(q, c, logo, QRectF(0, 0, box.width(), box.height()), c.accent, c.ink, 1.0);
		const double t = easeInOut(logoPhase(c) / 0.4); // the sweep takes the first 40% of each repeat
		if (logoPhase(c) < 0.4) {
			q.setCompositionMode(QPainter::CompositionMode_SourceAtop);
			const double bw = box.width() * 0.55;
			const double x = -bw + (box.width() + 2 * bw) * t;
			QLinearGradient g(x, 0, x + bw, 0);
			// Silver edges around a bright core: the core shows on dark and coloured logos, the silver
			// edges show on white ones, so the sweep is visible whatever the logo looks like.
			g.setColorAt(0.0, QColor(150, 158, 170, 0));
			g.setColorAt(0.3, QColor(125, 133, 148, 215));
			g.setColorAt(0.5, QColor(255, 255, 255, 240));
			g.setColorAt(0.7, QColor(125, 133, 148, 215));
			g.setColorAt(1.0, QColor(150, 158, 170, 0));
			q.setPen(Qt::NoPen);
			q.setBrush(g);
			QPolygonF band;
			band << QPointF(x + bw * 0.25, 0) << QPointF(x + bw * 1.25, 0)
			     << QPointF(x + bw * 0.75, box.height()) << QPointF(x - bw * 0.25, box.height());
			q.drawPolygon(band);
		}
	}
	// Draw the layer with the painter's current transform.
	c.p.save();
	c.p.setOpacity(c.p.opacity() * a);
	c.p.setRenderHint(QPainter::SmoothPixmapTransform, true);
	c.p.drawImage(box, layer);
	c.p.restore();
}

// Logo Spin: the logo turns on its vertical axis once every few seconds, shading as it turns edge-on.
void logoSpin(Ctx &c)
{
	const QImage logo = logoImage(c.x.logoPath);
	const QRectF box = logoBox(c);
	const double a = c.part(0.0, 0.6);
	const double turn = easeInOut(logoPhase(c) / 0.5); // one full turn in the first half of each repeat
	const double angle = logoPhase(c) < 0.5 ? turn * 2.0 * kPi : 0.0;
	const double sx = std::max(0.02, std::abs(std::cos(angle)));
	const int size = int(std::ceil(box.width() * c.p.transform().m11()));
	QImage layer(std::max(size, 1), std::max(size, 1), QImage::Format_ARGB32_Premultiplied);
	layer.fill(Qt::transparent);
	{
		QPainter q(&layer);
		q.setRenderHint(QPainter::Antialiasing, true);
		q.setRenderHint(QPainter::SmoothPixmapTransform, true);
		q.scale(layer.width() / box.width(), layer.height() / box.height());
		drawLogoOrMarkOn(q, c, logo, QRectF(0, 0, box.width(), box.height()), c.accent, c.ink, 1.0);
		q.setCompositionMode(QPainter::CompositionMode_SourceAtop);
		q.fillRect(QRectF(0, 0, box.width(), box.height()), QColor(0, 0, 0, int(150 * (1.0 - sx))));
	}
	c.p.save();
	c.p.setOpacity(c.p.opacity() * a);
	c.p.setRenderHint(QPainter::SmoothPixmapTransform, true);
	c.p.translate(box.center());
	c.p.scale(sx, 1.0 - 0.05 * (1.0 - sx));
	c.p.translate(-box.center());
	c.p.drawImage(box, layer);
	c.p.restore();
}

// ---- More logo animations: modern broadcast "bug" motions, all quiet and repeating ---------------------------

enum class LogoFx { Pulse, Float, Flip, Ripple, Hop, Reveal, Orbit, Water };

// The logo drawn once into a square picture, so a motion can move it as one piece.
QImage logoLayer(const Ctx &c, const QImage &logo, const QRectF &box)
{
	const int size = int(std::ceil(box.width() * c.p.transform().m11()));
	QImage layer(std::max(size, 1), std::max(size, 1), QImage::Format_ARGB32_Premultiplied);
	layer.fill(Qt::transparent);
	{
		QPainter q(&layer);
		q.setRenderHint(QPainter::Antialiasing, true);
		q.setRenderHint(QPainter::SmoothPixmapTransform, true);
		q.scale(layer.width() / box.width(), layer.height() / box.height());
		drawLogoOrMarkOn(q, c, logo, QRectF(0, 0, box.width(), box.height()), c.accent, c.ink, 1.0);
	}
	return layer;
}

// The logo's own shape pushed about like a reflection in water: rows slide sideways and columns up and down
// in travelling waves. `amount` is 0 (still) to 1 (the strongest the effect gets).
QImage waterWarp(const QImage &src, double amount, double phase)
{
	const int w = src.width(), h = src.height();
	if (w < 4 || h < 4)
		return src;
	QImage out(w, h, QImage::Format_ARGB32_Premultiplied);
	out.fill(Qt::transparent);
	QImage mid(w, h, QImage::Format_ARGB32_Premultiplied);
	mid.fill(Qt::transparent);
	const double ampX = amount * w * 0.02, ampY = amount * h * 0.012;
	for (int y = 0; y < h; ++y) {
		const double dx = ampX * std::sin(2.0 * kPi * (y * 2.4 / h) - phase);
		const int shift = int(std::lround(dx));
		const QRgb *from = reinterpret_cast<const QRgb *>(src.constScanLine(y));
		QRgb *to = reinterpret_cast<QRgb *>(mid.scanLine(y));
		for (int x = 0; x < w; ++x) {
			const int sx = x - shift;
			if (sx >= 0 && sx < w)
				to[x] = from[sx];
		}
	}
	for (int x = 0; x < w; ++x) {
		const double dy = ampY * std::sin(2.0 * kPi * (x * 1.8 / w) - phase * 0.8 + 1.2);
		const int shift = int(std::lround(dy));
		for (int y = 0; y < h; ++y) {
			const int sy = y - shift;
			if (sy >= 0 && sy < h)
				reinterpret_cast<QRgb *>(out.scanLine(y))[x] =
					reinterpret_cast<const QRgb *>(mid.constScanLine(sy))[x];
		}
	}
	return out;
}

void logoFx(Ctx &c, LogoFx fx)
{
	const QImage logo = logoImage(c.x.logoPath);
	const QRectF box = logoBox(c);
	const QImage layer = logoLayer(c, logo, box);
	const double ph = logoPhase(c);
	const QPointF mid = box.center();
	c.p.save();
	c.p.setRenderHint(QPainter::SmoothPixmapTransform, true);
	c.p.setOpacity(c.p.opacity() * c.part(0.0, 0.6));
	switch (fx) {
	case LogoFx::Pulse: { // a soft glow swells behind the logo, which grows a touch with it
		const double bump = ph < 0.5 ? std::sin(ph / 0.5 * kPi) : 0.0;
		const double r = box.width() * 0.9;
		QColor halo = c.accent;
		QRadialGradient g(mid, r);
		halo.setAlpha(int(115 * bump));
		g.setColorAt(0.4, halo);
		halo.setAlpha(0);
		g.setColorAt(1.0, halo);
		c.p.setPen(Qt::NoPen);
		c.p.setBrush(g);
		c.p.drawEllipse(mid, r, r);
		c.p.translate(mid);
		c.p.scale(1.0 + 0.08 * bump, 1.0 + 0.08 * bump);
		c.p.translate(-mid);
		c.p.drawImage(box, layer);
		break;
	}
	case LogoFx::Float: { // a slow drift up and down over a soft shadow
		const double lift = std::sin(ph * 2.0 * kPi);
		c.p.drawImage(box.translated(0, -10 * lift), layer);
		break;
	}
	case LogoFx::Flip: { // turns over on its horizontal axis, shading as it goes edge-on
		const double angle = ph < 0.45 ? easeInOut(ph / 0.45) * 2.0 * kPi : 0.0;
		const double sy = std::max(0.02, std::abs(std::cos(angle)));
		QImage shaded = layer;
		{
			QPainter q(&shaded);
			q.setCompositionMode(QPainter::CompositionMode_SourceAtop);
			q.fillRect(shaded.rect(), QColor(0, 0, 0, int(150 * (1.0 - sy))));
		}
		c.p.translate(mid);
		c.p.scale(1.0 - 0.05 * (1.0 - sy), sy);
		c.p.translate(-mid);
		c.p.drawImage(box, shaded);
		break;
	}
	case LogoFx::Ripple: { // two rings spread out from the logo and fade
		c.p.drawImage(box, layer);
		for (int k = 0; k < 2; ++k) {
			const double t = ph / 0.75 - 0.22 * k;
			if (t <= 0.0 || t >= 1.0)
				continue;
			const double r = box.width() * (0.5 + 0.5 * easeInOut(t));
			QColor ring = c.accent;
			ring.setAlpha(int(210 * (1.0 - t)));
			c.p.setBrush(Qt::NoBrush);
			c.p.setPen(QPen(ring, 6.0 * (1.0 - t) + 1.5));
			c.p.drawEllipse(mid, r, r);
		}
		break;
	}
	case LogoFx::Hop: { // two small hops that settle, with a touch of squash on landing
		const double u = std::clamp(ph / 0.4, 0.0, 1.0);
		const double arc = ph < 0.4 ? std::abs(std::sin(u * 2.0 * kPi)) * (1.0 - 0.55 * u) : 0.0;
		const double squash = ph < 0.4 ? 0.07 * (1.0 - arc) * (1.0 - u) : 0.0;
		c.p.translate(mid.x(), box.bottom());
		c.p.scale(1.0 + squash * 0.8, 1.0 - squash);
		c.p.translate(-mid.x(), -box.bottom());
		c.p.drawImage(box.translated(0, -34 * arc), layer);
		break;
	}
	case LogoFx::Reveal: { // wipes away to the left behind a thin line, then wipes back in
		double r = 1.0;
		if (ph < 0.18)
			r = 1.0 - easeInOut(ph / 0.18);
		else if (ph < 0.36)
			r = easeInOut((ph - 0.18) / 0.18);
		const double edge = box.left() + box.width() * r;
		c.p.save();
		c.p.setClipRect(QRectF(box.left() - 4, box.top() - 4, box.width() * r + 4, box.height() + 8));
		c.p.drawImage(box, layer);
		c.p.restore();
		if (r > 0.01 && r < 0.99)
			fillRect(c.p, QRectF(edge - 1.5, box.top(), 3, box.height()), c.accent);
		break;
	}
	case LogoFx::Water: { // the logo ripples like water while a silver sheen drifts across it
		const double u = ph < 0.6 ? ph / 0.6 : 0.0;
		const double env = std::sin(u * kPi); // swells in, settles out
		QImage rippled = waterWarp(layer, env, u * 4.0 * kPi);
		if (env > 0.01) {
			QPainter q(&rippled);
			q.setCompositionMode(QPainter::CompositionMode_SourceAtop);
			const double w = rippled.width(), h = rippled.height();
			const double x = -w * 0.5 + w * 2.0 * u;
			QLinearGradient g(x, 0, x + w * 0.5, h * 0.3);
			g.setColorAt(0.0, QColor(150, 158, 170, 0));
			g.setColorAt(0.3, QColor(150, 158, 170, int(150 * env)));
			g.setColorAt(0.5, QColor(255, 255, 255, int(230 * env)));
			g.setColorAt(0.7, QColor(150, 158, 170, int(150 * env)));
			g.setColorAt(1.0, QColor(150, 158, 170, 0));
			q.fillRect(rippled.rect(), g);
		}
		c.p.drawImage(box, rippled);
		break;
	}
	case LogoFx::Orbit: { // a short arc circles the logo
		c.p.drawImage(box, layer);
		const double r = box.width() * 0.62;
		const QRectF ring(mid.x() - r, mid.y() - r, 2 * r, 2 * r);
		QColor faint = c.accent;
		faint.setAlpha(60);
		c.p.setBrush(Qt::NoBrush);
		c.p.setPen(QPen(faint, 3.0));
		c.p.drawEllipse(ring);
		c.p.setPen(QPen(c.accent, 6.0, Qt::SolidLine, Qt::RoundCap));
		c.p.drawArc(ring, int(-ph * 360.0 * 16.0), 100 * 16);
		break;
	}
	}
	c.p.restore();
}

void logoPulse(Ctx &c)
{
	logoFx(c, LogoFx::Pulse);
}
void logoFloat(Ctx &c)
{
	logoFx(c, LogoFx::Float);
}
void logoFlip(Ctx &c)
{
	logoFx(c, LogoFx::Flip);
}
void logoRipple(Ctx &c)
{
	logoFx(c, LogoFx::Ripple);
}
void logoHop(Ctx &c)
{
	logoFx(c, LogoFx::Hop);
}
void logoReveal(Ctx &c)
{
	logoFx(c, LogoFx::Reveal);
}
void logoOrbit(Ctx &c)
{
	logoFx(c, LogoFx::Orbit);
}

void logoWater(Ctx &c)
{
	logoFx(c, LogoFx::Water);
}

// ---- Glass Cube and Glass Disc: the logo floats inside a transparent solid that turns ---------------------

// A tiny 3D view: turns a point in the solid's space (y up, z toward the viewer) into a point on the picture,
// looking slightly down on it with a touch of perspective.
struct GlassView {
	QPointF mid;
	double tilt = 0.30;
	double dist = 420.0;
	QPointF at(double x, double y, double z, double *depth = nullptr) const
	{
		if (depth)
			*depth = z;
		const double p = dist / (dist - z);
		return QPointF(mid.x() + x * p, mid.y() - y * std::cos(tilt) * p + z * std::sin(tilt) * p);
	}
};

// One pane of glass: a faint cool tint, a diagonal sheen that slides as the solid turns, and a bright edge.
void glassPane(Ctx &c, const QPolygonF &poly, double facing, double turn, double strength)
{
	const QRectF bb = poly.boundingRect();
	QLinearGradient g(bb.topLeft(), bb.bottomRight());
	const double band = 0.5 + 0.42 * std::sin(turn);
	g.setColorAt(0.0, QColor(215, 232, 248, int(70 * strength)));
	g.setColorAt(std::clamp(band - 0.12, 0.01, 0.98), QColor(190, 215, 238, int(24 * strength)));
	g.setColorAt(std::clamp(band, 0.02, 0.99), QColor(255, 255, 255, int(120 * strength * facing)));
	g.setColorAt(std::clamp(band + 0.12, 0.03, 0.995), QColor(190, 215, 238, int(24 * strength)));
	g.setColorAt(1.0, QColor(170, 200, 228, int(60 * strength)));
	c.p.setBrush(g);
	c.p.setPen(
		QPen(QColor(255, 255, 255, int(70 + 130 * facing)), 1.6, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
	c.p.drawPolygon(poly);
}

// The logo as a flat card standing in the middle of the solid, turned with it (so from behind it reads mirrored).
void glassLogo(Ctx &c, const GlassView &v, const QImage &layer, const QRectF &box, double turn, double half)
{
	if (std::abs(std::cos(turn)) < 0.03)
		return; // edge-on: nothing to see
	QPolygonF to;
	for (const QPointF &q : {QPointF(-1, 1), QPointF(1, 1), QPointF(1, -1), QPointF(-1, -1)})
		to << v.at(q.x() * half * std::cos(turn), q.y() * half, -q.x() * half * std::sin(turn));
	QTransform t;
	const QPolygonF from(QRectF(0, 0, box.width(), box.height()));
	if (!QTransform::quadToQuad(QPolygonF({from[0], from[1], from[2], from[3]}), to, t))
		return;
	c.p.save();
	c.p.setTransform(t, true);
	c.p.drawImage(QRectF(0, 0, box.width(), box.height()), layer);
	c.p.restore();
}

// How far round the solid is, 0..2pi: one smooth turn early in each repeat, then it rests facing the viewer.
double glassTurn(const Ctx &c)
{
	const double ph = logoPhase(c);
	return ph < 0.75 ? easeInOut(ph / 0.75) * 2.0 * kPi : 0.0;
}

// Logo Glass Cube: a transparent cube with the logo inside it, turning on its vertical axis.
void logoGlassCube(Ctx &c)
{
	const QImage logo = logoImage(c.x.logoPath);
	const QRectF box = logoBox(c);
	const QImage layer = logoLayer(c, logo, box);
	const double turn = glassTurn(c);
	const GlassView v{box.center()};
	const double a = 46.0, R = a * std::sqrt(2.0);
	struct Face {
		double facing; // cos of the angle between the face's normal and the viewer
		QPolygonF poly;
		double psi;
	};
	std::vector<Face> faces;
	for (int k = 0; k < 4; ++k) {
		const double p0 = turn + kPi / 4 + k * kPi / 2, p1 = p0 + kPi / 2;
		QPolygonF poly;
		poly << v.at(R * std::sin(p0), a, R * std::cos(p0)) << v.at(R * std::sin(p1), a, R * std::cos(p1))
		     << v.at(R * std::sin(p1), -a, R * std::cos(p1)) << v.at(R * std::sin(p0), -a, R * std::cos(p0));
		const double psi = turn + kPi / 2 + k * kPi / 2;
		faces.push_back({std::cos(psi), poly, psi});
	}
	std::sort(faces.begin(), faces.end(), [](const Face &x, const Face &y) { return x.facing < y.facing; });
	QPolygonF top, bottom;
	for (int k = 0; k < 4; ++k) {
		const double p = turn + kPi / 4 + k * kPi / 2;
		top << v.at(R * std::sin(p), a, R * std::cos(p));
		bottom << v.at(R * std::sin(p), -a, R * std::cos(p));
	}
	c.p.save();
	c.p.setRenderHint(QPainter::Antialiasing, true);
	c.p.setRenderHint(QPainter::SmoothPixmapTransform, true);
	c.p.setOpacity(c.p.opacity() * c.part(0.0, 0.6));
	glassPane(c, bottom, 0.2, turn, 0.8);
	for (const Face &f : faces)
		if (f.facing < 0)
			glassPane(c, f.poly, 0.15, f.psi, 0.8);
	glassLogo(c, v, layer, box, turn, a * 0.78);
	for (const Face &f : faces)
		if (f.facing >= 0)
			glassPane(c, f.poly, f.facing, f.psi, 1.0);
	glassPane(c, top, 0.7, turn, 1.0);
	c.p.restore();
}

// Logo Glass Disc: a round, thick glass coin with the logo inside it, turning on its vertical axis.
void logoGlassDisc(Ctx &c)
{
	const QImage logo = logoImage(c.x.logoPath);
	const QRectF box = logoBox(c);
	const QImage layer = logoLayer(c, logo, box);
	const double turn = glassTurn(c);
	const GlassView v{box.center()};
	const double r = 58.0, t = 15.0;
	constexpr int kSteps = 56;
	const double nx = std::sin(turn), nz = std::cos(turn); // the disc's normal; (cos, -sin) runs along its face
	auto rim = [&](double side, double al) {
		const double ux = std::cos(turn) * std::cos(al), uz = -std::sin(turn) * std::cos(al);
		return v.at(side * t * nx + r * ux, r * std::sin(al), side * t * nz + r * uz);
	};
	QPolygonF front, back;
	const double s = nz >= 0 ? 1.0 : -1.0; // which face is nearer the viewer
	for (int i = 0; i < kSteps; ++i) {
		const double al = 2.0 * kPi * i / kSteps;
		front << rim(s, al);
		back << rim(-s, al);
	}
	c.p.save();
	c.p.setRenderHint(QPainter::Antialiasing, true);
	c.p.setRenderHint(QPainter::SmoothPixmapTransform, true);
	c.p.setOpacity(c.p.opacity() * c.part(0.0, 0.6));
	glassPane(c, back, 0.15, turn + 1.0, 0.8);
	// The rim, a ring of narrow strips; the far ones first so the near ones sit on top.
	struct Strip {
		double z;
		QPolygonF poly;
		double lit;
	};
	std::vector<Strip> strips;
	for (int i = 0; i < kSteps; ++i) {
		const double a0 = 2.0 * kPi * i / kSteps, a1 = 2.0 * kPi * (i + 1) / kSteps, am = (a0 + a1) / 2;
		QPolygonF poly;
		poly << v.at(t * nx + r * std::cos(turn) * std::cos(a0), r * std::sin(a0),
			     t * nz - r * std::sin(turn) * std::cos(a0))
		     << v.at(t * nx + r * std::cos(turn) * std::cos(a1), r * std::sin(a1),
			     t * nz - r * std::sin(turn) * std::cos(a1))
		     << v.at(-t * nx + r * std::cos(turn) * std::cos(a1), r * std::sin(a1),
			     -t * nz - r * std::sin(turn) * std::cos(a1))
		     << v.at(-t * nx + r * std::cos(turn) * std::cos(a0), r * std::sin(a0),
			     -t * nz - r * std::sin(turn) * std::cos(a0));
		const double outward = -std::sin(turn) * std::cos(am); // the strip's push toward the viewer
		strips.push_back({outward, poly, std::max(0.0, outward + 0.4 * std::sin(am))});
	}
	std::sort(strips.begin(), strips.end(), [](const Strip &x, const Strip &y) { return x.z < y.z; });
	c.p.setPen(Qt::NoPen);
	for (const Strip &st : strips) {
		c.p.setBrush(QColor(200, 222, 244, int(26 + 70 * std::min(1.0, st.lit))));
		c.p.drawPolygon(st.poly);
	}
	glassLogo(c, v, layer, box, turn, r * 0.7);
	glassPane(c, front, std::abs(nz), turn, 1.0);
	c.p.restore();
}

// ---- Artsy name bars (pastors and speakers) -------------------------------------------------------------------

// "Philip Anthony Mantofa" -> {"Philip", "Anthony Mantofa"}: the first word, and the rest, for stacked names.
QStringList nameLines(const QString &name)
{
	const QStringList words = name.simplified().split(QLatin1Char(' '), Qt::SkipEmptyParts);
	if (words.size() <= 1)
		return words;
	return {words.front(), words.mid(1).join(QLatin1Char(' '))};
}

QPainterPath slantedBar(const QRectF &r, double slantLeft, double slantRight)
{
	QPainterPath path;
	path.moveTo(r.left() + slantLeft, r.top());
	path.lineTo(r.right(), r.top());
	path.lineTo(r.right() - slantRight, r.bottom());
	path.lineTo(r.left(), r.bottom());
	path.closeSubpath();
	return path;
}

// Name 1, Lime Slant: a bright bar with a slanted end, dark words, and a dark logo block with a coloured edge.
void nameLimeSlant(Ctx &c)
{
	const QFont f1 = fitFont(c, c.t.display, 50, 30, c.x.line1.toUpper(), 760);
	const QFont f2 = fitFont(c, c.t.body, 28, 18, c.x.line2.toUpper(), 760);
	const double textW = std::max(textWidth(f1, c.x.line1.toUpper()), textWidth(f2, c.x.line2.toUpper()));
	const double h = 104, logoW = 130, w = std::max(420.0, textW + 80);
	const double y = c.bottom() - h, e = c.part(0.0, 0.55);
	c.p.save();
	c.p.setClipRect(QRectF(kLeft - 10, y - 10, (w + logoW + 40) * e, h + 20));
	c.p.fillPath(slantedBar(QRectF(kLeft, y, w + 30, h), 0, 30), c.panel);
	const QRectF block(kLeft + w, y, logoW, h);
	c.p.fillPath(slantedBar(block, 30, 0), c.shape3);
	fillRect(c.p, QRectF(block.right() - 2, y, 8, h), c.accent);
	drawLogoOrMark(c, logoImage(c.x.logoPath), block.adjusted(36, 18, -14, -18), c.accent, c.ink, c.part(0.3, 0.4));
	drawText(c.p, QRectF(kLeft + 34, y + 12, w, 52), c.x.line1.toUpper(), f1, c.text, Qt::AlignLeft | Qt::AlignVCenter,
		 c.part(0.2, 0.4));
	drawText(c.p, QRectF(kLeft + 34, y + 62, w, 32), c.x.line2.toUpper(), f2, c.sub, Qt::AlignLeft | Qt::AlignVCenter,
		 c.part(0.3, 0.4));
	c.p.restore();
}

// Name 2, Edge Line: a dark see-through bar with a coloured line on its left edge.
void nameEdgeLine(Ctx &c)
{
	const QFont f1 = fitFont(c, c.t.display, 50, 30, c.x.line1.toUpper(), 900);
	const QFont f2 = fitFont(c, c.t.body, 28, 18, c.x.line2.toUpper(), 900);
	const double textW = std::max(textWidth(f1, c.x.line1.toUpper()), textWidth(f2, c.x.line2.toUpper()));
	const double h = 100, w = std::max(380.0, textW + 70);
	const double y = c.bottom() - h;
	const double eLine = c.part(0.0, 0.35), eBar = c.part(0.15, 0.55);
	fillRect(c.p, QRectF(kLeft, y + h * (1 - eLine), 10, h * eLine), c.accent);
	c.p.save();
	c.p.setClipRect(QRectF(kLeft + 10, y, w * eBar, h));
	fillRect(c.p, QRectF(kLeft + 10, y, w, h), c.panel);
	drawText(c.p, QRectF(kLeft + 40, y + 12, w, 50), c.x.line1.toUpper(), f1, c.text, Qt::AlignLeft | Qt::AlignVCenter,
		 c.part(0.3, 0.4));
	drawText(c.p, QRectF(kLeft + 40, y + 60, w, 30), c.x.line2.toUpper(), f2, c.sub, Qt::AlignLeft | Qt::AlignVCenter,
		 c.part(0.4, 0.4));
	c.p.restore();
}

// Name 3, Blue Slant: a logo tile, then a slanted coloured bar with light words.
void nameBlueSlant(Ctx &c)
{
	const QFont f1 = fitFont(c, c.t.display, 50, 30, c.x.line1.toUpper(), 800);
	const QFont f2 = fitFont(c, c.t.body, 28, 18, c.x.line2.toUpper(), 800);
	const double textW = std::max(textWidth(f1, c.x.line1.toUpper()), textWidth(f2, c.x.line2.toUpper()));
	const double h = 104, tile = 110, w = std::max(400.0, textW + 90);
	const double y = c.bottom() - h, e = c.part(0.0, 0.55);
	c.p.save();
	c.p.setClipRect(QRectF(kLeft - 10, y - 10, (tile + w + 60) * e, h + 20));
	c.p.fillPath(slantedBar(QRectF(kLeft, y, tile + 26, h), 26, 0), c.shape3);
	drawLogoOrMark(c, logoImage(c.x.logoPath), QRectF(kLeft + 30, y + 16, tile - 26, h - 32), c.accent, c.ink,
		       c.part(0.25, 0.4));
	c.p.fillPath(slantedBar(QRectF(kLeft + tile + 10, y, w, h), 26, 26), c.panel);
	drawText(c.p, QRectF(kLeft + tile + 54, y + 12, w, 52), c.x.line1.toUpper(), f1, c.text,
		 Qt::AlignLeft | Qt::AlignVCenter, c.part(0.3, 0.4));
	drawText(c.p, QRectF(kLeft + tile + 54, y + 62, w, 32), c.x.line2.toUpper(), f2, c.sub,
		 Qt::AlignLeft | Qt::AlignVCenter, c.part(0.4, 0.4));
	c.p.restore();
}

// Fine dots that grow from one side to the other, like a printed halftone.
void halftone(QPainter &p, const QRectF &r, const QColor &colour)
{
	p.save();
	p.setClipRect(r);
	p.setPen(Qt::NoPen);
	p.setBrush(colour);
	const double step = 9;
	for (double y = r.top(); y < r.bottom() + step; y += step) {
		for (double x = r.left(); x < r.right() + step; x += step) {
			const double f = (x - r.left()) / std::max(1.0, r.width());
			const double rad = step * 0.48 * (0.25 + 0.75 * (1.0 - std::abs(f - 0.5) * 2.0));
			p.drawEllipse(QPointF(x + (int((y - r.top()) / step) % 2 ? step / 2 : 0), y), rad, rad);
		}
	}
	p.restore();
}

// Name 4, Bracket Frame: corner brackets around a coloured bar with a lighter block behind part of the name, and
// the role on a halftone tag under it.
void nameBracket(Ctx &c)
{
	const QString name = c.x.line1.toUpper();
	const QFont f1 = fitFont(c, c.t.display, 76, 40, name, 1000);
	const QFont f2 = fitFont(c, c.t.body, 30, 18, c.x.line2.toUpper(), 600);
	const double nameW = textWidth(f1, name);
	const double h = 112, w = std::max(520.0, nameW + 120);
	const double y = c.bottom() - h - 56, e = c.part(0.0, 0.5);
	const QRectF bar(kLeft + 30, y, w, h);
	// Brackets.
	const double arm = 34, gap = 20;
	c.p.save();
	c.p.setOpacity(c.p.opacity() * c.part(0.0, 0.35));
	c.p.setPen(QPen(c.shape2, 5, Qt::SolidLine, Qt::SquareCap, Qt::MiterJoin));
	c.p.setBrush(Qt::NoBrush);
	const QRectF frame = bar.adjusted(-gap, -gap, gap, gap);
	for (const QPointF &corner : {frame.topLeft(), frame.topRight(), frame.bottomLeft(), frame.bottomRight()}) {
		const double sx = corner.x() < frame.center().x() ? 1 : -1, sy = corner.y() < frame.center().y() ? 1 : -1;
		QPolygonF l;
		l << QPointF(corner.x(), corner.y() + sy * arm) << corner << QPointF(corner.x() + sx * arm, corner.y());
		c.p.drawPolyline(l);
	}
	c.p.restore();
	c.p.save();
	c.p.setClipRect(QRectF(bar.left(), bar.top() - 4, bar.width() * e, bar.height() + 8));
	fillRect(c.p, bar, c.panel);
	fillRect(c.p, QRectF(bar.left() + w * 0.3, bar.top(), w * 0.34, h), withOpacity(c.shape2, 0.35));
	drawText(c.p, bar, name, f1, c.text, Qt::AlignCenter, c.part(0.25, 0.4), 2.0);
	c.p.restore();
	if (c.has(c.x.line2)) {
		const QString role = c.x.line2.toUpper();
		const double rw = textWidth(f2, role) + 70;
		const QRectF tag(bar.right() - rw - 30, bar.bottom() + 8, rw, 52);
		c.p.save();
		c.p.setOpacity(c.p.opacity() * c.part(0.4, 0.4));
		halftone(c.p, tag, withOpacity(c.text, 0.7));
		drawText(c.p, tag, role, f2, c.ink, Qt::AlignCenter, 1.0, 2.0);
		c.p.restore();
	}
}

// Name 5, Red Stack: a tall coloured line, the name stacked on two lines in tall capitals, the role under it.
void nameRedStack(Ctx &c)
{
	const QStringList lines = nameLines(c.x.line1.toUpper());
	const QFont f1 = c.font(c.t.display, 74);
	const Wrapped role = fitWrapped(c, c.t.body, 26, 16, c.x.line2.toUpper(), 620, 2, 1.2);
	const double lineH = 70, h = lineH * lines.size() + 16 + role.lineHeight * role.lines.size();
	const double y = c.bottom() - h;
	fillRect(c.p, QRectF(kLeft, y + h * (1 - c.part(0.0, 0.4)), 10, h * c.part(0.0, 0.4)), c.accent);
	for (int i = 0; i < lines.size(); ++i) {
		const double a = c.part(0.15 + 0.1 * i, 0.4);
		drawShadowText(c.p, QRectF(kLeft + 34 + 30 * (1 - a), y + i * lineH, 1200, lineH), lines[i], f1, c.text,
			       Qt::AlignLeft | Qt::AlignVCenter, a);
	}
	for (int i = 0; i < role.lines.size(); ++i)
		drawShadowText(c.p,
			       QRectF(kLeft + 36, y + lineH * lines.size() + 12 + i * role.lineHeight, 700, role.lineHeight),
			       role.lines[i], role.font, c.sub, Qt::AlignLeft | Qt::AlignVCenter, c.part(0.4, 0.4));
}

// Name 6, Red Box: the first name in white, the rest in a coloured box under it, the role to the left on a line.
void nameRedBox(Ctx &c)
{
	const QStringList lines = nameLines(c.x.line1.toUpper());
	const QString first = lines.isEmpty() ? QString() : lines.front();
	const QString rest = lines.size() > 1 ? lines.back() : QString();
	const QFont f1 = c.font(c.t.display, 66);
	const QFont f2 = fitFont(c, c.t.body, 24, 16, c.x.line2.toUpper(), 420);
	const double roleW = c.has(c.x.line2) ? std::min(460.0, textWidth(f2, c.x.line2.toUpper()) + 40) : 0.0;
	const double x = kLeft + roleW;
	const double y = c.bottom() - 150;
	const double a1 = c.part(0.1, 0.4), a2 = c.part(0.25, 0.45);
	drawShadowText(c.p, QRectF(x + 20, y, 1000, 70), first, f1, c.text, Qt::AlignLeft | Qt::AlignVCenter, a1);
	if (!rest.isEmpty()) {
		const double bw = textWidth(f1, rest) + 40;
		c.p.save();
		c.p.setClipRect(QRectF(x, y + 70, bw * a2, 78));
		fillRect(c.p, QRectF(x, y + 70, bw, 78), c.accent);
		drawText(c.p, QRectF(x + 20, y + 70, bw, 78), rest, f1, c.text, Qt::AlignLeft | Qt::AlignVCenter);
		c.p.restore();
	}
	if (c.has(c.x.line2)) {
		const double a3 = c.part(0.4, 0.4);
		drawShadowText(c.p, QRectF(kLeft, y + 96, roleW - 30, 34), c.x.line2.toUpper(), f2, c.sub,
			       Qt::AlignLeft | Qt::AlignVCenter, a3);
		fillRect(c.p, QRectF(kLeft + 90 * a3, y + 136, (roleW - 30) * a3 - 90 * a3, 3), withOpacity(c.sub, 0.85));
	}
}

// Name 7, Role Box: the name stacked in heavy capitals, the role in a light box to its left.
void nameRoleBox(Ctx &c)
{
	const QStringList lines = nameLines(c.x.line1.toUpper());
	const QFont f1 = c.font(c.t.display, 64);
	const QFont f2 = fitFont(c, c.t.body, 24, 16, c.x.line2.toUpper(), 520);
	double nameW = 0;
	for (const QString &l : lines)
		nameW = std::max(nameW, textWidth(f1, l));
	const double lineH = 62, h = lineH * std::max<qsizetype>(1, lines.size());
	const double y = c.bottom() - h;
	const double roleW = c.has(c.x.line2) ? textWidth(f2, c.x.line2.toUpper()) + 3.0 * c.x.line2.size() + 60 : 0.0;
	const double nameX = kLeft + roleW + 24;
	for (int i = 0; i < lines.size(); ++i)
		drawShadowText(c.p, QRectF(nameX, y + i * lineH, nameW + 10, lineH), lines[i], f1, c.text,
			       Qt::AlignLeft | Qt::AlignVCenter, c.part(0.15 + 0.1 * i, 0.4));
	if (roleW > 0) {
		const double e = c.part(0.0, 0.5);
		const QRectF box(kLeft + roleW * (1 - e), y + h / 2 - 22, roleW * e, 44);
		fillRect(c.p, box, c.shape2);
		c.p.save();
		c.p.setClipRect(box);
		drawText(c.p, QRectF(kLeft, box.top(), roleW, 44), c.x.line2.toUpper(), f2, c.ink, Qt::AlignCenter, 1.0, 3.0);
		c.p.restore();
	}
}

// Name 8, Heavy + Light: the first name heavy on a light plate, the rest light on a dark box, the role spaced
// out underneath.
void nameHeavyLight(Ctx &c)
{
	const QStringList lines = nameLines(c.x.line1.toUpper());
	const QString first = lines.isEmpty() ? QString() : lines.front();
	const QString rest = lines.size() > 1 ? lines.back() : QString();
	const QFont heavy = c.font(FontFace::OutfitExtraBold, 64);
	const QFont light = c.font(FontFace::OutfitRegular, 54);
	const QFont f2 = fitFont(c, c.t.body, 24, 16, c.x.line2.toUpper(), 900);
	const double w1 = textWidth(heavy, first) + 36, w2 = rest.isEmpty() ? 0.0 : textWidth(light, rest) + 44;
	const double h = 84, y = c.bottom() - h - 50;
	const double e = c.part(0.0, 0.55);
	c.p.save();
	c.p.setClipRect(QRectF(kLeft, y - 10, (w1 + w2 + 20) * e, h + 20));
	fillRect(c.p, QRectF(kLeft, y, w1, h), c.shape2);
	drawText(c.p, QRectF(kLeft, y, w1, h), first, heavy, c.ink, Qt::AlignCenter);
	if (w2 > 0) {
		fillRect(c.p, QRectF(kLeft + w1, y + 10, w2, h - 20), c.panel);
		drawText(c.p, QRectF(kLeft + w1, y + 10, w2, h - 20), rest, light, c.text, Qt::AlignCenter, 1.0, 2.0);
	}
	c.p.restore();
	drawShadowText(c.p, QRectF(kLeft + 6, y + h + 8, 1200, 40), c.x.line2.toUpper(), f2, c.sub,
		       Qt::AlignLeft | Qt::AlignVCenter, c.part(0.4, 0.4), 6.0);
}

// ---- Glow: words on soft moving colours that rise from the bottom --------------------------------------------

// Three soft patches of colour drifting past each other, fading out toward the top of `area`.
void movingGlow(Ctx &c, const QRectF &area, double strength)
{
	const double t = c.tickerPx / 100.0;
	const QColor colours[3] = {c.panel, c.accent, c.shape2};
	c.p.save();
	c.p.setClipRect(area);
	c.p.setPen(Qt::NoPen);
	for (int i = 0; i < 3; ++i) {
		const QPointF centre(area.left() + area.width() * (0.5 + 0.38 * std::sin(t * 0.21 * (i + 1) + i * 2.1)),
				     area.bottom() - area.height() * (0.25 + 0.18 * std::sin(t * 0.17 * (i + 2) + i)));
		const double r = area.width() * (0.38 + 0.06 * std::sin(t * 0.3 + i));
		QRadialGradient g(centre, r);
		QColor col = colours[i];
		col.setAlpha(int(200 * strength));
		g.setColorAt(0.0, col);
		col.setAlpha(0);
		g.setColorAt(1.0, col);
		c.p.setBrush(g);
		c.p.drawEllipse(centre, r, r * 0.75);
	}
	c.p.restore();
}

// Draws `s` as an outline only (no fill), like the second line of a big title.
void outlineText(QPainter &p, const QRectF &box, const QString &s, const QFont &font, const QColor &colour,
		 double width, double opacity)
{
	if (s.isEmpty() || opacity <= 0.0)
		return;
	QPainterPath path;
	const QFontMetricsF fm(font);
	const double x = box.center().x() - fm.horizontalAdvance(s) / 2.0;
	const double y = box.center().y() + (fm.ascent() - fm.descent()) / 2.0;
	path.addText(QPointF(x, y), font, s);
	p.save();
	p.setOpacity(p.opacity() * opacity);
	p.setRenderHint(QPainter::Antialiasing, true);
	p.setBrush(Qt::NoBrush);
	p.setPen(QPen(colour, width, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
	p.drawPath(path);
	p.restore();
}

// Glow Headline: big slanted words over moving colours, the second line drawn as an outline.
void glowHeadline(Ctx &c)
{
	const double glowH = 330;
	movingGlow(c, QRectF(0, c.H - glowH, kDesignWidth, glowH), c.part(0.0, 0.7));
	QFont f1 = fitFont(c, c.t.display, 104, 50, c.x.line1.toUpper(), 1600);
	f1.setItalic(true);
	QFont f2 = fitFont(c, c.t.display, 96, 46, c.x.line2.toUpper(), 1500);
	f2.setItalic(true);
	const double y = c.bottom() - (c.has(c.x.line2) ? 220 : 120);
	const double a1 = c.part(0.15, 0.45), a2 = c.part(0.3, 0.45);
	drawShadowText(c.p, QRectF(0, y + 30 * (1 - a1), kDesignWidth, 110), c.x.line1.toUpper(), f1, c.text,
		       Qt::AlignCenter, a1);
	outlineText(c.p, QRectF(0, y + 110 + 30 * (1 - a2), kDesignWidth, 104), c.x.line2.toUpper(), f2, c.text, 3.0,
		    a2);
}

// Glow Name: a name bar for a speaker over moving colours in the lower-left corner.
void glowName(Ctx &c)
{
	movingGlow(c, QRectF(0, c.H - 300, 1200, 300), c.part(0.0, 0.7));
	QFont f1 = fitFont(c, c.t.display, 76, 40, c.x.line1.toUpper(), 1100);
	f1.setItalic(true);
	QFont f2 = fitFont(c, c.t.display, 40, 24, c.x.line2.toUpper(), 1000);
	f2.setItalic(true);
	const double y = c.bottom() - 140;
	const double a1 = c.part(0.15, 0.45), a2 = c.part(0.3, 0.45);
	drawShadowText(c.p, QRectF(kLeft + 30 * (1 - a1), y, 1200, 84), c.x.line1.toUpper(), f1, c.text,
		       Qt::AlignLeft | Qt::AlignVCenter, a1);
	const QFontMetricsF fm(f2);
	outlineText(c.p,
		    QRectF(kLeft + 30 * (1 - a2), y + 84, fm.horizontalAdvance(c.x.line2.toUpper()), 52),
		    c.x.line2.toUpper(), f2, c.text, 2.0, a2);
}

// ---- Logo 3D Spin: the logo as a thick coin turning in space -----------------------------------------------------

void logoSpin3d(Ctx &c)
{
	const QImage logo = logoImage(c.x.logoPath);
	const QRectF box = logoBox(c);
	const QImage layer = logoLayer(c, logo, box);
	const double turn = logoPhase(c) * 2.0 * kPi; // one whole turn per repeat, steady like a turntable
	const QPointF mid = box.center();
	const double half = box.width() / 2.0, depth = 14.0, dist = 520.0;
	auto at = [&](double x, double y, double z) {
		const double rx = x * std::cos(turn) + z * std::sin(turn);
		const double rz = -x * std::sin(turn) + z * std::cos(turn);
		const double p = dist / (dist - rz);
		return QPointF(mid.x() + rx * p, mid.y() + y * p);
	};
	auto face = [&](double z) {
		QPolygonF to;
		to << at(-half, -half, z) << at(half, -half, z) << at(half, half, z) << at(-half, half, z);
		return to;
	};
	c.p.save();
	c.p.setRenderHint(QPainter::SmoothPixmapTransform, true);
	c.p.setOpacity(c.p.opacity() * c.part(0.0, 0.6));
	// The thickness: darker copies from the back face to the front, then the lit front face on top.
	const QPolygonF from(QRectF(0, 0, box.width(), box.height()));
	const QPolygonF src({from[0], from[1], from[2], from[3]});
	QImage dark = layer;
	{
		QPainter q(&dark);
		q.setCompositionMode(QPainter::CompositionMode_SourceAtop);
		q.fillRect(dark.rect(), QColor(0, 0, 0, 150));
	}
	const bool frontVisible = std::cos(turn) >= 0.0;
	const int steps = 10;
	for (int i = 0; i <= steps; ++i) {
		const double z = frontVisible ? -depth + depth * i / steps : depth - depth * i / steps;
		const bool last = i == steps;
		QTransform t;
		if (std::abs(std::cos(turn)) < 0.02 || !QTransform::quadToQuad(src, face(z), t))
			continue;
		c.p.save();
		c.p.setTransform(t, true);
		c.p.drawImage(QRectF(0, 0, box.width(), box.height()), last ? layer : dark);
		c.p.restore();
	}
	c.p.restore();
}

// ---- Quick Link: a link turned into a QR code, beside a title, a description and a tag ----------------------
// Fields: line 1 = title, line 2 = description, tag = the small label (SCAN ME), note = the link itself.

// The QR code of a link, worked out once and kept (it is asked for on every frame of an animation).
const QrCode &qrFor(const QString &link)
{
	static thread_local QString key;
	static thread_local QrCode code;
	if (link != key) {
		key = link;
		code = encodeQr(link.trimmed().toStdString());
	}
	return code;
}

// "https://www.example.com/give/" -> "example.com/give": what people read under the code.
QString shortLink(QString link)
{
	link = link.trimmed();
	for (const char *prefix : {"https://", "http://"}) {
		if (link.startsWith(QLatin1String(prefix), Qt::CaseInsensitive))
			link = link.mid(int(strlen(prefix)));
	}
	if (link.startsWith(QLatin1String("www."), Qt::CaseInsensitive))
		link = link.mid(4);
	while (link.endsWith(QLatin1Char('/')))
		link.chop(1);
	return link;
}

// The code on a light tile. The tile includes the empty margin of four squares that scanners need, so keep the
// colours far apart (dark on light). `dots` draws the squares as round dots, except in the three big corner marks.
void drawQr(Ctx &c, const QRectF &tile, const QColor &light, const QColor &dark, bool dots, double radius)
{
	const QrCode &code = qrFor(c.x.note);
	fillRect(c.p, tile, light, radius);
	if (code.empty()) {
		drawText(c.p, tile, QStringLiteral("LINK"), c.font(c.t.display, tile.width() * 0.16),
			 withOpacity(dark, 0.35), Qt::AlignCenter);
		return;
	}
	const double cell = tile.width() / double(code.size + 8);
	const double left = tile.left() + 4 * cell, top = tile.top() + (tile.height() - cell * code.size) / 2.0;
	auto inMark = [&](int x, int y) {
		return (x < 7 && y < 7) || (x >= code.size - 7 && y < 7) || (x < 7 && y >= code.size - 7);
	};
	QPainterPath path;
	for (int y = 0; y < code.size; ++y) {
		for (int x = 0; x < code.size; ++x) {
			if (!code.dark(x, y))
				continue;
			const QRectF m(left + x * cell, top + y * cell, cell, cell);
			if (dots && !inMark(x, y))
				path.addEllipse(m.adjusted(cell * 0.07, cell * 0.07, -cell * 0.07, -cell * 0.07));
			else
				path.addRect(m);
		}
	}
	c.p.save();
	c.p.setRenderHint(QPainter::Antialiasing, true);
	c.p.setPen(Qt::NoPen);
	c.p.setBrush(dark);
	c.p.drawPath(path);
	c.p.restore();
}

// A small rounded label with tracked capitals, in the accent colour. Returns its width.
double drawChip(Ctx &c, double x, double y, const QString &label, const QColor &fill, const QColor &ink, double delay)
{
	if (!c.has(label))
		return 0.0;
	const QFont f = c.font(FontFace::BarlowCondensedBold, 24);
	const QString text = label.toUpper();
	const double w = textWidth(f, text) + 3.0 * text.size() + 38;
	c.p.save();
	c.p.setOpacity(c.p.opacity() * c.part(delay, 0.4));
	fillRect(c.p, QRectF(x, y, w, 40), fill, 20);
	drawText(c.p, QRectF(x + 2, y, w, 40), text, f, ink, Qt::AlignCenter, 1.0, 3.0);
	c.p.restore();
	return w;
}

// The words beside or under a code: title, description (up to `lines` lines) and the short link. Returns the
// bottom of what was drawn.
double quickWords(Ctx &c, double x, double y, double w, double titlePx, double descPx, int lines, const QColor &title,
		  const QColor &desc, const QColor &link, Qt::Alignment align = Qt::AlignLeft)
{
	const QFont ft = fitFont(c, c.t.display, titlePx, titlePx * 0.55, c.x.line1, w);
	const double th = titlePx * 1.15;
	drawText(c.p, QRectF(x, y, w, th), c.x.line1, ft, title, align | Qt::AlignVCenter, c.part(0.2, 0.4));
	y += th + 6;
	if (c.has(c.x.line2)) {
		const Wrapped d = fitWrapped(c, c.t.body, descPx, descPx * 0.7, c.x.line2, w, lines, 1.3);
		for (int i = 0; i < d.lines.size(); ++i)
			drawText(c.p, QRectF(x, y + i * d.lineHeight, w, d.lineHeight), d.lines[i], d.font, desc,
				 align | Qt::AlignVCenter, c.part(0.3, 0.4));
		y += d.lineHeight * d.lines.size() + 8;
	}
	if (c.has(c.x.note)) {
		const QFont fl = c.font(FontFace::BarlowSemiBold, descPx * 0.9);
		drawText(c.p, QRectF(x, y, w, descPx * 1.3), elided(fl, shortLink(c.x.note), w), fl, link,
			 align | Qt::AlignVCenter, c.part(0.4, 0.4));
		y += descPx * 1.3;
	}
	return y;
}

// Quick Link 1, Card: a cream card, the code on the left, words on the right, an accent line down the edge.
void quickLinkCard(Ctx &c)
{
	const double w = 900, h = 300, pad = 30, qr = h - 2 * pad;
	const double y = c.bottom() - h, e = c.part(0.0, 0.55);
	c.p.save();
	c.p.setClipRect(QRectF(kLeft - 10, y - 30, (w + 40) * e, h + 60));
	solidShape(c, QRectF(kLeft, y, w, h), c.panel, 28);
	fillRect(c.p, QRectF(kLeft, y + 28, 8, h - 56), c.accent);
	drawQr(c, QRectF(kLeft + pad + 10, y + pad, qr, qr), c.shape2, c.ink, false, 18);
	const double tx = kLeft + pad + 10 + qr + 36, tw = w - (tx - kLeft) - pad;
	double ty = y + pad - 6;
	if (c.has(c.x.tag)) {
		drawChip(c, tx, ty, c.x.tag, c.accent, QColor(255, 255, 255), 0.15);
		ty += 52;
	}
	quickWords(c, tx, ty, tw, 54, 26, 3, c.text, c.sub, c.accent);
	c.p.restore();
}

// Quick Link 2, Bar: a slim dark bar across the lower third, the code standing out of its left end.
void quickLinkBar(Ctx &c)
{
	const double w = 1700, h = 190, qr = 230;
	const double y = c.bottom() - h, e = c.part(0.0, 0.5);
	c.p.save();
	c.p.setClipRect(QRectF(kLeft - 10, y - qr, (w + 40) * e, h + qr + 60));
	solidShape(c, QRectF(kLeft, y, w, h), c.panel, 22);
	fillRect(c.p, QRectF(kLeft, y, w, 8), c.accent, 4);
	drawQr(c, QRectF(kLeft + 24, y + h - qr - 14 + 0.0, qr, qr), c.shape2, c.ink, false, 16);
	const double tx = kLeft + 24 + qr + 40, tw = w - (tx - kLeft) - 260;
	quickWords(c, tx, y + 26, tw, 60, 26, 2, c.text, c.sub, c.accent);
	if (c.has(c.x.tag)) {
		const QFont f = c.font(FontFace::BarlowCondensedBold, 34);
		const QString text = c.x.tag.toUpper();
		const double cw = textWidth(f, text) + 3.0 * text.size() + 56;
		const QRectF chip(kLeft + w - cw - 30, y + (h - 70) / 2, cw, 70);
		c.p.save();
		c.p.setOpacity(c.p.opacity() * c.part(0.3, 0.4));
		fillRect(c.p, chip, c.accent, 35);
		drawText(c.p, chip, text, f, QColor(255, 255, 255), Qt::AlignCenter, 1.0, 3.0);
		c.p.restore();
	}
	c.p.restore();
}

// Quick Link 3, Poster: a tall card, a tab on top, the code in the middle, the words underneath, centred.
void quickLinkPoster(Ctx &c)
{
	const double w = 440, pad = 36, qr = w - 2 * pad, tabH = 64;
	const double h = tabH + 24 + qr + 24 + 190;
	const double y = c.bottom() - h, e = c.part(0.0, 0.55);
	c.p.save();
	c.p.setClipRect(QRectF(kLeft - 10, y + h * (1 - e) - 10, w + 20, h * e + 60));
	solidShape(c, QRectF(kLeft, y, w, h), c.panel, 30);
	c.p.save();
	c.p.setClipRect(QRectF(kLeft, y, w, tabH));
	fillRect(c.p, QRectF(kLeft, y, w, tabH + 30), c.accent, 30);
	c.p.restore();
	drawText(c.p, QRectF(kLeft, y, w, tabH), c.x.tag.toUpper(), c.font(FontFace::BarlowCondensedBold, 34),
		 QColor(255, 255, 255), Qt::AlignCenter, c.part(0.15, 0.4), 4.0);
	drawQr(c, QRectF(kLeft + pad, y + tabH + 24, qr, qr), c.shape2, c.ink, false, 20);
	quickWords(c, kLeft + pad, y + tabH + 24 + qr + 14, qr, 46, 24, 3, c.text, c.sub, c.accent, Qt::AlignHCenter);
	c.p.restore();
}

// Quick Link 4, Glass: a frosted dark panel with a pale edge; the code sits on a white tile at its right.
void quickLinkGlass(Ctx &c)
{
	const double w = 1000, h = 290, pad = 34, qr = h - 2 * pad;
	const double y = c.bottom() - h, e = c.part(0.0, 0.55);
	c.p.save();
	c.p.setOpacity(c.p.opacity() * e);
	c.p.translate(0, 26 * (1 - e));
	const QRectF panel(kLeft, y, w, h);
	fillRect(c.p, panel, c.panel, 34);
	QLinearGradient sheen(panel.topLeft(), panel.bottomRight());
	sheen.setColorAt(0.0, QColor(255, 255, 255, 70));
	sheen.setColorAt(0.45, QColor(255, 255, 255, 8));
	sheen.setColorAt(1.0, QColor(255, 255, 255, 30));
	c.p.setPen(QPen(QColor(255, 255, 255, 120), 2));
	c.p.setBrush(sheen);
	c.p.setRenderHint(QPainter::Antialiasing, true);
	c.p.drawRoundedRect(panel.adjusted(1, 1, -1, -1), 34, 34);
	double ty = y + pad;
	if (c.has(c.x.tag)) {
		drawChip(c, kLeft + pad + 6, ty, c.x.tag, c.accent, QColor(20, 20, 26), 0.15);
		ty += 52;
	}
	quickWords(c, kLeft + pad + 6, ty, w - 2 * pad - qr - 40, 54, 26, 3, c.text, c.sub, c.accent);
	drawQr(c, QRectF(kLeft + w - pad - qr, y + pad, qr, qr), c.shape2, c.ink, false, 22);
	c.p.restore();
}

// Quick Link 5, Dots: a round-dot code in a ring, a round "scan me" badge on its corner, words underneath.
void quickLinkDots(Ctx &c)
{
	const double w = 400, pad = 30, qr = w - 2 * pad;
	const double h = pad + qr + 20 + 180;
	const double y = c.bottom() - h, e = c.partBack(0.0, 0.6);
	c.p.save();
	c.p.translate(0, 40 * (1 - c.part(0.0, 0.5)));
	c.p.setOpacity(c.p.opacity() * std::min(1.0, e));
	solidShape(c, QRectF(kLeft, y, w, h), c.panel, 36);
	c.p.setRenderHint(QPainter::Antialiasing, true);
	c.p.setPen(QPen(c.accent, 6));
	c.p.setBrush(Qt::NoBrush);
	c.p.drawRoundedRect(QRectF(kLeft + pad - 8, y + pad - 8, qr + 16, qr + 16), 30, 30);
	drawQr(c, QRectF(kLeft + pad, y + pad, qr, qr), c.shape2, c.ink, true, 24);
	if (c.has(c.x.tag)) {
		const double d = 118;
		const QRectF badge(kLeft + w - d + 22, y - d / 2 + 6, d, d);
		c.p.save();
		c.p.translate(badge.center());
		c.p.rotate(10);
		c.p.translate(-badge.center());
		c.p.setPen(Qt::NoPen);
		c.p.setBrush(c.accent);
		c.p.drawEllipse(badge);
		const Wrapped t = fitWrapped(c, FontFace::BarlowCondensedBold, 32, 20, c.x.tag.toUpper(), d - 26, 2, 1.0);
		for (int i = 0; i < t.lines.size(); ++i)
			drawText(c.p, QRectF(badge.left(), badge.center().y() - t.lineHeight * t.lines.size() / 2 + i * t.lineHeight,
					     d, t.lineHeight),
				 t.lines[i], t.font, QColor(255, 255, 255), Qt::AlignCenter);
		c.p.restore();
	}
	quickWords(c, kLeft + pad, y + pad + qr + 14, qr, 42, 22, 2, c.text, c.sub, c.accent, Qt::AlignHCenter);
	c.p.restore();
}

// ---- Logo + wordmark ------------------------------------------------------------------------------------

// A stand-in lockup (a round mark and a word) for when no logo picture has been chosen yet.
QImage lockupPlaceholder(const Ctx &c)
{
	QImage im(520, 150, QImage::Format_ARGB32_Premultiplied);
	im.fill(Qt::transparent);
	QPainter q(&im);
	q.setRenderHint(QPainter::Antialiasing, true);
	q.setRenderHint(QPainter::TextAntialiasing, true);
	q.setPen(Qt::NoPen);
	q.setBrush(c.accent);
	q.drawEllipse(QRectF(8, 8, 134, 134));
	q.setBrush(QColor(255, 255, 255));
	q.drawEllipse(QRectF(52, 52, 46, 46));
	q.setFont(c.font(c.t.display, 62));
	q.setPen(c.text);
	q.drawText(QRectF(170, 0, 350, 150), Qt::AlignVCenter | Qt::AlignLeft, QStringLiteral("YOUR BRAND"));
	return im;
}

// Logo mark + wordmark: the wordmark slides away behind the mark, leaving only the mark, then slides back out.
void logoLockup(Ctx &c)
{
	QImage img = logoImage(c.x.logoPath);
	int cut = c.x.logoCut;
	if (img.isNull()) {
		img = lockupPlaceholder(c);
		cut = 30;
	}
	const double iw = img.width(), ih = img.height();
	const double sc = std::min(620.0 / iw, 190.0 / ih);
	const QRectF whole(kLeft, 64 + (190.0 - ih * sc) / 2.0, iw * sc, ih * sc);
	const double a = c.part(0.0, 0.6);
	c.p.save();
	c.p.setRenderHint(QPainter::SmoothPixmapTransform, true);
	c.p.setOpacity(c.p.opacity() * a);
	if (cut <= 0) { // no parts chosen yet: the logo as one still piece
		c.p.drawImage(whole, img);
		c.p.restore();
		return;
	}
	const double f = std::clamp(cut / 100.0, 0.1, 0.9);
	const bool stacked = c.x.logoStacked;
	QRectF srcA, srcB, dstA, dstB;
	if (!stacked) {
		const double cx = iw * f;
		srcA = QRectF(0, 0, cx, ih);
		srcB = QRectF(cx, 0, iw - cx, ih);
		dstA = QRectF(whole.left(), whole.top(), cx * sc, whole.height());
		dstB = QRectF(whole.left() + cx * sc, whole.top(), (iw - cx) * sc, whole.height());
	} else {
		const double cy = ih * f;
		srcA = QRectF(0, 0, iw, cy);
		srcB = QRectF(0, cy, iw, ih - cy);
		dstA = QRectF(whole.left(), whole.top(), whole.width(), cy * sc);
		dstB = QRectF(whole.left(), whole.top() + cy * sc, whole.width(), (ih - cy) * sc);
	}
	const bool markFirst = !c.x.logoMarkSecond;
	const QRectF srcM = markFirst ? srcA : srcB, srcW = markFirst ? srcB : srcA;
	const QRectF dstM = markFirst ? dstA : dstB, dstW = markFirst ? dstB : dstA;

	// How far the wordmark has tucked away: 0 out, 1 hidden. Hold, slide away, hold, slide back, hold.
	const double ph = logoPhase(c);
	double s = 0.0, bump = 0.0;
	if (ph >= 0.35 && ph < 0.5) {
		s = easeInOut((ph - 0.35) / 0.15);
		bump = std::sin((ph - 0.35) / 0.15 * kPi);
	} else if (ph >= 0.5 && ph < 0.75) {
		s = 1.0;
	} else if (ph >= 0.75 && ph < 0.9) {
		s = 1.0 - easeInOut((ph - 0.75) / 0.15);
		bump = std::sin((ph - 0.75) / 0.15 * kPi);
	}

	// The wordmark moves toward the mark and is hidden by it, so it seems to slide in behind.
	const QPointF dir = stacked ? QPointF(0, markFirst ? -1 : 1) : QPointF(markFirst ? -1 : 1, 0);
	const double travel = stacked ? dstW.height() : dstW.width();
	QPainterPath clip;
	clip.addRect(whole.adjusted(-2, -2, 2, 2));
	QPainterPath markArea;
	markArea.addRect(dstM);
	c.p.save();
	c.p.setClipPath(clip.subtracted(markArea), Qt::IntersectClip);
	c.p.setOpacity(c.p.opacity() * (1.0 - s * s));
	c.p.drawImage(dstW.translated(dir * travel * s), img, srcW);
	c.p.restore();

	// While the mark is alone, a soft band of light crosses it and it gives one small pop, then the wordmark
	// comes back out from behind it.
	const double sweepU = (ph >= 0.52 && ph < 0.72) ? (ph - 0.52) / 0.20 : -1.0;
	const double pop = sweepU >= 0.0 ? std::sin(sweepU * kPi) : 0.0;
	const double pixelScale = c.p.transform().m11();

	// The mark stays, with a very small swell as the wordmark tucks in or comes out.
	c.p.translate(dstM.center());
	const double swell = 1.0 + 0.05 * bump + 0.045 * pop;
	c.p.scale(swell, swell);
	c.p.translate(-dstM.center());
	if (sweepU < 0.0) {
		c.p.drawImage(dstM, img, srcM);
	} else {
		QImage layer(std::max(1, int(std::ceil(dstM.width() * pixelScale))),
			     std::max(1, int(std::ceil(dstM.height() * pixelScale))),
			     QImage::Format_ARGB32_Premultiplied);
		layer.fill(Qt::transparent);
		QPainter q(&layer);
		q.setRenderHint(QPainter::SmoothPixmapTransform, true);
		q.drawImage(QRectF(0, 0, layer.width(), layer.height()), img, srcM);
		q.setCompositionMode(QPainter::CompositionMode_SourceAtop); // light only lands on the mark itself
		const double lw = layer.width(), lh = layer.height();
		const double bw = lw * 0.6;
		const double x = -bw + (lw + 2 * bw) * easeInOut(sweepU);
		QLinearGradient g(x, 0, x + bw, 0);
		// Silver edges around a bright core, so it shows on dark, coloured and white marks alike.
		g.setColorAt(0.0, QColor(150, 158, 170, 0));
		g.setColorAt(0.3, QColor(125, 133, 148, 200));
		g.setColorAt(0.5, QColor(255, 255, 255, 235));
		g.setColorAt(0.7, QColor(125, 133, 148, 200));
		g.setColorAt(1.0, QColor(150, 158, 170, 0));
		q.setPen(Qt::NoPen);
		q.setBrush(g);
		QPolygonF band;
		band << QPointF(x + bw * 0.25, 0) << QPointF(x + bw * 1.25, 0) << QPointF(x + bw * 0.75, lh)
		     << QPointF(x - bw * 0.25, lh);
		q.drawPolygon(band);
		q.end();
		c.p.setRenderHint(QPainter::SmoothPixmapTransform, true);
		c.p.drawImage(dstM, layer);
	}
	c.p.restore();
}

// ---- More cards: coming up, event date, prayer, welcome, celebrate, question, hashtag ---------------------

// 31. Coming Up: a tag block ("UP NEXT"), the title, and a time in a pill.
void comingUp(Ctx &c)
{
	const double h = 104, padX = c.pad(34), tagPad = 28;
	const QString tag = (c.has(c.x.tag) ? c.x.tag : QStringLiteral("Up next")).toUpper();
	const QFont ft = c.font(c.t.display, 30);
	const QFont f1 = fitFont(c, c.t.display, 54, 34, c.x.line1, 900);
	const QFont f2 = c.font(c.t.display, 32);
	const double tagW = textWidth(ft, tag) + tag.size() * 2.0 + 2 * tagPad;
	const bool when = c.has(c.x.line2);
	const QString time = c.x.line2.toUpper();
	const double timeW = when ? textWidth(f2, time) + time.size() * 1.5 + 44 : 0.0;
	const double w1 = textWidth(f1, c.x.line1);
	const double w = tagW + padX + w1 + (when ? 30 + timeW : 0.0) + padX;
	const double y = c.bottom() - h;
	const double pw = w * c.part(0.0, 0.6);
	fillPanel(c, QRectF(kLeft, y, pw, h));
	c.p.save();
	c.p.setClipRect(QRectF(kLeft, y, pw, h));
	fillRect(c.p, QRectF(kLeft, y, tagW, h), c.accent);
	drawText(c.p, QRectF(kLeft, y, tagW, h), tag, ft, c.ink, Qt::AlignCenter, c.part(0.2, 0.4), 2.0);
	const double e1 = c.part(0.3, 0.5), e2 = c.part(0.42, 0.45);
	const double tx = kLeft + tagW + padX;
	drawText(c.p, QRectF(tx + 26 * (1 - e1), y, w1 + 20, h), c.x.line1, f1, c.text,
		 Qt::AlignLeft | Qt::AlignVCenter, e1);
	if (when) {
		const QRectF chip(tx + w1 + 30, y + (h - 54) / 2.0, timeW, 54);
		fillRect(c.p, chip.translated(20 * (1 - e2), 0), withOpacity(c.accent, 0.22 * e2), 27);
		drawText(c.p, chip.translated(20 * (1 - e2), 0), time, f2, c.accent, Qt::AlignCenter, e2, 1.5);
	}
	c.p.restore();
}

// 32. Event Date: a date tile (month over day) beside the event's name and details. The tag holds the date: "Oct 12".
void eventDate(Ctx &c)
{
	const double h = 118, pad = 16, tile = h - 2 * pad, gap = 28, padR = 36;
	const QStringList parts = c.x.tag.trimmed().toUpper().split(' ', Qt::SkipEmptyParts);
	const QString month = parts.value(0, QStringLiteral("SUN"));
	const QString day = parts.size() > 1 ? parts.mid(1).join(' ') : QString();
	const QFont f1 = fitFont(c, c.t.display, 52, 32, c.x.line1, 900);
	const QFont f2 = c.font(c.t.body, 28);
	const double textW = std::max(textWidth(f1, c.x.line1), textWidth(f2, c.x.line2));
	const double w = pad + tile + gap + textW + padR;
	const double y = c.bottom() - h;
	const double e = c.part(0.0, 0.55);
	fillPanel(c, QRectF(kLeft, y, w * e, h), c.t.cornerRadius);
	c.p.save();
	c.p.setClipRect(QRectF(kLeft, y, w * e + 1, h));
	fillRect(c.p, QRectF(kLeft, y, w, h), c.panel, c.t.cornerRadius);
	const double pop = c.partBack(0.1, 0.45);
	const QRectF box(kLeft + pad, y + pad, tile, tile);
	c.p.save();
	c.p.translate(box.center());
	c.p.scale(std::max(pop, 0.001), std::max(pop, 0.001));
	c.p.translate(-box.center());
	fillRect(c.p, box, c.accent, 14);
	if (day.isEmpty()) {
		drawText(c.p, box, month, c.font(c.t.display, 40), c.ink, Qt::AlignCenter, 1.0, 1.0);
	} else {
		drawText(c.p, QRectF(box.left(), box.top() + 6, box.width(), 26), month, c.font(c.t.display, 24), c.ink,
			 Qt::AlignCenter, 1.0, 2.0);
		drawText(c.p, QRectF(box.left(), box.top() + 28, box.width(), box.height() - 30), day,
			 c.font(c.t.display, 56), c.ink, Qt::AlignCenter);
	}
	c.p.restore();
	const double tx = kLeft + pad + tile + gap;
	const double e1 = c.part(0.25, 0.5), e2 = c.part(0.36, 0.5);
	if (c.has(c.x.line2)) {
		drawText(c.p, QRectF(tx + 24 * (1 - e1), y + 10, textW + 40, 62), c.x.line1, f1, c.text,
			 Qt::AlignLeft | Qt::AlignVCenter, e1);
		drawText(c.p, QRectF(tx + 24 * (1 - e2), y + 70, textW + 40, 36), c.x.line2, f2, c.sub,
			 Qt::AlignLeft | Qt::AlignVCenter, e2);
	} else {
		drawText(c.p, QRectF(tx + 24 * (1 - e1), y, textW + 40, h), c.x.line1, f1, c.text,
			 Qt::AlignLeft | Qt::AlignVCenter, e1);
	}
	c.p.restore();
}

// 33. Prayer Request: a soft card with a "Pray with us" label, the request in a few lines, and who it is for.
void prayerRequest(Ctx &c)
{
	const double padX = c.pad(42), padY = 26;
	const QString tag = (c.has(c.x.tag) ? c.x.tag : QStringLiteral("Pray with us")).toUpper();
	const QFont ft = c.font(c.t.display, 26);
	const QFont fa = c.font(c.t.body, 28);
	const Wrapped q = fitWrapped(c, c.t.display, 46, 30, c.x.line1, 1000, 3, 1.28);
	double textW = textWidth(ft, tag) + tag.size() * 2.0 + 44;
	for (const QString &line : q.lines)
		textW = std::max(textW, textWidth(q.font, line));
	const bool who = c.has(c.x.line2);
	if (who)
		textW = std::max(textW, textWidth(fa, c.x.line2) + 40);
	const double chipH = 40, attr = who ? 46 : 0;
	const double h = padY + chipH + 16 + q.lines.size() * q.lineHeight + (who ? 8 + attr : 0) + padY;
	const double w = std::max(560.0, 2 * padX + textW);
	const double y = c.bottom() - h;
	const double e = c.part(0.0, 0.6);
	fillPanel(c, QRectF(kLeft, y, w * e, h), c.t.cornerRadius);
	c.p.save();
	c.p.setClipRect(QRectF(kLeft, y, w * e + 1, h));
	fillRect(c.p, QRectF(kLeft, y, w, h), c.panel, c.t.cornerRadius);
	const double e0 = c.part(0.2, 0.4);
	const double chipW = textWidth(ft, tag) + tag.size() * 2.0 + 44;
	fillRect(c.p, QRectF(kLeft + padX, y + padY, chipW, chipH), withOpacity(c.accent, e0), chipH / 2);
	drawText(c.p, QRectF(kLeft + padX, y + padY, chipW, chipH), tag, ft, c.ink, Qt::AlignCenter, e0, 2.0);
	const double top = y + padY + chipH + 16;
	for (int i = 0; i < q.lines.size(); ++i) {
		const double ei = c.part(0.3 + 0.07 * i, 0.5);
		drawText(c.p, QRectF(kLeft + padX, top + i * q.lineHeight + 14 * (1 - ei), w, q.lineHeight), q.lines[i],
			 q.font, c.text, Qt::AlignLeft | Qt::AlignVCenter, ei);
	}
	if (who) {
		const double ew = c.part(0.55, 0.45);
		drawText(c.p, QRectF(kLeft + padX, top + q.lines.size() * q.lineHeight + 8, w, attr), c.x.line2, fa,
			 c.sub, Qt::AlignLeft | Qt::AlignVCenter, ew);
	}
	c.p.restore();
}

// 34. Welcome: big words straight on the picture (with a soft shadow), an accent rule growing under them.
void welcome(Ctx &c)
{
	const QFont f1 = fitFont(c, c.t.display, 118, 56, c.x.line1, 1500);
	const QFont f2 = fitFont(c, c.t.body, 44, 26, c.x.line2, 1500);
	const bool two = c.has(c.x.line2);
	const double h1 = 138, h2 = two ? 64 : 0;
	const double top = c.bottom() - h1 - h2 - 36;
	const double e1 = c.part(0.0, 0.55), er = c.part(0.18, 0.5), e2 = c.part(0.32, 0.5);
	drawShadowText(c.p, QRectF(kLeft + 44 * (1 - e1), top, 1650, h1), c.x.line1, f1, c.text,
		       Qt::AlignLeft | Qt::AlignVCenter, e1);
	fillRect(c.p, QRectF(kLeft + 4, top + h1 + 6, 240 * er, 8), c.accent, 4);
	if (two)
		drawShadowText(c.p, QRectF(kLeft + 30 * (1 - e2), top + h1 + 26, 1650, h2), c.x.line2, f2, c.sub,
			       Qt::AlignLeft | Qt::AlignVCenter, e2);
}

// 35. Celebrate: a light card for a birthday or an anniversary, with confetti that pops around it.
void celebrate(Ctx &c)
{
	const double padX = c.pad(46);
	const bool two = c.has(c.x.line2);
	const double h = two ? 150 : 118;
	const QString tag = (c.has(c.x.tag) ? c.x.tag : QStringLiteral("Happy birthday")).toUpper();
	const QFont ft = c.font(c.t.display, 28);
	const QFont f1 = fitFont(c, c.t.display, 60, 36, c.x.line1, 1000);
	const QFont f2 = c.font(c.t.body, 30);
	const double tabH = 46, tabW = textWidth(ft, tag) + tag.size() * 2.0 + 56;
	const double w =
		std::max({560.0, 2 * padX + textWidth(f1, c.x.line1), 2 * padX + textWidth(f2, c.x.line2), tabW + 40});
	const double y = c.bottom() - h;
	const double e = c.part(0.0, 0.6);
	fillPanel(c, QRectF(kLeft, y, w * e, h), c.t.cornerRadius);
	c.p.save();
	c.p.setClipRect(QRectF(kLeft, y - tabH, w * e + 1, h + tabH));
	fillRect(c.p, QRectF(kLeft, y, w, h), c.panel, c.t.cornerRadius);
	const double et = c.part(0.2, 0.4);
	fillRect(c.p, QRectF(kLeft, y - tabH + 8 * (1 - et), tabW, tabH), c.accent, 12);
	drawText(c.p, QRectF(kLeft, y - tabH + 8 * (1 - et), tabW, tabH), tag, ft, c.ink, Qt::AlignCenter, et, 2.0);
	const double e1 = c.part(0.3, 0.5), e2 = c.part(0.42, 0.5);
	if (two) {
		drawText(c.p, QRectF(kLeft + padX + 26 * (1 - e1), y + 12, w, 76), c.x.line1, f1, c.text,
			 Qt::AlignLeft | Qt::AlignVCenter, e1);
		drawText(c.p, QRectF(kLeft + padX + 26 * (1 - e2), y + 90, w, 42), c.x.line2, f2, c.sub,
			 Qt::AlignLeft | Qt::AlignVCenter, e2);
	} else {
		drawText(c.p, QRectF(kLeft + padX + 26 * (1 - e1), y, w, h), c.x.line1, f1, c.text,
			 Qt::AlignLeft | Qt::AlignVCenter, e1);
	}
	c.p.restore();
	// Confetti: the same pieces every time, so a title never flickers; they pop in one after another.
	const QColor colours[4] = {QColor(0x2E, 0x8B, 0xFF), QColor(0x2F, 0xD1, 0x8B), QColor(0xFF, 0xB3, 0x1A),
				   QColor(0xFF, 0x4D, 0x2E)};
	unsigned seed = 7;
	const auto next = [&seed]() {
		seed = seed * 1103515245u + 12345u;
		return double((seed >> 16) & 0x7FFF) / 32767.0;
	};
	for (int i = 0; i < 18; ++i) {
		const double fx = next(), fy = next(), rot = next() * 90.0, size = 9 + next() * 8;
		const double pop = c.partBack(0.25 + 0.02 * i, 0.4);
		if (pop <= 0.01)
			continue;
		const QPointF at(kLeft - 24 + fx * (w + 48), y - tabH - 34 + fy * (h + tabH + 60));
		if (QRectF(kLeft - 6, y - 6, w + 12, h + 12).contains(at))
			continue; // confetti goes around the card, never over its words
		c.p.save();
		c.p.translate(at);
		c.p.rotate(rot);
		c.p.scale(pop, pop);
		c.p.setPen(Qt::NoPen);
		c.p.setBrush(colours[i % 4]);
		if (i % 3 == 0)
			c.p.drawEllipse(QPointF(0, 0), size / 2, size / 2);
		else
			c.p.drawRoundedRect(QRectF(-size / 2, -size / 4, size, size / 2), 2, 2);
		c.p.restore();
	}
}

// 36. Question: a speech bubble for a viewer's question, with who asked it underneath.
void question(Ctx &c)
{
	const double padX = c.pad(42), padY = 24;
	const QString tag = (c.has(c.x.tag) ? c.x.tag : QStringLiteral("Question")).toUpper();
	const QFont ft = c.font(c.t.display, 26);
	const QFont fa = c.font(c.t.body, 28);
	const Wrapped q = fitWrapped(c, c.t.display, 46, 30, c.x.line1, 1000, 3, 1.28);
	double textW = 0;
	for (const QString &line : q.lines)
		textW = std::max(textW, textWidth(q.font, line));
	const bool who = c.has(c.x.line2);
	const double chipH = 40, attr = who ? 44 : 0;
	const double h = padY + chipH + 12 + q.lines.size() * q.lineHeight + (who ? 6 + attr : 0) + padY;
	const double w = std::max(520.0, 2 * padX + textW);
	const double y = c.bottom() - h - 22; // room for the bubble's tail
	const double e = c.part(0.0, 0.6);
	c.p.save();
	c.p.translate(kLeft, y + h);
	const double s = 0.9 + 0.1 * e;
	c.p.scale(s, s);
	c.p.translate(-kLeft, -(y + h));
	c.p.setOpacity(c.p.opacity() * std::clamp(e * 1.4, 0.0, 1.0));
	fillPanel(c, QRectF(kLeft, y, w, h), 26);
	QPolygonF tail;
	tail << QPointF(kLeft + 46, y + h - 2) << QPointF(kLeft + 92, y + h - 2) << QPointF(kLeft + 40, y + h + 24);
	c.p.setPen(Qt::NoPen);
	c.p.setBrush(c.panel);
	c.p.drawPolygon(tail);
	const double chipW = textWidth(ft, tag) + tag.size() * 2.0 + 44;
	fillRect(c.p, QRectF(kLeft + padX, y + padY, chipW, chipH), c.accent, chipH / 2);
	drawText(c.p, QRectF(kLeft + padX, y + padY, chipW, chipH), tag, ft, c.ink, Qt::AlignCenter, 1.0, 2.0);
	const double top = y + padY + chipH + 12;
	for (int i = 0; i < q.lines.size(); ++i) {
		const double ei = c.part(0.25 + 0.07 * i, 0.5);
		drawText(c.p, QRectF(kLeft + padX, top + i * q.lineHeight + 12 * (1 - ei), w, q.lineHeight), q.lines[i],
			 q.font, c.text, Qt::AlignLeft | Qt::AlignVCenter, ei);
	}
	if (who)
		drawText(c.p, QRectF(kLeft + padX, top + q.lines.size() * q.lineHeight + 6, w, attr),
			 QStringLiteral("— ") + c.x.line2, fa, c.sub, Qt::AlignLeft | Qt::AlignVCenter,
			 c.part(0.5, 0.45));
	c.p.restore();
}

// 37. Hashtag: an accent square with a hash sign, then the tag in big letters, and a small line above it.
void hashtagBar(Ctx &c)
{
	const double h = 100, padX = c.pad(34);
	const QFont f1 = fitFont(c, c.t.display, 58, 34, c.x.line1, 900);
	const QFont ftag = c.font(c.t.body, 22);
	const bool above = c.has(c.x.tag);
	const double w1 = textWidth(f1, c.x.line1);
	const double w =
		h + padX + std::max(w1, above ? textWidth(ftag, c.x.tag.toUpper()) + c.x.tag.size() * 3.0 : 0.0) + padX;
	const double y = c.bottom() - h;
	const double e = c.part(0.0, 0.55);
	fillPanel(c, QRectF(kLeft, y, w * e, h), c.t.cornerRadius);
	c.p.save();
	c.p.setClipRect(QRectF(kLeft, y, w * e + 1, h));
	fillRect(c.p, QRectF(kLeft, y, w, h), c.panel, c.t.cornerRadius);
	const double pop = c.partBack(0.08, 0.45);
	const QRectF box(kLeft, y, h, h);
	c.p.save();
	c.p.translate(box.center());
	c.p.scale(std::max(pop, 0.001), std::max(pop, 0.001));
	c.p.translate(-box.center());
	fillRect(c.p, box, c.accent, c.t.cornerRadius);
	drawText(c.p, box, QStringLiteral("#"), c.font(c.t.display, 72), c.ink, Qt::AlignCenter);
	c.p.restore();
	const double e1 = c.part(0.25, 0.5);
	const double tx = kLeft + h + padX;
	if (above) {
		drawText(c.p, QRectF(tx + 22 * (1 - e1), y + 10, w, 30), c.x.tag.toUpper(), ftag, c.sub,
			 Qt::AlignLeft | Qt::AlignVCenter, e1, 3.0);
		drawText(c.p, QRectF(tx + 22 * (1 - e1), y + 36, w, 56), c.x.line1, f1, c.text,
			 Qt::AlignLeft | Qt::AlignVCenter, e1);
	} else {
		drawText(c.p, QRectF(tx + 22 * (1 - e1), y, w, h), c.x.line1, f1, c.text,
			 Qt::AlignLeft | Qt::AlignVCenter, e1);
	}
	c.p.restore();
}

// ---- Sermon notes -------------------------------------------------------------------------------------

// Words between asterisks are drawn in the accent colour: "God is *faithful*".
struct Piece {
	QString text;
	bool marked = false;
};

QList<Piece> markedPieces(const QString &s)
{
	QList<Piece> out;
	bool marked = false;
	QString cur;
	for (const QChar ch : s) {
		if (ch == QLatin1Char('*')) {
			if (!cur.isEmpty())
				out.append({cur, marked});
			cur.clear();
			marked = !marked;
		} else {
			cur += ch;
		}
	}
	if (!cur.isEmpty())
		out.append({cur, marked});
	return out;
}

QString stripMarks(QString s)
{
	return s.remove(QLatin1Char('*'));
}

// The points a notes card shows: rows that have text.
QList<QPair<QString, QString>> notePoints(const Ctx &c)
{
	QList<QPair<QString, QString>> out;
	for (const auto &r : c.x.rows)
		if (c.has(r.second))
			out.append(r);
	return out;
}

// Sermon Notes: a card with the sermon's title and its points, each with a marker.
void notesCard(Ctx &c)
{
	const auto points = notePoints(c);
	const double pad = 40, w = 820, markerW = 44;
	const QFont ft = c.font(c.t.body, 22);
	const QFont fTitle = fitFont(c, c.t.display, 54, 32, c.x.line1, w - 2 * pad);
	const double textW = w - 2 * pad - markerW - 16;
	std::vector<Wrapped> wrapped;
	double pointsH = 0.0;
	for (const auto &pt : points) {
		wrapped.push_back(fitWrapped(c, c.t.body, 32, 24, stripMarks(pt.second), textW, 3, 1.25));
		pointsH += std::max(markerW, wrapped.back().lineHeight * wrapped.back().lines.size()) + 16;
	}
	const double headH = (c.has(c.x.tag) ? 36 : 0.0) + 70;
	const double h = pad + headH + (points.isEmpty() ? 0.0 : 16 + pointsH) + pad - 16;
	const double y = c.bottom() - h;
	const double e = c.part(0.0, 0.55);
	const QRectF card(kLeft, y, w, h);
	c.p.save();
	c.p.setClipRect(QRectF(kLeft, y - 20 + h * (1 - e), w + 20, h + 40));
	solidShape(c, card, c.panel, 26);
	fillRect(c.p, QRectF(kLeft, y + 26, 8, h - 52), c.accent);
	double cy = y + pad;
	if (c.has(c.x.tag)) {
		drawText(c.p, QRectF(kLeft + pad, cy - 8, w, 30), c.x.tag.toUpper(), ft, c.accent,
			 Qt::AlignLeft | Qt::AlignVCenter, c.part(0.2, 0.4), 3.0);
		cy += 36;
	}
	drawText(c.p, QRectF(kLeft + pad, cy - 6, w - 2 * pad, 70), c.x.line1, fTitle, c.text,
		 Qt::AlignLeft | Qt::AlignVCenter, c.part(0.25, 0.4));
	cy += 70 + 16;
	for (int i = 0; i < points.size(); ++i) {
		const double a = c.part(0.35 + 0.08 * i, 0.4);
		const Wrapped &wp = wrapped[size_t(i)];
		const double rowH = std::max(markerW, wp.lineHeight * wp.lines.size());
		c.p.save();
		c.p.translate(14 * (1 - a), 0);
		c.p.setOpacity(c.p.opacity() * a);
		const QString marker = c.has(points[i].first) ? points[i].first : QString();
		c.p.setPen(Qt::NoPen);
		c.p.setBrush(c.accent);
		c.p.drawEllipse(QRectF(kLeft + pad, cy + (rowH - markerW) / 2, markerW, markerW));
		if (!marker.isEmpty())
			drawText(c.p, QRectF(kLeft + pad, cy + (rowH - markerW) / 2, markerW, markerW), marker,
				 c.font(c.t.display, 26), c.ink, Qt::AlignCenter);
		for (int l = 0; l < wp.lines.size(); ++l)
			drawText(c.p,
				 QRectF(kLeft + pad + markerW + 16,
					cy + (rowH - wp.lineHeight * wp.lines.size()) / 2 + l * wp.lineHeight, textW,
					wp.lineHeight),
				 wp.lines[l], wp.font, c.sub, Qt::AlignLeft | Qt::AlignVCenter);
		c.p.restore();
		cy += rowH + 16;
	}
	c.p.restore();
}

// Numbered Points: a stack of numbered lines, each on its own rounded bar.
void notesPoints(Ctx &c)
{
	const auto points = notePoints(c);
	const double circle = 64, gap = 12, maxW = 880;
	const QFont fhead = fitFont(c, c.t.display, 40, 26, c.x.line1, maxW);
	std::vector<Wrapped> wrapped;
	double h = 0.0;
	for (const auto &pt : points) {
		wrapped.push_back(fitWrapped(c, c.t.body, 34, 24, stripMarks(pt.second), maxW - circle - 60, 2, 1.2));
		h += std::max(circle, wrapped.back().lineHeight * wrapped.back().lines.size() + 20) + gap;
	}
	const double headH = c.has(c.x.line1) ? 64 : 0.0;
	const double y = c.bottom() - h - headH;
	if (c.has(c.x.line1)) {
		const double a = c.part(0.0, 0.45);
		const double tw = textWidth(fhead, c.x.line1) + 2 * 30;
		solidShape(c, QRectF(kLeft, y, tw * a, 56), c.accent, 28);
		drawText(c.p, QRectF(kLeft + 30, y, tw, 56), c.x.line1, fhead, c.ink, Qt::AlignLeft | Qt::AlignVCenter,
			 a);
	}
	double cy = y + headH;
	for (int i = 0; i < points.size(); ++i) {
		const double a = c.part(0.15 + 0.1 * i, 0.4);
		const Wrapped &wp = wrapped[size_t(i)];
		const double rowH = std::max(circle, wp.lineHeight * wp.lines.size() + 20);
		double barW = circle;
		for (const QString &l : wp.lines)
			barW = std::max(barW, textWidth(wp.font, l));
		barW += circle + 50;
		c.p.save();
		c.p.translate(-60 * (1 - a), 0);
		c.p.setOpacity(c.p.opacity() * a);
		solidShape(c, QRectF(kLeft + circle / 2, cy, barW - circle / 2, rowH), c.panel, 16);
		c.p.setPen(Qt::NoPen);
		c.p.setBrush(c.accent);
		const QRectF dot(kLeft, cy + (rowH - circle) / 2, circle, circle);
		c.p.drawEllipse(dot);
		drawText(c.p, dot, c.has(points[i].first) ? points[i].first : QString::number(i + 1),
			 c.font(c.t.display, 34), c.ink, Qt::AlignCenter);
		for (int l = 0; l < wp.lines.size(); ++l)
			drawText(c.p,
				 QRectF(kLeft + circle + 24,
					cy + (rowH - wp.lineHeight * wp.lines.size()) / 2 + l * wp.lineHeight, barW,
					wp.lineHeight),
				 wp.lines[l], wp.font, c.text, Qt::AlignLeft | Qt::AlignVCenter);
		c.p.restore();
		cy += rowH + gap;
	}
}

// Key Point: one big statement; words between asterisks stand out (a fill-in-the-blank or a key word).
void notesKey(Ctx &c)
{
	const double pad = 48, w = 1000;
	const QString plain = stripMarks(c.x.line1);
	const Wrapped wp = fitWrapped(c, c.t.display, 64, 36, plain, w - 2 * pad, 4, 1.22);
	const double headH = c.has(c.x.tag) ? 56 : 0.0;
	const double h = 2 * pad + headH + wp.lineHeight * wp.lines.size();
	const double y = c.bottom() - h;
	const double e = c.part(0.0, 0.55);
	const QRectF card(kLeft, y, w, h);
	c.p.save();
	c.p.setClipRect(QRectF(kLeft, y - 20, (w + 20) * e, h + 40));
	solidShape(c, card, c.panel, 28);
	fillRect(c.p, QRectF(kLeft + pad - 20, y + pad + headH, 8, wp.lineHeight * wp.lines.size()), c.accent);
	if (c.has(c.x.tag)) {
		const QFont ft = c.font(c.t.body, 22);
		const double tw = textWidth(ft, c.x.tag.toUpper()) + 2 * 22 + c.x.tag.size() * 3;
		fillRect(c.p, QRectF(kLeft + pad, y + pad - 8, tw, 38), c.accent, 10);
		drawText(c.p, QRectF(kLeft + pad, y + pad - 8, tw, 38), c.x.tag.toUpper(), ft, c.ink, Qt::AlignCenter,
			 c.part(0.2, 0.4), 3.0);
	}
	// Draw the statement line by line, colouring the marked words (in runs, so kerning is kept).
	const QList<Piece> pieces = markedPieces(c.x.line1);
	QList<bool> markedChar;
	for (const Piece &pc : pieces)
		for (int i = 0; i < pc.text.size(); ++i)
			markedChar.append(pc.marked);
	int consumed = 0;
	const QFontMetricsF fm(wp.font);
	const double a = c.part(0.25, 0.5);
	for (int l = 0; l < wp.lines.size(); ++l) {
		const QString &line = wp.lines[l];
		while (consumed < plain.size() && plain[consumed].isSpace())
			++consumed;
		double x = kLeft + pad + 24;
		const double ly = y + pad + headH + l * wp.lineHeight;
		int start = 0;
		while (start < line.size()) {
			const bool marked = consumed + start < markedChar.size() && markedChar[consumed + start];
			int end = start;
			while (end < line.size() &&
			       (consumed + end < markedChar.size() && markedChar[consumed + end]) == marked)
				++end;
			const QString run = line.mid(start, end - start);
			drawText(c.p, QRectF(x, ly, textWidth(wp.font, run) + 8, wp.lineHeight), run, wp.font,
				 marked ? c.accent : c.text, Qt::AlignLeft | Qt::AlignVCenter, a);
			x += fm.horizontalAdvance(run);
			start = end;
		}
		consumed += line.size();
	}
	c.p.restore();
}

// Columns: up to four columns, each with a big number or short heading and a line or two of text.
void notesColumns(Ctx &c)
{
	const auto points = notePoints(c);
	const int n = std::max<int>(1, int(points.size()));
	const double gap = 0, pad = 36, colW = n <= 2 ? 460 : n == 3 ? 400 : 340;
	const double w = 2 * pad + n * colW;
	const QFont fnum = c.font(c.t.display, 64);
	double maxLines = 1;
	std::vector<Wrapped> wrapped;
	for (const auto &pt : points) {
		wrapped.push_back(fitWrapped(c, c.t.body, 32, 22, stripMarks(pt.second), colW - 2 * 24, 4, 1.22));
		maxLines = std::max<double>(maxLines, wrapped.back().lines.size());
	}
	const double lh = QFontMetricsF(c.font(c.t.body, 32)).height() * 1.22;
	const double headH = c.has(c.x.line1) ? 70 : 0.0;
	const double h = headH + 84 + maxLines * lh + pad;
	const double y = c.bottom() - h;
	const double e = c.part(0.0, 0.55);
	const QRectF card(kLeft, y, w, h);
	c.p.save();
	c.p.setClipRect(QRectF(kLeft, y - 20, (w + 20) * e, h + 40));
	solidShape(c, card, c.panel, 24);
	if (c.has(c.x.line1)) {
		fillRect(c.p, QRectF(kLeft, y, w, headH), c.accent.darker(110), 24);
		fillRect(c.p, QRectF(kLeft, y + headH - 24, w, 24), c.accent.darker(110));
		drawText(c.p, QRectF(kLeft + pad, y, w - 2 * pad, headH), c.x.line1,
			 fitFont(c, c.t.display, 40, 26, c.x.line1, w - 2 * pad), c.ink,
			 Qt::AlignLeft | Qt::AlignVCenter, c.part(0.2, 0.4));
	}
	for (int i = 0; i < points.size(); ++i) {
		const double a = c.part(0.3 + 0.1 * i, 0.4);
		const double x = kLeft + pad + i * colW + gap;
		if (i > 0)
			fillRect(c.p, QRectF(x - 12, y + headH + 20, 2, h - headH - 40), QColor(255, 255, 255, 40));
		drawText(c.p, QRectF(x + 12, y + headH + 8 + 16 * (1 - a), colW - 24, 80),
			 c.has(points[i].first) ? points[i].first : QString::number(i + 1), fnum, c.accent,
			 Qt::AlignLeft | Qt::AlignVCenter, a);
		const Wrapped &wp = wrapped[size_t(i)];
		for (int l = 0; l < wp.lines.size(); ++l)
			drawText(c.p, QRectF(x + 12, y + headH + 88 + l * wp.lineHeight, colW - 24, wp.lineHeight),
				 wp.lines[l], wp.font, c.text, Qt::AlignLeft | Qt::AlignVCenter, a);
	}
	c.p.restore();
}

struct StyleInfo {
	const char *id;
	Style draw;
	bool verticalSlide = false; // tickers rise from the bottom edge instead of sliding in from the left
	bool wholeRegion = false;   // draws across the whole region, so no group slide is applied
};

const std::vector<StyleInfo> &styles()
{
	static const std::vector<StyleInfo> all = {
		{"clean-bar", cleanBar},
		{"accent-edge", accentEdge},
		{"split-blocks", splitBlocks},
		{"underline", underline},
		{"pop-card", popCard},
		{"corner-tag", cornerTag},
		{"stacked-panels", stackedPanels},
		{"hairline", hairline},
		{"series-title", seriesTitle},
		{"song-lyric", songLyric},
		{"big-quote", bigQuote},
		{"announcement", announcement},
		{"breaking", breaking},
		{"news-bar", newsBar},
		{"handle-pill", handlePill},
		{"thank-you", thankYou},
		{"modern-pill", modernPill},
		{"modern-badge", modernBadge},
		{"modern-card", modernCard},
		{"modern-stack", modernStack},
		{"modern-logo", modernLogo},
		{"glass-bar", glassBar},
		{"desk-two",
		 [](Ctx &c) {
			 deskBar(c, DeskKind::Two);
		 }},
		{"desk-one",
		 [](Ctx &c) {
			 deskBar(c, DeskKind::One);
		 }},
		{"desk-clock",
		 [](Ctx &c) {
			 deskBar(c, DeskKind::Clock);
		 }},
		{"desk-crawl", [](Ctx &c) { deskBar(c, DeskKind::Crawl); }, true},
		{"network-name", networkName},
		{"network-breaking", networkBreaking},
		{"angular-logo", angularLogo},
		{"logo-sweep", logoSweep},
		{"logo-spin", logoSpin},
		{"logo-pulse", logoPulse},
		{"logo-float", logoFloat},
		{"logo-flip", logoFlip},
		{"logo-ripple", logoRipple},
		{"logo-hop", logoHop},
		{"logo-reveal", logoReveal},
		{"logo-orbit", logoOrbit},
		{"logo-water", logoWater},
		{"logo-glass-cube", logoGlassCube},
		{"logo-glass-disc", logoGlassDisc},
		{"logo-lockup", logoLockup},
		{"name-lime-slant", nameLimeSlant},
		{"name-edge-line", nameEdgeLine},
		{"name-blue-slant", nameBlueSlant},
		{"name-bracket", nameBracket},
		{"name-red-stack", nameRedStack},
		{"name-red-box", nameRedBox},
		{"name-role-box", nameRoleBox},
		{"name-heavy-light", nameHeavyLight},
		{"glow-headline", glowHeadline},
		{"glow-name", glowName},
		{"logo-spin3d", logoSpin3d},
		{"quick-link-card", quickLinkCard},
		{"quick-link-bar", quickLinkBar},
		{"quick-link-poster", quickLinkPoster},
		{"quick-link-glass", quickLinkGlass},
		{"quick-link-dots", quickLinkDots},
		{"notes-card", notesCard},
		{"notes-points", notesPoints},
		{"notes-key", notesKey},
		{"notes-columns", notesColumns},
		{"guest-minister", guestMinister},
		{"giving", giving},
		{"news-flash", newsFlash},
		{"news-report", newsReport},
		{"news-desk", newsDesk},
		{"live-badge", liveBadge},
		{"on-air", onAir},
		{"social-youtube",
		 [](Ctx &c) {
			 socialCard(c, Platform::YouTube);
		 }},
		{"social-instagram",
		 [](Ctx &c) {
			 socialCard(c, Platform::Instagram);
		 }},
		{"social-tiktok",
		 [](Ctx &c) {
			 socialCard(c, Platform::TikTok);
		 }},
		{"social-facebook",
		 [](Ctx &c) {
			 socialCard(c, Platform::Facebook);
		 }},
		{"social-subscribe", socialSubscribe},
		{"coming-up", comingUp},
		{"event-date", eventDate},
		{"prayer-request", prayerRequest},
		{"welcome", welcome},
		{"celebrate", celebrate},
		{"question", question},
		{"hashtag", hashtagBar},
		{"social-mixlr",
		 [](Ctx &c) {
			 socialCard(c, Platform::Mixlr);
		 }},
		{"crawl-ticker", crawlTicker, true},
		{"headline-ticker", headlineTicker, true},
	};
	return all;
}

const StyleInfo *styleFor(const std::string &baseId)
{
	for (const StyleInfo &s : styles()) {
		if (baseId == s.id)
			return &s;
	}
	return nullptr;
}

} // namespace

double regionDesignHeight(const TitleTemplate &tpl, int canvasWidth, int canvasHeight)
{
	if (tpl.fullHeight || tpl.id == "thank-you")
		return kDesignWidth * double(canvasHeight) / double(std::max(canvasWidth, 1));
	if (tpl.id == "live-badge")
		return 200.0;
	if (tpl.id.rfind("logo-", 0) == 0)
		return 300.0;
	if (tpl.id.rfind("notes-", 0) == 0)
		return 760.0;
	if (tpl.id.rfind("quick-link-", 0) == 0)
		return 780.0;
	if (tpl.id == "big-quote" || tpl.id == "series-title")
		return kTallRegionHeight;
	return kRegionHeight;
}

double tickerStripWidth(const FontSet &fonts, const TitleTemplate &tpl, const TitleText &text)
{
	if (tpl.tickerSpeed <= 0.0)
		return tpl.pulseSeconds > 0.0 ? 100.0 * tpl.pulseSeconds : 0.0; // a pulse repeats every strip
	const double px = tpl.id == "headline-ticker" ? 40 : 42;
	const QFont font = fonts.font(tpl.display, px);
	return tickerStripLength(font, tickerItems(tpl.id == "desk-crawl" ? text.note : text.line1));
}

namespace {

struct Extent {
	double minX = 0.0;
	double maxX = 0.0;
	bool ok = false;
};

RenderedTitle renderImpl(const FontSet &fonts, const RenderRequest &request, bool place);

// The left and right edges of the title when it is fully on screen, in design pixels. Cached, because it
// only changes when the words, fonts or picture size do.
Extent measureExtent(const FontSet &fonts, const RenderRequest &request)
{
	QString rowsKey;
	for (const auto &row : request.text.rows)
		rowsKey += row.first + QLatin1Char('=') + row.second + QLatin1Char(';');
	const QString key =
		QStringLiteral("%1|%2|%3|%4|%5|%6|%7|%8|%9|%10|%11")
			.arg(QString::fromStdString(request.tpl.id),
			     request.text.line1 + request.text.note + request.text.logoPath + rowsKey +
				     QString::number(request.text.logoCut) + QString::number(request.text.logoStacked) +
				     QString::number(request.text.logoMarkSecond),
			     request.text.line2, request.text.tag, QString::number(int(request.tpl.display)),
			     QString::number(int(request.tpl.body)), QString::number(request.tpl.cornerRadius),
			     QString::number(request.canvasWidth), QString::number(request.canvasHeight),
			     QString::number(request.fit), QString::number(request.fitWidth));
	thread_local QHash<QString, Extent> cache;
	const auto found = cache.constFind(key);
	if (found != cache.constEnd())
		return found.value();
	if (cache.size() > 32)
		cache.clear();
	RenderRequest probe = request;
	probe.align = 0;
	probe.scale = 1.0;
	probe.frame = {1.0, 0.0, 0.0};
	const RenderedTitle r = renderImpl(fonts, probe, false);
	Extent e;
	if (!r.image.isNull()) {
		const QImage &img = r.image;
		int minPx = img.width(), maxPx = -1;
		for (int y = 0; y < img.height(); ++y) {
			const QRgb *line = reinterpret_cast<const QRgb *>(img.constScanLine(y));
			for (int x = 0; x < img.width(); ++x) {
				if (qAlpha(line[x]) > 100) { // the title itself, not its soft shadow
					minPx = std::min(minPx, x);
					maxPx = std::max(maxPx, x);
					break;
				}
			}
			for (int x = img.width() - 1; x > maxPx && x >= 0; --x) {
				if (qAlpha(line[x]) > 100) {
					maxPx = x;
					break;
				}
			}
		}
		if (maxPx >= minPx) {
			const double scale = double(request.canvasWidth) / kDesignWidth;
			e.minX = minPx / scale;
			e.maxX = (maxPx + 1) / scale;
			e.ok = true;
		}
	}
	cache.insert(key, e);
	return e;
}

} // namespace

RenderedTitle renderTitle(const FontSet &fonts, const RenderRequest &request)
{
	return renderImpl(fonts, request, true);
}

namespace {

RenderedTitle renderImpl(const FontSet &fonts, const RenderRequest &request, bool place)
{
	RenderedTitle result;
	try {
		const StyleInfo *style = styleFor(request.tpl.id.empty() ? std::string() : request.tpl.id);
		// A custom template carries its own palette etc. but draws like its base template.
		if (!style)
			return result;
		if (request.canvasWidth <= 0 || request.canvasHeight <= 0)
			return result;

		const double scale = double(request.canvasWidth) / kDesignWidth;
		const double regionH =
			std::min(regionDesignHeight(request.tpl, request.canvasWidth, request.canvasHeight),
				 double(request.canvasHeight) / scale);
		const int pixelH = std::max(1, int(std::lround(regionH * scale)));
		QImage image(request.canvasWidth, pixelH, QImage::Format_ARGB32_Premultiplied);
		image.fill(Qt::transparent);

		QPainter painter(&image);
		painter.setRenderHint(QPainter::Antialiasing, true);
		painter.setRenderHint(QPainter::TextAntialiasing, true);
		painter.setRenderHint(QPainter::SmoothPixmapTransform, true);
		painter.scale(scale, scale);

		Ctx ctx{painter, request.tpl, request.text, fonts};
		ctx.W = kDesignWidth;
		ctx.H = regionH;
		ctx.in = std::clamp(request.frame.in, 0.0, 1.0);
		ctx.out = std::clamp(request.frame.out, 0.0, 1.0);
		ctx.tickerPx = request.frame.tickerPx;
		ctx.panel = toQ(request.tpl.palette.panel);
		ctx.accent = toQ(request.useAccentOverride ? request.accentOverride : request.tpl.palette.accent);
		ctx.text = toQ(request.tpl.palette.text);
		ctx.sub = toQ(request.tpl.palette.subtext);
		ctx.shape2 = toQ(request.tpl.palette.shape2);
		ctx.shape3 = toQ(request.tpl.palette.shape3);
		ctx.ink = toQ(request.tpl.palette.ink);
		if (supportsFit(request.tpl)) {
			ctx.fit = std::clamp(request.fit, 0, 2);
			ctx.fitW = std::clamp(request.fitWidth, kMinFitWidth, kMaxFitWidth);
		}

		painter.save();
		const double size = std::clamp(request.scale, 0.5, 1.5);
		const int align = std::clamp(request.align, 0, 2);
		if (place && supportsPlacement(request.tpl) && (align != 0 || std::abs(size - 1.0) > 0.001)) {
			const Extent e = measureExtent(fonts, request);
			if (e.ok) {
				// Grow or shrink about the title's own baseline (its top edge for a title at the top), and
				// put its left edge, middle or right edge where the operator chose.
				const double width = (e.maxX - e.minX) * size;
				const double left = align == 1   ? (ctx.W - width) / 2.0
						    : align == 2 ? ctx.W - kLeft - width
								 : kLeft;
				const double pivotY = request.tpl.anchorTop ? 64.0 : ctx.bottom();
				painter.translate(left, pivotY * (1.0 - size));
				painter.scale(size, size);
				painter.translate(-e.minX, 0);
			}
		}
		const Group group = groupFor(ctx);
		painter.save();
		applyGroup(ctx, group, style->verticalSlide, QPointF(kLeft, ctx.bottom()));
		style->draw(ctx);
		painter.restore();
		painter.restore(); // the placement transform
		painter.end();

		result.image = std::move(image);
		result.x = 0;
		result.y = request.tpl.anchorTop ? 0 : request.canvasHeight - pixelH;
	} catch (...) {
		result.image = QImage();
	}
	return result;
}

} // namespace

} // namespace laycue::titles
