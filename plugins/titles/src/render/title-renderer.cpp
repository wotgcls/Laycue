#include "render/title-renderer.hpp"

#include "core/easing.hpp"

#include <QFontMetricsF>
#include <QHash>
#include <QImage>
#include <QLinearGradient>
#include <QPainter>
#include <QPainterPath>
#include <QPolygonF>
#include <QStringList>

#include <algorithm>
#include <cmath>
#include <functional>

namespace laycue::titles {

namespace {

constexpr double kDesignWidth = 1920.0;
constexpr double kLeft = 110.0;  // left margin (inside the broadcast title-safe area)
constexpr double kBottom = 96.0; // distance from the bottom of the picture
constexpr double kRegionHeight = 360.0;
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
	const double padX = 40, capW = 12;
	const bool two = c.has(c.x.line2);
	const double h = two ? 132 : 98;
	const QFont f1 = fitFont(c, c.t.display, 62, 40, c.x.line1, 1250);
	const QFont f2 = fitFont(c, c.t.body, 30, 22, c.x.line2, 1250);
	const double w =
		std::max(560.0, capW + 2 * padX + std::max(textWidth(f1, c.x.line1), textWidth(f2, c.x.line2)));
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
	const double padX = 44, edge = 9;
	const bool two = c.has(c.x.line2);
	const double h = two ? 138 : 104;
	const QFont f1 = fitFont(c, c.t.display, 64, 40, c.x.line1, 1250);
	const QFont f2 = fitFont(c, c.t.body, 30, 22, c.x.line2, 1250);
	const double w =
		std::max(600.0, edge + 2 * padX + std::max(textWidth(f1, c.x.line1), textWidth(f2, c.x.line2)));
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
	const double padX = 36;
	const QFont f1 = fitFont(c, c.t.display, 62, 40, c.x.line1, 1200);
	const QFont f2 = fitFont(c, c.t.body, 30, 22, c.x.line2, 1200);
	const double h1 = 88, h2 = c.has(c.x.line2) ? 58 : 0;
	const double w1 = std::max(380.0, textWidth(f1, c.x.line1) + 2 * padX);
	const double w2 = std::max(300.0, textWidth(f2, c.x.line2) + 2 * padX);
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
	const double padX = 44;
	const bool two = c.has(c.x.line2);
	const QFont f1 = fitFont(c, c.t.display, 58, 38, c.x.line1, 1200);
	const QFont f2 = fitFont(c, c.t.body, 30, 22, c.x.line2, 1200);
	const QFont ft = c.font(c.t.body, 22);
	const double h = two ? 148 : 112;
	const double w = std::max(520.0, 2 * padX + std::max(textWidth(f1, c.x.line1), textWidth(f2, c.x.line2)));
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
		drawText(c.p, QRectF(kLeft + padX - 8, y - 17 + 8 * (1 - ey), tw, 34), tag, ft, QColor(17, 17, 16),
			 Qt::AlignCenter, c.part(0.2, 0.4), 2.4);
	}
}

