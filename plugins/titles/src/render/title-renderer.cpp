#include "render/title-renderer.hpp"

#include "core/easing.hpp"
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

#include <algorithm>
#include <cmath>
#include <functional>

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
	drawText(c.p, QRectF(kLeft + padL + dotArea, top, ww + 10, h), word, fw, QColor(255, 255, 255),
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
	drawText(c.p, QRectF(kLeft + 74, y, blockW - 74, h), word, fw, QColor(255, 255, 255),
		 Qt::AlignLeft | Qt::AlignVCenter, c.part(0.25, 0.4), 3.0);
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
	drawText(c.p, QRectF(block.left() + 70, y, blockW - 70, h), QStringLiteral("LIVE"), fw, QColor(255, 255, 255),
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
	drawText(c.p, QRectF(kLeft + 88, y, blockW - 88, h), QStringLiteral("LIVE"), fw, QColor(255, 255, 255),
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
	if (!img.isNull() && (img.width() > 600 || img.height() > 600))
		img = img.scaled(600, 600, Qt::KeepAspectRatio, Qt::SmoothTransformation);
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
	const double h = 100, padX = 30, tabH = 42, tab2H = 38;
	const QImage logo = logoImage(c.x.logoPath);
	const double logoW = logo.isNull() ? 0.0 : h - 24;
	const QString name = c.x.line1.toUpper();
	const QFont f1 = fitFont(c, c.t.display, 66, 38, name, 1150);
	const QFont ft = c.font(c.t.display, 28);
	const QFont fb = c.font(c.t.display, 26);
	const double w = std::max(520.0, padX + (logo.isNull() ? 0.0 : logoW + 26) + textWidth(f1, name) + padX);
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
		{"series-title", seriesTitle},
		{"song-lyric", songLyric},
		{"big-quote", bigQuote},
		{"announcement", announcement},
		{"breaking", breaking},
		{"news-bar", newsBar},
		{"handle-pill", handlePill},
		{"thank-you", thankYou},
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
	return textWidth(font, text.line1.simplified()) + 180.0;
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
		QStringLiteral("%1|%2|%3|%4|%5|%6|%7|%8|%9")
			.arg(QString::fromStdString(request.tpl.id),
			     request.text.line1 + request.text.note + request.text.logoPath + rowsKey,
			     request.text.line2, request.text.tag, QString::number(int(request.tpl.display)),
			     QString::number(int(request.tpl.body)), QString::number(request.tpl.cornerRadius),
			     QString::number(request.canvasWidth), QString::number(request.canvasHeight));
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