// 6. Corner Tag: a slanted block with a tag label above its corner.
void cornerTag(Ctx &c)
{
	const double padX = 52, skew = 26;
	const bool two = c.has(c.x.line2);
	const QFont f1 = fitFont(c, c.t.display, 66, 40, c.x.line1, 1200);
	const QFont f2 = fitFont(c, c.t.body, 30, 22, c.x.line2, 1200);
	const QFont ft = c.font(c.t.display, 26);
	const double h = two ? 138 : 104;
	const double w = std::max(560.0, 2 * padX + std::max(textWidth(f1, c.x.line1), textWidth(f2, c.x.line2)));
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
		drawText(c.p, QRectF(kLeft + skew + 8, y - 44, tw, 44), tag, ft, QColor(17, 17, 16), Qt::AlignCenter,
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
	const double padX = 40;
	const QFont f1 = fitFont(c, c.t.display, 64, 40, c.x.line1, 1200);
	const QFont f2 = fitFont(c, c.t.body, 30, 22, c.x.line2, 1200);
	const double h1 = 92, h2 = c.has(c.x.line2) ? 56 : 0;
	const double w1 = std::max(420.0, textWidth(f1, c.x.line1) + 2 * padX);
	const double w2 = std::max(300.0, textWidth(f2, c.x.line2) + 2 * padX);
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

// 9. Scripture Bar: the verse, wrapped and fitted, with its reference above a thin rule.
void scriptureBar(Ctx &c)
{
	const double padX = 52, padY = 34;
	const double w = c.W - 2 * kLeft;
	const QString reference = c.x.line2.toUpper();
	const QFont fr = c.font(c.t.body, 34);
	const Wrapped verse = fitWrapped(c, c.t.display, 48, 32, c.x.line1, w - 2 * padX, 4, 1.38);
	const double refH = c.has(reference) ? 50 : 0;
	const double h = 2 * padY + refH + verse.lines.size() * verse.lineHeight + (refH > 0 ? 8 : 0);
	const double y = c.bottom() - h;
	const double e = c.part(0.0, 0.6);
	fillPanel(c, QRectF(kLeft, y, w, h));
	fillRect(c.p, QRectF(kLeft, y, 9, h * e), c.accent);
	double ty = y + padY;
	if (refH > 0) {
		const double er = c.part(0.15, 0.5);
		drawText(c.p, QRectF(kLeft + padX, ty, w, 42), reference, fr, c.sub, Qt::AlignLeft | Qt::AlignVCenter,
			 er, 4.0);
		fillRect(c.p, QRectF(kLeft + padX, ty + 46, 120 * c.part(0.2, 0.5), 2), c.accent);
		ty += refH + 8;
	}
	for (int i = 0; i < verse.lines.size(); ++i) {
		const double ev = c.part(0.24 + 0.05 * i, 0.5);
		drawText(c.p,
			 QRectF(kLeft + padX, ty + i * verse.lineHeight + 14 * (1 - ev), w - 2 * padX,
				verse.lineHeight),
			 verse.lines[i], verse.font, c.text, Qt::AlignLeft | Qt::AlignVCenter, ev);
	}
}

// 10. Verse Card: a centred card with a large quotation mark.
void verseCard(Ctx &c)
{
	const double w = std::min(1380.0, c.W - 2 * kLeft);
	const double padX = 90, padY = 46;
	const Wrapped verse = fitWrapped(c, c.t.display, 44, 30, c.x.line1, w - 2 * padX, 4, 1.42);
	const QFont fr = c.font(c.t.body, 30);
	const double refH = c.has(c.x.line2) ? 56 : 0;
	const double h = 2 * padY + verse.lines.size() * verse.lineHeight + refH;
	const double x = (c.W - w) / 2.0;
	const double y = c.bottom() - h;
	fillPanel(c, QRectF(x, y, w, h), c.t.cornerRadius);
	drawText(c.p, QRectF(x + 28, y - 8 + 18 * (1 - c.part(0.1, 0.5)), 140, 170), QStringLiteral("“"),
		 c.font(c.t.display, 170), withOpacity(c.accent, 0.55), Qt::AlignLeft | Qt::AlignTop, c.part(0.1, 0.5));
	for (int i = 0; i < verse.lines.size(); ++i) {
		const double ev = c.part(0.2 + 0.05 * i, 0.5);
		drawText(c.p,
			 QRectF(x + padX, y + padY + i * verse.lineHeight + 12 * (1 - ev), w - 2 * padX,
				verse.lineHeight),
			 verse.lines[i], verse.font, c.text, Qt::AlignHCenter | Qt::AlignVCenter, ev);
	}
	if (refH > 0)
		drawText(c.p, QRectF(x + padX, y + padY + verse.lines.size() * verse.lineHeight + 6, w - 2 * padX, 44),
			 c.x.line2.toUpper(), fr, c.accent, Qt::AlignHCenter | Qt::AlignVCenter, c.part(0.4, 0.5), 4.0);
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
		drawText(c.p, QRectF(kLeft, y, tagW, h), tag, ft, QColor(17, 17, 16), Qt::AlignCenter, 1.0, 3.0);
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
	drawText(c.p, QRectF(kLeft, y, tagW, h), tag, ft, QColor(255, 255, 255), Qt::AlignCenter, c.part(0.2, 0.4),
		 3.0);
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
	drawText(c.p, QRectF(kLeft + 56, y, tagW, h), tag, ft, QColor(255, 255, 255), Qt::AlignLeft | Qt::AlignVCenter,
		 1.0, 3.0);
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

// The scrolling strip shared by both tickers.
void drawTickerText(Ctx &c, const QRectF &area, const QFont &font, const QColor &colour)
{
	if (!c.has(c.x.line1))
		return;
	const QString text = c.x.line1.simplified();
	const double tw = textWidth(font, text);
	const double gap = 180.0;
	const double strip = tw + gap;
	c.p.save();
	c.p.setClipRect(area);
	const double offset = std::fmod(c.tickerPx, strip);
	for (double xPos = area.left() - offset; xPos < area.right(); xPos += strip)
		drawText(c.p, QRectF(xPos, area.top(), tw + 4, area.height()), text, font, colour,
			 Qt::AlignLeft | Qt::AlignVCenter);
	c.p.restore();
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
		{"scripture-bar", scriptureBar},
		{"verse-card", verseCard},
		{"series-title", seriesTitle},
		{"song-lyric", songLyric},
		{"big-quote", bigQuote},
		{"announcement", announcement},
		{"breaking", breaking},
		{"news-bar", newsBar},
		{"handle-pill", handlePill},
		{"thank-you", thankYou},
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
	if (tpl.id == "scripture-bar" || tpl.id == "big-quote" || tpl.id == "verse-card" || tpl.id == "series-title")
		return kTallRegionHeight;
	return kRegionHeight;
}

double tickerStripWidth(const FontSet &fonts, const TitleTemplate &tpl, const TitleText &text)
{
	if (tpl.tickerSpeed <= 0.0)
		return 0.0;
	const double px = tpl.id == "headline-ticker" ? 40 : 42;
	const QFont font = fonts.font(tpl.display, px);
	return textWidth(font, text.line1.simplified()) + 180.0;
}

RenderedTitle renderTitle(const FontSet &fonts, const RenderRequest &request)
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

		const Group group = groupFor(ctx);
		painter.save();
		applyGroup(ctx, group, style->verticalSlide, QPointF(kLeft, ctx.bottom()));
		style->draw(ctx);
		painter.restore();
		painter.end();

		result.image = std::move(image);
		result.x = 0;
		result.y = request.tpl.anchorTop ? 0 : request.canvasHeight - pixelH;
	} catch (...) {
		result.image = QImage();
	}
	return result;
}

} // namespace laycue::titles
