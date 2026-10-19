#include "slides/slide-render.hpp"

#include <obs-module.h>

#include <QFont>
#include <QFontDatabase>
#include <QFontMetricsF>
#include <QLinearGradient>
#include <QPainter>
#include <QPainterPath>
#include <QRadialGradient>
#include <QStringList>

#include <algorithm>
#include <cmath>
#include <vector>

namespace laycue::slides {

namespace {

constexpr double kPi = 3.14159265358979323846; // M_PI is not defined by default on MSVC

// The family name of each bundled typeface, filled in by loadSlideFonts() from what Qt reports.
QString g_families[kFontCount] = {QStringLiteral("Outfit"), QStringLiteral("Barlow"),
				  QStringLiteral("Barlow Condensed"), QStringLiteral("Libre Baskerville"),
				  QStringLiteral("Bebas Neue")};

// The typeface a slide is being drawn in (the slide is drawn on one thread at a time, so this is safe).
thread_local int t_font = int(Font::Modern);

QColor rgb(uint32_t c, int alpha = 255)
{
	return QColor(int((c >> 16) & 0xFF), int((c >> 8) & 0xFF), int(c & 0xFF), alpha);
}

QColor lighter(const QColor &c)
{
	return QColor::fromHsv(c.hsvHue() < 0 ? 0 : c.hsvHue(), std::max(0, c.hsvSaturation() - 70),
			       std::min(255, c.value() + 70));
}

// Dark text on a light accent, light text on a dark one.
QColor onColour(const QColor &c)
{
	return c.lightness() > 150 ? QColor(0x10, 0x12, 0x18) : QColor(Qt::white);
}

// The tall display capitals and the serif only come in one weight each, so the others are not faked.
int weightFor(int wanted)
{
	if (t_font == int(Font::Display))
		return QFont::Normal;
	if (t_font == int(Font::Classic))
		return wanted >= 700 ? QFont::Bold : QFont::Normal;
	return wanted;
}

QFont fontOf(double px, int weight)
{
	QFont f(g_families[std::clamp(t_font, 0, kFontCount - 1)]);
	f.setPixelSize(std::max(8, int(px)));
	f.setWeight(QFont::Weight(weightFor(weight)));
	return f;
}

Qt::Alignment horizontal(int align)
{
	if (align == int(Align::Left))
		return Qt::AlignLeft;
	return align == int(Align::Right) ? Qt::AlignRight : Qt::AlignHCenter;
}

// The line under or over the words (reference or song name) sits to the right, unless the words are on the left.
Qt::Alignment captionAlign(int align)
{
	return align == int(Align::Left) ? Qt::AlignLeft : Qt::AlignRight;
}

// The biggest size (down to minPx) at which the text, wrapped, fits the box.
QFont fitFont(const QString &text, const QRectF &box, double maxPx, double minPx, int weight, int align)
{
	const int flags = int(horizontal(align) | Qt::AlignVCenter | Qt::TextWordWrap);
	for (double px = maxPx; px > minPx; px -= 2.0) {
		const QFont f = fontOf(px, weight);
		const QRectF need = QFontMetricsF(f).boundingRect(box, flags, text);
		if (need.height() <= box.height() && need.width() <= box.width() + 1.0)
			return f;
	}
	return fontOf(minPx, weight);
}

// Words with a soft shadow, and an outline when nothing is behind them, so they read on any video.
void drawText(QPainter &p, const QRectF &box, const QString &text, const QFont &font, const QColor &colour,
	      double scale, bool outline, int align)
{
	p.setFont(font);
	const int flags = int(horizontal(align) | Qt::AlignVCenter | Qt::TextWordWrap);
	const QColor shade = colour.lightness() > 128 ? QColor(0, 0, 0) : QColor(255, 255, 255);
	if (outline) {
		QColor o = shade;
		o.setAlpha(210);
		p.setPen(o);
		for (const QPointF offset : {QPointF(-2 * scale, 0), QPointF(2 * scale, 0), QPointF(0, -2 * scale),
					     QPointF(0, 2 * scale), QPointF(2 * scale, 3 * scale)})
			p.drawText(box.translated(offset), flags, text);
	} else {
		QColor s = shade;
		s.setAlpha(110);
		p.setPen(s);
		p.drawText(box.translated(0, 3 * scale), flags, text);
	}
	p.setPen(colour);
	p.drawText(box, flags, text);
}

void drawCaption(QPainter &p, const QRectF &box, const QString &caption, double px, const QColor &colour,
		 Qt::Alignment align, double scale, bool outline)
{
	if (caption.isEmpty())
		return;
	p.setFont(fontOf(px, 700));
	p.setPen(QColor(0, 0, 0, outline ? 200 : 120));
	p.drawText(box.translated(0, 2 * scale), int(align | Qt::AlignVCenter), caption);
	p.setPen(colour);
	p.drawText(box, int(align | Qt::AlignVCenter), caption);
}

// Paints the style's background into `area` (clipped to `shape` when given).
void paintFill(QPainter &p, const QRectF &area, const QPainterPath &shape, const SlideStyle &s, const QImage &picture)
{
	p.save();
	if (!shape.isEmpty())
		p.setClipPath(shape);
	switch (Fill(s.fill)) {
	case Fill::None:
	case Fill::Moving: // drawn as its own moving layer under the slide (renderBackdrop)
		break;
	case Fill::Solid:
		p.fillRect(area, rgb(s.color1));
		break;
	case Fill::SeeThrough:
		p.fillRect(area, rgb(s.color1, 150));
		break;
	case Fill::Picture:
		if (!picture.isNull()) {
			const QSizeF size = picture.size().scaled(area.size().toSize(), Qt::KeepAspectRatioByExpanding);
			const QRectF target(area.center().x() - size.width() / 2.0,
					    area.center().y() - size.height() / 2.0, size.width(), size.height());
			p.drawImage(target, picture);
			p.fillRect(area, QColor(0, 0, 0, 70)); // a little darker, so the words always read
			break;
		}
		[[fallthrough]];
	case Fill::Gradient:
	default: {
		QLinearGradient g(area.topLeft(), area.bottomRight());
		g.setColorAt(0, rgb(s.color1));
		g.setColorAt(1, rgb(s.color2));
		p.fillRect(area, g);
		break;
	}
	}
	p.restore();
}

// ---- Where everything goes -------------------------------------------------------------------------------

struct Geometry {
	QRectF area;    // the shape behind the words
	QRectF text;    // the words
	QRectF caption; // the reference or song name, when it has a line of its own
	QRectF decor;   // the looping picture (empty when the style has none)
	double radius = 0.0;
	double maxPx = 60.0, minPx = 20.0;
	int weight = 600;
};

// Puts the looping picture at one end of `container` (opposite the words' side) and takes its room from `text`.
void placeDecor(Geometry &g, const SlideStyle &s, const QRectF &container, double size, double margin, bool narrowText)
{
	if (s.decor == int(Decor::None))
		return;
	const double top = container.center().y() - size / 2.0;
	if (s.align == int(Align::Right)) {
		g.decor = QRectF(container.left() + margin, top, size, size);
		if (narrowText)
			g.text.setLeft(std::max(g.text.left(), g.decor.right() + margin * 0.6));
	} else {
		g.decor = QRectF(container.right() - margin - size, top, size, size);
		if (narrowText)
			g.text.setRight(std::min(g.text.right(), g.decor.left() - margin * 0.6));
	}
}

Geometry geometryOf(const SlideStyle &s, bool hasCaption, int W, int H)
{
	Geometry g;
	const double k = H / 1080.0;
	const double size = s.sizePercent / 100.0;
	switch (Layout(s.layout)) {
	case Layout::FullScreen: {
		g.area = QRectF(0, 0, W, H);
		g.text = QRectF(W * 0.08, H * 0.12, W * 0.84, H * 0.66);
		g.caption = QRectF(W * 0.08, H * 0.80, W * 0.84, H * 0.10);
		g.maxPx = 96 * k * size;
		g.minPx = 26 * k;
		g.weight = 700;
		placeDecor(g, s, QRectF(W * 0.04, H * 0.12, W * 0.92, H * 0.66), H * 0.34, W * 0.03, true);
		break;
	}
	case Layout::Card: {
		g.area = QRectF(W * 0.08, H * 0.665, W * 0.84, H * 0.28);
		g.radius = 22 * k;
		g.text = g.area.adjusted(40 * k, 30 * k, -40 * k, -22 * k);
		g.maxPx = 58 * k * size;
		g.minPx = 20 * k;
		placeDecor(g, s, g.area, g.area.height() * 0.78, 28 * k, true);
		break;
	}
	case Layout::Tab: {
		g.area = QRectF(W * 0.05, H * 0.73, W * 0.90, H * 0.21);
		g.radius = 8 * k;
		g.text = g.area.adjusted(40 * k, 22 * k, -40 * k, -22 * k);
		g.maxPx = 54 * k * size;
		g.minPx = 20 * k;
		placeDecor(g, s, g.area, g.area.height() * 0.82, 26 * k, true);
		break;
	}
	case Layout::Rule: {
		const double w = W * 0.64, h = H * 0.29;
		const double x = s.align == int(Align::Right) ? W - W * 0.06 - w
				 : s.align == int(Align::Centre)      ? (W - w) / 2.0
								      : W * 0.06;
		g.area = QRectF(x, H * 0.64, w, h);
		g.radius = 10 * k;
		const double room = (hasCaption ? 62 : 28) * k;
		g.text = QRectF(g.area.left() + 52 * k, g.area.top() + 22 * k, w - 92 * k, h - 22 * k - room);
		g.caption = QRectF(g.area.left() + 52 * k, g.area.bottom() - 56 * k, w - 92 * k, 40 * k);
		g.maxPx = 52 * k * size;
		g.minPx = 20 * k;
		if (s.decor != int(Decor::None)) {
			const double ds = H * 0.21;
			const double gap = W * 0.03;
			const double cy = g.area.center().y();
			g.decor = s.align == int(Align::Right) ? QRectF(g.area.left() - gap - ds, cy - ds / 2.0, ds, ds)
							       : QRectF(g.area.right() + gap, cy - ds / 2.0, ds, ds);
		}
		break;
	}
	case Layout::Quote: {
		g.area = QRectF(W * 0.12, H * 0.56, W * 0.76, H * 0.36);
		g.radius = 28 * k;
		const double room = (hasCaption ? 70 : 30) * k;
		g.text = QRectF(g.area.left() + 80 * k, g.area.top() + 50 * k, g.area.width() - 160 * k,
				g.area.height() - 50 * k - room);
		g.caption = QRectF(g.area.left() + 80 * k, g.area.bottom() - 70 * k, g.area.width() - 160 * k, 44 * k);
		g.maxPx = 56 * k * size;
		g.minPx = 20 * k;
		placeDecor(g, s, g.area, g.area.height() * 0.62, 34 * k, true);
		break;
	}
	case Layout::Split: {
		// [looping picture][reference][words], one strip low on the screen.
		const double h = H * 0.17, y = H * 0.77;
		double x = W * 0.06;
		if (s.decor != int(Decor::None)) {
			g.decor = QRectF(x, y, h, h);
			x += h;
		}
		g.caption = QRectF(x, y, W * 0.17, h);
		g.area = QRectF(g.caption.right(), y, W * 0.94 - g.caption.right(), h);
		g.text = g.area.adjusted(34 * k, 16 * k, -34 * k, -16 * k);
		g.maxPx = 40 * k * size;
		g.minPx = 16 * k;
		break;
	}
	case Layout::Torn: {
		g.area = QRectF(0, H * 0.78, W, H * 0.22);
		const double pad = H * 0.035;
		g.caption = QRectF(W * 0.04, g.area.top() + pad + 10 * k, W * 0.24, g.area.height() - 2 * pad - 10 * k);
		g.text = QRectF(W * 0.31 + 30 * k, g.caption.top() + 16 * k, W * 0.62 - 60 * k, g.caption.height() - 32 * k);
		g.maxPx = 34 * k * size;
		g.minPx = 14 * k;
		g.weight = 700;
		placeDecor(g, s, g.area, g.area.height() * 0.7, W * 0.012, true);
		break;
	}
	case Layout::LowerThird:
	default: {
		g.area = QRectF(0, H * 0.70, W, H * 0.25);
		const double room = hasCaption ? 76 : 40;
		g.text = QRectF(W * 0.06, g.area.top() + 22 * k, W * 0.88, g.area.height() - room * k);
		g.caption = QRectF(W * 0.06, g.area.bottom() - 52 * k, W * 0.88, 40 * k);
		g.maxPx = 62 * k * size;
		g.minPx = 20 * k;
		placeDecor(g, s, QRectF(W * 0.02, g.area.top(), W * 0.96, g.area.height()), g.area.height() * 0.84,
			   W * 0.02, true);
		break;
	}
	}
	return g;
}

// ---- The looping Bible pictures ------------------------------------------------------------------------------

double easeInOut(double t)
{
	t = std::clamp(t, 0.0, 1.0);
	return t * t * (3.0 - 2.0 * t);
}

// Soft light behind the picture that swells and fades.
void drawGlow(QPainter &p, const QPointF &centre, double radius, const QColor &accent, double strength)
{
	QRadialGradient g(centre, radius);
	QColor c = accent;
	c.setAlpha(int(std::clamp(strength, 0.0, 1.0) * 120));
	g.setColorAt(0.0, c);
	c.setAlpha(0);
	g.setColorAt(1.0, c);
	p.setPen(Qt::NoPen);
	p.setBrush(g);
	p.drawEllipse(centre, radius, radius);
}

// A few specks of light drifting upward, each on its own timing.
void drawSparkles(QPainter &p, const QRectF &box, double t, const QColor &accent)
{
	p.setPen(Qt::NoPen);
	for (int i = 0; i < 7; ++i) {
		const double period = 3.0 + 0.45 * i;
		const double u = std::fmod(t / period + i * 0.37, 1.0);
		const double x = box.left() + box.width() * (0.18 + 0.64 * std::fmod(i * 0.311 + 0.17, 1.0)) +
				 std::sin(u * 2.0 * kPi + i) * box.width() * 0.03;
		const double y = box.bottom() - u * box.height() * 0.95;
		const double r = box.width() * (0.012 + 0.008 * (i % 3));
		QColor c = lighter(accent);
		c.setAlpha(int(200 * std::sin(u * kPi)));
		p.setBrush(c);
		p.drawEllipse(QPointF(x, y), r, r);
	}
}

// An open Bible on a glow. Every few seconds the right-hand page turns over to the left, then it rests.
void drawOpenBible(QPainter &p, double S, double t, const QColor &accent)
{
	const double period = 3.4;
	const double u = std::fmod(t, period) / period;
	drawGlow(p, QPointF(S / 2, S * 0.52), S * 0.56, accent, 0.55 + 0.35 * std::sin(2.0 * kPi * t / 4.0));

	const double spine = S / 2, half = S * 0.43;
	const double top = S * 0.28, bottom = S * 0.80;
	const double lift = S * 0.035; // the outer edges sit a little higher than the spine
	const QColor cover = accent.darker(230);
	const QColor pageLight(250, 245, 230), pageShade(222, 212, 188);

	// The cover shows as a rim around the pages.
	QPainterPath rim;
	rim.moveTo(spine, top + S * 0.015);
	rim.lineTo(spine - half - S * 0.03, top - lift - S * 0.01);
	rim.lineTo(spine - half - S * 0.03, bottom - lift + S * 0.035);
	rim.lineTo(spine, bottom + S * 0.045);
	rim.lineTo(spine + half + S * 0.03, bottom - lift + S * 0.035);
	rim.lineTo(spine + half + S * 0.03, top - lift - S * 0.01);
	rim.closeSubpath();
	p.setPen(Qt::NoPen);
	p.setBrush(cover);
	p.drawPath(rim);

	auto page = [&](double side, double width, double raise, bool shade) {
		// A page from the spine out to `width` (a share of half) on `side` (-1 left, +1 right).
		const double x = spine + side * half * width;
		QPolygonF poly;
		poly << QPointF(spine, top) << QPointF(x, top - lift - raise * S * 0.05) << QPointF(x, bottom - lift)
		     << QPointF(spine, bottom);
		QLinearGradient g(QPointF(spine, 0), QPointF(x, 0));
		g.setColorAt(0.0, pageShade);
		g.setColorAt(0.35, shade ? pageShade : pageLight);
		g.setColorAt(1.0, shade ? pageShade.lighter(108) : pageLight);
		p.setBrush(g);
		p.setPen(QPen(QColor(0, 0, 0, 40), S * 0.004));
		p.drawPolygon(poly);
		// Lines of text.
		if (width > 0.25) {
			QColor ink(90, 80, 70, 95);
			p.setPen(QPen(ink, S * 0.006, Qt::SolidLine, Qt::RoundCap));
			for (int i = 0; i < 7; ++i) {
				const double f = 0.16 + 0.11 * i;
				const double y0 = top + (bottom - top) * f;
				const double lx0 = spine + side * half * width * 0.14;
				const double lx1 = spine + side * half * width * (i == 6 ? 0.55 : 0.86);
				const double dy = -lift * (std::abs(lx0 - spine) / half) * 0.9;
				const double dy1 = -lift * (std::abs(lx1 - spine) / half) * 0.9;
				p.drawLine(QPointF(lx0, y0 + dy), QPointF(lx1, y0 + dy1));
			}
		}
	};

	page(-1, 1.0, 0.0, false); // the left page
	page(+1, 1.0, 0.0, false); // the right page
	// The turning page: shrinks to the spine, then opens out on the left.
	const double turn = 0.38;
	if (u < turn) {
		const double a = easeInOut(u / turn) * kPi;
		const double c = std::cos(a);
		const double raise = std::sin(a);
		if (std::abs(c) > 0.02)
			page(c >= 0 ? +1 : -1, std::abs(c), raise, c < 0);
	}
	// The line down the middle.
	p.setPen(QPen(QColor(0, 0, 0, 70), S * 0.008));
	p.drawLine(QPointF(spine, top), QPointF(spine, bottom));
	drawSparkles(p, QRectF(S * 0.1, S * 0.05, S * 0.8, S * 0.5), t, accent);
}

// A closed Bible that floats, with a gold cross and slow rays of light turning behind it.
void drawGlowBible(QPainter &p, double S, double t, const QColor &accent)
{
	const QPointF centre(S / 2, S * 0.5);
	drawGlow(p, centre, S * 0.55, accent, 0.65 + 0.3 * std::sin(2.0 * kPi * t / 3.6));
	// Rays.
	p.save();
	p.translate(centre);
	p.rotate(t * 14.0);
	p.setPen(Qt::NoPen);
	for (int i = 0; i < 12; ++i) {
		QLinearGradient g(QPointF(0, 0), QPointF(S * 0.5, 0));
		QColor c = lighter(accent);
		c.setAlpha(i % 2 ? 70 : 36);
		g.setColorAt(0.2, c);
		c.setAlpha(0);
		g.setColorAt(1.0, c);
		p.setBrush(g);
		QPolygonF ray;
		ray << QPointF(0, 0) << QPointF(S * 0.5, -S * 0.07) << QPointF(S * 0.5, S * 0.07);
		p.drawPolygon(ray);
		p.rotate(30.0);
	}
	p.restore();

	const double bob = std::sin(2.0 * kPi * t / 3.5) * S * 0.03;
	const double w = S * 0.42, h = S * 0.58;
	const QRectF book(centre.x() - w / 2.0, centre.y() - h / 2.0 + bob, w, h);
	// A soft shadow under it.
	p.setBrush(QColor(0, 0, 0, int(70 - bob / S * 600)));
	p.drawEllipse(QPointF(centre.x(), S * 0.93), w * (0.55 - bob / S), S * 0.025);
	// The pages (a cream edge), then the cover.
	p.setPen(Qt::NoPen);
	p.setBrush(QColor(246, 240, 224));
	p.drawRoundedRect(book.adjusted(w * 0.05, h * 0.02, w * 0.03, h * 0.015), S * 0.012, S * 0.012);
	QLinearGradient cover(book.topLeft(), book.bottomRight());
	cover.setColorAt(0.0, accent.darker(170));
	cover.setColorAt(1.0, accent.darker(300));
	p.setBrush(cover);
	p.drawRoundedRect(book, S * 0.022, S * 0.022);
	// The spine.
	p.setBrush(QColor(0, 0, 0, 70));
	p.drawRoundedRect(QRectF(book.left(), book.top(), w * 0.09, h), S * 0.022, S * 0.022);
	// The cross.
	const QColor gold = lighter(accent);
	p.setBrush(gold);
	const QPointF cc(book.center().x() + w * 0.04, book.top() + h * 0.4);
	p.drawRoundedRect(QRectF(cc.x() - w * 0.04, cc.y() - h * 0.2, w * 0.08, h * 0.4), S * 0.006, S * 0.006);
	p.drawRoundedRect(QRectF(cc.x() - w * 0.19, cc.y() - h * 0.1, w * 0.38, h * 0.075), S * 0.006, S * 0.006);
	// A band of light sliding over the cover.
	p.save();
	QPainterPath clip;
	clip.addRoundedRect(book, S * 0.022, S * 0.022);
	p.setClipPath(clip);
	const double slide = std::fmod(t / 4.0, 1.0);
	const double bx = book.left() - w * 0.6 + slide * w * 2.2;
	QLinearGradient sheen(QPointF(bx, book.top()), QPointF(bx + w * 0.5, book.bottom()));
	sheen.setColorAt(0.0, QColor(255, 255, 255, 0));
	sheen.setColorAt(0.5, QColor(255, 255, 255, 60));
	sheen.setColorAt(1.0, QColor(255, 255, 255, 0));
	p.fillRect(book, sheen);
	p.restore();
	drawSparkles(p, QRectF(S * 0.1, S * 0.08, S * 0.8, S * 0.8), t, accent);
}

// Rays of light turning slowly about `centre`.
void drawRays(QPainter &p, const QPointF &centre, double length, double t, const QColor &accent, int count)
{
	p.save();
	p.translate(centre);
	p.rotate(t * 12.0);
	p.setPen(Qt::NoPen);
	for (int i = 0; i < count; ++i) {
		QLinearGradient g(QPointF(0, 0), QPointF(length, 0));
		QColor c = lighter(accent);
		c.setAlpha(i % 2 ? 70 : 36);
		g.setColorAt(0.15, c);
		c.setAlpha(0);
		g.setColorAt(1.0, c);
		p.setBrush(g);
		QPolygonF ray;
		ray << QPointF(0, 0) << QPointF(length, -length * 0.13) << QPointF(length, length * 0.13);
		p.drawPolygon(ray);
		p.rotate(360.0 / count);
	}
	p.restore();
}

// A scroll that slowly unrolls and rolls up again.
void drawScroll(QPainter &p, double S, double t, const QColor &accent)
{
	drawGlow(p, QPointF(S / 2, S / 2), S * 0.52, accent, 0.5 + 0.3 * std::sin(2.0 * kPi * t / 4.0));
	const double open = 0.25 + 0.75 * (0.5 - 0.5 * std::cos(2.0 * kPi * t / 7.0));
	const double w = S * 0.6, full = S * 0.62, h = full * open;
	const double cx = S / 2, top = S / 2 - h / 2;
	QLinearGradient paper(QPointF(cx - w / 2, 0), QPointF(cx + w / 2, 0));
	paper.setColorAt(0.0, QColor(226, 210, 172));
	paper.setColorAt(0.5, QColor(248, 238, 214));
	paper.setColorAt(1.0, QColor(226, 210, 172));
	p.setPen(Qt::NoPen);
	p.setBrush(paper);
	p.drawRect(QRectF(cx - w / 2, top, w, h));
	p.setPen(QPen(QColor(110, 90, 60, 110), S * 0.007, Qt::SolidLine, Qt::RoundCap));
	for (int i = 0; i < 9; ++i) {
		const double y = top + S * 0.07 + i * S * 0.055;
		if (y > top + h - S * 0.05)
			break;
		p.drawLine(QPointF(cx - w * 0.36, y), QPointF(cx + w * (i % 3 == 2 ? 0.1 : 0.36), y));
	}
	for (const double y : {top, top + h}) {
		const QRectF rod(cx - w * 0.58, y - S * 0.04, w * 1.16, S * 0.08);
		QLinearGradient g(rod.topLeft(), rod.bottomLeft());
		g.setColorAt(0.0, accent.lighter(130));
		g.setColorAt(0.5, accent.darker(150));
		g.setColorAt(1.0, accent.darker(260));
		p.setPen(Qt::NoPen);
		p.setBrush(g);
		p.drawRoundedRect(rod, S * 0.04, S * 0.04);
		p.setBrush(lighter(accent));
		p.drawEllipse(QPointF(rod.left(), rod.center().y()), S * 0.035, S * 0.035);
		p.drawEllipse(QPointF(rod.right(), rod.center().y()), S * 0.035, S * 0.035);
	}
	drawSparkles(p, QRectF(S * 0.1, S * 0.05, S * 0.8, S * 0.6), t, accent);
}

// A white dove gliding on the spot, wings beating slowly.
void drawDove(QPainter &p, double S, double t, const QColor &accent)
{
	drawGlow(p, QPointF(S / 2, S / 2), S * 0.52, accent, 0.55 + 0.3 * std::sin(2.0 * kPi * t / 3.0));
	const double bob = std::sin(2.0 * kPi * t / 2.8) * S * 0.04;
	const double beat = 0.5 + 0.5 * std::sin(2.0 * kPi * t / 1.2); // 0 wings down .. 1 wings up
	const QColor white(250, 250, 255), shade(214, 220, 236);
	p.save();
	p.translate(S / 2, S / 2 + bob);
	p.setPen(Qt::NoPen);
	auto wing = [&](double lift, const QColor &colour, double dx) {
		QPainterPath path;
		path.moveTo(dx, 0);
		path.quadTo(dx - S * 0.12, -S * (0.08 + 0.3 * lift), dx - S * 0.36, -S * (0.02 + 0.26 * lift));
		path.quadTo(dx - S * 0.2, -S * 0.02, dx + S * 0.05, S * 0.05);
		path.closeSubpath();
		p.setBrush(colour);
		p.drawPath(path);
	};
	wing(beat * 0.85, shade, S * 0.05);
	// Body, tail and head.
	p.setBrush(white);
	p.drawEllipse(QPointF(0, S * 0.05), S * 0.21, S * 0.085);
	QPolygonF tail;
	tail << QPointF(-S * 0.17, S * 0.04) << QPointF(-S * 0.37, -S * 0.02) << QPointF(-S * 0.35, S * 0.13);
	p.drawPolygon(tail);
	p.drawEllipse(QPointF(S * 0.19, -S * 0.02), S * 0.07, S * 0.065);
	p.setBrush(lighter(accent));
	QPolygonF beak;
	beak << QPointF(S * 0.25, -S * 0.03) << QPointF(S * 0.32, -S * 0.005) << QPointF(S * 0.25, S * 0.01);
	p.drawPolygon(beak);
	p.setBrush(QColor(30, 30, 40));
	p.drawEllipse(QPointF(S * 0.205, -S * 0.035), S * 0.011, S * 0.011);
	wing(beat, white, 0);
	p.restore();
	drawSparkles(p, QRectF(S * 0.1, S * 0.1, S * 0.8, S * 0.8), t, accent);
}

// A gold cross in turning rays of light.
void drawCross(QPainter &p, double S, double t, const QColor &accent)
{
	const QPointF centre(S / 2, S / 2);
	drawGlow(p, centre, S * 0.55, accent, 0.6 + 0.35 * std::sin(2.0 * kPi * t / 3.0));
	drawRays(p, centre, S * 0.5, t, accent, 14);
	const double bw = S * 0.11;
	QPainterPath cross;
	cross.addRoundedRect(QRectF(S / 2 - bw / 2, S * 0.18, bw, S * 0.64), S * 0.015, S * 0.015);
	cross.addRoundedRect(QRectF(S / 2 - S * 0.21, S * 0.33, S * 0.42, bw), S * 0.015, S * 0.015);
	cross.setFillRule(Qt::WindingFill);
	QLinearGradient g(QPointF(S * 0.3, S * 0.18), QPointF(S * 0.7, S * 0.82));
	g.setColorAt(0.0, lighter(accent).lighter(115));
	g.setColorAt(0.5, accent);
	g.setColorAt(1.0, accent.darker(160));
	p.setPen(QPen(QColor(255, 255, 255, 120), S * 0.006));
	p.setBrush(g);
	p.drawPath(cross);
	// A band of light sliding down it.
	p.save();
	p.setClipPath(cross);
	const double y = S * (0.1 + std::fmod(t / 3.5, 1.0) * 0.9);
	QLinearGradient sheen(QPointF(0, y - S * 0.08), QPointF(0, y + S * 0.08));
	sheen.setColorAt(0.0, QColor(255, 255, 255, 0));
	sheen.setColorAt(0.5, QColor(255, 255, 255, 110));
	sheen.setColorAt(1.0, QColor(255, 255, 255, 0));
	p.fillRect(QRectF(0, 0, S, S), sheen);
	p.restore();
	drawSparkles(p, QRectF(S * 0.1, S * 0.1, S * 0.8, S * 0.8), t, accent);
}

// A warm sky with the sun rising out of shimmering water: fills its whole square.
void drawSunrise(QPainter &p, double S, double t, const QColor &accent)
{
	QLinearGradient sky(QPointF(0, 0), QPointF(0, S));
	sky.setColorAt(0.0, lighter(accent));
	sky.setColorAt(0.55, QColor(255, 176, 72));
	sky.setColorAt(1.0, QColor(232, 104, 38));
	p.fillRect(QRectF(0, 0, S, S), sky);
	const QPointF sun(S / 2, S * 0.64 + std::sin(2.0 * kPi * t / 8.0) * S * 0.04);
	drawRays(p, sun, S * 0.7, t * 0.6, QColor(255, 240, 200), 16);
	QRadialGradient g(sun, S * 0.27);
	g.setColorAt(0.0, QColor(255, 252, 230));
	g.setColorAt(0.6, QColor(255, 214, 96));
	g.setColorAt(1.0, QColor(255, 150, 50));
	p.setPen(Qt::NoPen);
	p.setBrush(g);
	p.drawEllipse(sun, S * 0.26, S * 0.26);
	// Water over the lower part, with lines of light drifting across it.
	const double water = S * 0.74;
	QLinearGradient sea(QPointF(0, water), QPointF(0, S));
	sea.setColorAt(0.0, QColor(214, 96, 40, 235));
	sea.setColorAt(1.0, QColor(150, 54, 30, 245));
	p.fillRect(QRectF(0, water, S, S - water), sea);
	p.setPen(QPen(QColor(255, 236, 190, 160), S * 0.012, Qt::SolidLine, Qt::RoundCap));
	for (int i = 0; i < 4; ++i) {
		const double y = water + S * (0.04 + 0.05 * i);
		const double w = S * (0.32 - 0.06 * i);
		const double x = S / 2 - w / 2 + std::sin(t * (1.1 + 0.3 * i) + i) * S * 0.04;
		p.drawLine(QPointF(x, y), QPointF(x + w, y));
	}
}

// One music note (single, or two joined by a beam) of height `size`, its head at the origin.
void drawNote(QPainter &p, double size, bool pair, const QColor &colour)
{
	p.setPen(Qt::NoPen);
	p.setBrush(colour);
	const double stem = size * 0.09;
	auto head = [&](double x) {
		p.save();
		p.translate(x, 0);
		p.rotate(-22);
		p.drawEllipse(QPointF(0, 0), size * 0.24, size * 0.17);
		p.restore();
		p.drawRect(QRectF(x + size * 0.17, -size * 0.95, stem, size * 0.95));
	};
	head(0);
	if (pair) {
		head(size * 0.62);
		p.drawRect(QRectF(size * 0.17, -size * 0.95, size * 0.62 + stem, size * 0.17));
	} else {
		QPainterPath flag;
		flag.moveTo(size * 0.17 + stem, -size * 0.95);
		flag.cubicTo(size * 0.45, -size * 0.75, size * 0.55, -size * 0.55, size * 0.4, -size * 0.35);
		flag.cubicTo(size * 0.45, -size * 0.55, size * 0.35, -size * 0.65, size * 0.17 + stem, -size * 0.7);
		flag.closeSubpath();
		p.drawPath(flag);
	}
}

// Music notes rising and swaying.
void drawNotes(QPainter &p, double S, double t, const QColor &accent)
{
	drawGlow(p, QPointF(S / 2, S * 0.6), S * 0.5, accent, 0.45 + 0.25 * std::sin(2.0 * kPi * t / 3.0));
	for (int i = 0; i < 6; ++i) {
		const double period = 4.0 + 0.6 * i;
		const double u = std::fmod(t / period + i * 0.23, 1.0);
		const double x = S * (0.18 + 0.6 * std::fmod(i * 0.37 + 0.11, 1.0)) + std::sin(u * 2.0 * kPi + i) * S * 0.06;
		const double y = S * (0.92 - 0.78 * u);
		const double size = S * (0.2 + 0.05 * (i % 2));
		QColor c = i % 2 ? accent : lighter(accent);
		c.setAlpha(int(255 * std::sin(u * kPi)));
		p.save();
		p.translate(x, y);
		p.rotate(std::sin(u * 2.0 * kPi + i) * 14.0);
		drawNote(p, size, i % 3 == 0, c);
		p.restore();
	}
}

// Equaliser bars that rise and fall with an imagined beat.
void drawEqualizer(QPainter &p, double S, double t, const QColor &accent)
{
	drawGlow(p, QPointF(S / 2, S * 0.62), S * 0.48, accent, 0.4 + 0.2 * std::sin(2.0 * kPi * t / 2.0));
	const int count = 7;
	const double barW = S * 0.075, gap = S * 0.035;
	const double left = (S - (count * barW + (count - 1) * gap)) / 2.0, base = S * 0.78;
	p.setPen(Qt::NoPen);
	for (int i = 0; i < count; ++i) {
		const double level = 0.6 * std::abs(std::sin(t * (2.1 + 0.37 * i) + i * 1.3)) +
				     0.4 * std::abs(std::sin(t * (3.3 + 0.21 * i) + i));
		const double h = S * (0.1 + 0.52 * level);
		const QRectF bar(left + i * (barW + gap), base - h, barW, h);
		QLinearGradient g(bar.bottomLeft(), bar.topLeft());
		g.setColorAt(0.0, accent);
		g.setColorAt(1.0, lighter(accent));
		p.setBrush(g);
		p.drawRoundedRect(bar, barW / 2, barW / 2);
		QColor shadow = accent; // a faint reflection underneath
		shadow.setAlpha(50);
		p.setBrush(shadow);
		p.drawRoundedRect(QRectF(bar.left(), base + S * 0.02, barW, h * 0.3), barW / 2, barW / 2);
	}
}

// A record turning, with a tone arm resting on it.
void drawVinyl(QPainter &p, double S, double t, const QColor &accent)
{
	drawGlow(p, QPointF(S * 0.46, S * 0.52), S * 0.5, accent, 0.35);
	const QPointF centre(S * 0.46, S * 0.52);
	const double R = S * 0.38;
	p.save();
	p.translate(centre);
	p.rotate(t * 140.0);
	QRadialGradient disc(QPointF(0, 0), R);
	disc.setColorAt(0.0, QColor(36, 36, 42));
	disc.setColorAt(1.0, QColor(8, 8, 12));
	p.setPen(Qt::NoPen);
	p.setBrush(disc);
	p.drawEllipse(QPointF(0, 0), R, R);
	p.setBrush(Qt::NoBrush);
	p.setPen(QPen(QColor(255, 255, 255, 26), S * 0.004));
	for (int i = 1; i <= 6; ++i)
		p.drawEllipse(QPointF(0, 0), R * (0.42 + 0.09 * i), R * (0.42 + 0.09 * i));
	p.setPen(Qt::NoPen);
	p.setBrush(accent);
	p.drawEllipse(QPointF(0, 0), R * 0.34, R * 0.34);
	p.setBrush(lighter(accent));
	p.drawRect(QRectF(-R * 0.2, -R * 0.05, R * 0.4, R * 0.1)); // a mark on the label, so the turn shows
	p.setBrush(QColor(10, 10, 14));
	p.drawEllipse(QPointF(0, 0), R * 0.05, R * 0.05);
	p.restore();
	// Light on the vinyl that does not turn with it.
	QLinearGradient sheen(QPointF(centre.x() - R, centre.y() - R), QPointF(centre.x() + R, centre.y() + R));
	sheen.setColorAt(0.3, QColor(255, 255, 255, 0));
	sheen.setColorAt(0.5, QColor(255, 255, 255, 34));
	sheen.setColorAt(0.7, QColor(255, 255, 255, 0));
	QPainterPath round;
	round.addEllipse(centre, R, R);
	p.fillPath(round, sheen);
	// The tone arm.
	p.setPen(QPen(QColor(205, 208, 214), S * 0.022, Qt::SolidLine, Qt::RoundCap));
	p.drawLine(QPointF(S * 0.88, S * 0.12), QPointF(S * 0.7, S * 0.5));
	p.setPen(Qt::NoPen);
	p.setBrush(QColor(230, 232, 238));
	p.drawEllipse(QPointF(S * 0.88, S * 0.12), S * 0.045, S * 0.045);
	p.drawRoundedRect(QRectF(S * 0.65, S * 0.48, S * 0.08, S * 0.06), S * 0.01, S * 0.01);
}

// The band of the Torn layout: a straight bottom and a ragged, torn-paper top edge.
QPainterPath tornPath(const QRectF &area, double k)
{
	QPainterPath path;
	path.moveTo(area.left(), area.bottom());
	const double step = 16 * k;
	int i = 0;
	for (double x = area.left(); x <= area.right() + step; x += step, ++i) {
		const double jag = std::sin(i * 12.9898) * 43758.5453;
		const double r = jag - std::floor(jag); // a steady pseudo-random 0..1 per point
		path.lineTo(std::min(x, area.right()), area.top() + (r - 0.5) * 14 * k + (i % 2 ? 4 * k : 0));
	}
	path.lineTo(area.right(), area.bottom());
	path.closeSubpath();
	return path;
}

// Where the moving colours go: low on the screen they rise from the bottom edge and fade out upward.
QRectF backdropArea(const SlideStyle &s, const Geometry &g, int W, int H)
{
	if (s.fill != int(Fill::Moving))
		return QRectF();
	if (s.layout == int(Layout::LowerThird))
		return QRectF(0, H * 0.48, W, H * 0.52);
	return g.area;
}

} // namespace

void loadSlideFonts()
{
	static bool loaded = false;
	if (loaded)
		return;
	loaded = true;
	// The files of each typeface, in the order of the Font enum.
	const std::vector<std::vector<const char *>> files = {
		{"fonts/Outfit-SemiBold.ttf", "fonts/Outfit-Bold.ttf"},
		{"fonts/Barlow-Regular.ttf", "fonts/Barlow-SemiBold.ttf"},
		{"fonts/BarlowCondensed-SemiBold.ttf", "fonts/BarlowCondensed-Bold.ttf"},
		{"fonts/LibreBaskerville.ttf", "fonts/LibreBaskerville-Italic.ttf"},
		{"fonts/BebasNeue-Regular.ttf"},
	};
	for (size_t f = 0; f < files.size() && f < size_t(kFontCount); ++f) {
		for (const char *file : files[f]) {
			char *path = obs_module_file(file);
			if (!path)
				continue;
			const int id = QFontDatabase::addApplicationFont(QString::fromUtf8(path));
			bfree(path);
			const QStringList families = QFontDatabase::applicationFontFamilies(id);
			if (!families.isEmpty())
				g_families[f] = families.front();
		}
	}
}

QString applyCase(const QString &text, TextCase textCase)
{
	if (textCase == TextCase::Upper)
		return text.toUpper();
	if (textCase != TextCase::FirstLetter)
		return text;
	QStringList lines = text.split(QLatin1Char('\n'));
	for (QString &line : lines) {
		for (int i = 0; i < line.size(); ++i) {
			if (line[i].isLetter()) {
				line[i] = line[i].toUpper();
				break;
			}
		}
	}
	return lines.join(QLatin1Char('\n'));
}

QRect decorRect(const SlideStyle &style, int canvasWidth, int canvasHeight)
{
	SlideStyle s = style;
	sanitize(s);
	const Geometry g = geometryOf(s, true, std::max(16, canvasWidth), std::max(16, canvasHeight));
	return g.decor.isEmpty() ? QRect() : g.decor.toAlignedRect();
}

QImage renderDecor(const SlideStyle &style, double seconds, const QSize &size, double opacity)
{
	QImage image(std::max(8, size.width()), std::max(8, size.height()), QImage::Format_ARGB32_Premultiplied);
	image.fill(Qt::transparent);
	if (style.decor == int(Decor::None) || opacity <= 0.0)
		return image;
	QPainter p(&image);
	p.setRenderHint(QPainter::Antialiasing, true);
	p.setRenderHint(QPainter::SmoothPixmapTransform, true);
	p.setOpacity(std::clamp(opacity, 0.0, 1.0));
	const double S = std::min(image.width(), image.height());
	const QColor accent = rgb(style.accent);
	switch (Decor(style.decor)) {
	case Decor::OpenBible:
		drawOpenBible(p, S, seconds, accent);
		break;
	case Decor::GlowBible:
		drawGlowBible(p, S, seconds, accent);
		break;
	case Decor::Scroll:
		drawScroll(p, S, seconds, accent);
		break;
	case Decor::Dove:
		drawDove(p, S, seconds, accent);
		break;
	case Decor::Cross:
		drawCross(p, S, seconds, accent);
		break;
	case Decor::Sunrise:
		drawSunrise(p, S, seconds, accent);
		break;
	case Decor::Notes:
		drawNotes(p, S, seconds, accent);
		break;
	case Decor::Equalizer:
		drawEqualizer(p, S, seconds, accent);
		break;
	case Decor::Vinyl:
		drawVinyl(p, S, seconds, accent);
		break;
	case Decor::None:
	default:
		break;
	}
	return image;
}

QRect backdropRect(const SlideStyle &style, int canvasWidth, int canvasHeight)
{
	SlideStyle s = style;
	sanitize(s);
	const int W = std::max(16, canvasWidth), H = std::max(16, canvasHeight);
	const QRectF area = backdropArea(s, geometryOf(s, true, W, H), W, H);
	return area.isEmpty() ? QRect() : area.toAlignedRect();
}

QImage renderBackdrop(const SlideStyle &style, double seconds, int canvasWidth, int canvasHeight, double scale,
		      double opacity)
{
	SlideStyle s = style;
	sanitize(s);
	const int W = std::max(16, canvasWidth), H = std::max(16, canvasHeight);
	const Geometry g = geometryOf(s, true, W, H);
	const QRectF area = backdropArea(s, g, W, H);
	if (area.isEmpty() || opacity <= 0.0)
		return QImage();
	const int w = std::max(8, int(std::ceil(area.width() * scale)));
	const int h = std::max(8, int(std::ceil(area.height() * scale)));
	QImage image(w, h, QImage::Format_ARGB32_Premultiplied);
	image.fill(rgb(s.color1));
	QPainter p(&image);
	p.setRenderHint(QPainter::Antialiasing, true);
	p.setPen(Qt::NoPen);
	// Three soft patches of colour, each drifting on its own slow path.
	const QColor colours[3] = {rgb(s.color2), rgb(s.color3), lighter(rgb(s.color2))};
	const double big = std::max(w, h);
	for (int i = 0; i < 3; ++i) {
		const QPointF centre(w * (0.5 + 0.42 * std::sin(seconds * 0.13 * (i + 1) + i * 2.1)),
				     h * (0.62 + 0.32 * std::sin(seconds * 0.09 * (i + 2) + i)));
		const double radius = big * (0.5 + 0.08 * std::sin(seconds * 0.2 + i));
		QRadialGradient blob(centre, radius);
		QColor c = colours[i];
		c.setAlpha(i == 2 ? 120 : 230);
		blob.setColorAt(0.0, c);
		c.setAlpha(0);
		blob.setColorAt(1.0, c);
		p.setBrush(blob);
		p.drawRect(image.rect());
	}
	// The shape: low on the screen it rises from the bottom and fades out upward; otherwise it takes the slide's shape.
	p.setCompositionMode(QPainter::CompositionMode_DestinationIn);
	if (s.layout == int(Layout::LowerThird)) {
		QLinearGradient fade(QPointF(0, 0), QPointF(0, h));
		fade.setColorAt(0.0, QColor(0, 0, 0, 0));
		fade.setColorAt(0.5, QColor(0, 0, 0, 225));
		fade.setColorAt(1.0, QColor(0, 0, 0, 255));
		p.fillRect(image.rect(), fade);
	} else if (s.layout != int(Layout::FullScreen)) {
		QImage mask(w, h, QImage::Format_ARGB32_Premultiplied);
		mask.fill(Qt::transparent);
		{
			QPainter m(&mask);
			m.setRenderHint(QPainter::Antialiasing, true);
			m.scale(scale, scale);
			m.translate(-area.topLeft());
			QPainterPath shape;
			if (s.layout == int(Layout::Torn))
				shape = tornPath(area, H / 1080.0);
			else
				shape.addRoundedRect(area, g.radius, g.radius);
			m.fillPath(shape, QColor(0, 0, 0));
		}
		p.drawImage(0, 0, mask);
	}
	if (opacity < 1.0)
		p.fillRect(image.rect(), QColor(0, 0, 0, int(255 * std::clamp(opacity, 0.0, 1.0))));
	return image;
}

QImage renderSlide(const SlideRequest &r)
{
	const int W = std::max(16, r.canvasWidth), H = std::max(16, r.canvasHeight);
	QImage image(W, H, QImage::Format_ARGB32_Premultiplied);
	image.fill(Qt::transparent);
	if (r.text.trimmed().isEmpty() && r.caption.trimmed().isEmpty())
		return image;
	SlideStyle s = r.style;
	sanitize(s);
	if (s.fill == int(Fill::Picture) && r.picture.isNull())
		s.fill = int(Fill::Gradient);
	t_font = s.font;
	const double k = H / 1080.0; // canvas scale
	const QString text = applyCase(r.text, TextCase(s.textCase));
	const QColor textColour = rgb(s.text), accent = rgb(s.accent);
	const bool bare = s.fill == int(Fill::None);
	const bool hasCaption = !r.caption.isEmpty();
	const Geometry g = geometryOf(s, hasCaption, W, H);

	QPainter p(&image);
	p.setRenderHint(QPainter::Antialiasing, true);
	p.setRenderHint(QPainter::TextAntialiasing, true);
	p.setRenderHint(QPainter::SmoothPixmapTransform, true);
	const QFont words = fitFont(text, g.text, g.maxPx, g.minPx, g.weight, s.align);

	switch (Layout(s.layout)) {
	case Layout::FullScreen: {
		paintFill(p, g.area, QPainterPath(), s, r.picture);
		drawText(p, g.text, text, words, textColour, k, bare, s.align);
		drawCaption(p, g.caption, r.caption, 50 * k, accent, captionAlign(s.align), k, bare);
		break;
	}
	case Layout::Card: {
		QPainterPath path;
		path.addRoundedRect(g.area, g.radius, g.radius);
		paintFill(p, g.area, path, s, r.picture);
		if (!bare) {
			QLinearGradient edge(g.area.topLeft(), g.area.bottomRight());
			edge.setColorAt(0, lighter(accent));
			edge.setColorAt(0.5, accent);
			edge.setColorAt(1, lighter(accent));
			p.setPen(QPen(QBrush(edge), 4 * k));
			p.drawPath(path);
		}
		if (hasCaption) {
			p.setFont(fontOf(26 * k, 700));
			const double pillW = QFontMetricsF(p.font()).horizontalAdvance(r.caption) + 36 * k;
			const double left = s.align == int(Align::Right) ? g.area.right() - 28 * k - pillW
									  : g.area.left() + 28 * k;
			const QRectF pill(left, g.area.top() - 20 * k, pillW, 40 * k);
			QPainterPath pp;
			pp.addRoundedRect(pill, 20 * k, 20 * k);
			p.fillPath(pp, accent);
			p.setPen(onColour(accent));
			p.drawText(pill, Qt::AlignCenter, r.caption);
		}
		drawText(p, g.text, text, words, textColour, k, bare, s.align);
		break;
	}
	case Layout::Tab: {
		QPainterPath path;
		path.addRoundedRect(g.area, g.radius, g.radius);
		paintFill(p, g.area, path, s, r.picture);
		if (hasCaption) {
			QFont tabFont = fontOf(34 * k, 800);
			tabFont.setItalic(true);
			const double tabW = QFontMetricsF(tabFont).horizontalAdvance(r.caption) + 56 * k;
			const double left = s.align == int(Align::Right) ? g.area.right() - 28 * k - tabW
									  : g.area.left() + 28 * k;
			const QRectF tab(left, g.area.top() - 52 * k, tabW, 52 * k);
			QLinearGradient tg(tab.topLeft(), tab.bottomRight());
			tg.setColorAt(0, lighter(accent));
			tg.setColorAt(0.35, accent);
			tg.setColorAt(1, accent.darker(130));
			p.fillRect(tab, tg);
			p.setFont(tabFont);
			p.setPen(onColour(accent));
			p.drawText(tab, Qt::AlignCenter, r.caption);
		}
		drawText(p, g.text, text, words, textColour, k, bare, s.align);
		break;
	}
	case Layout::Split: {
		if (!g.decor.isEmpty())
			p.fillRect(g.decor, rgb(s.color2)); // behind the looping picture
		p.fillRect(g.caption, accent);
		paintFill(p, g.area, QPainterPath(), s, r.picture);
		if (hasCaption) {
			const QRectF box = g.caption.adjusted(22 * k, 12 * k, -22 * k, -12 * k);
			QFont ref = fitFont(r.caption, box, 46 * k, 16 * k, 700, int(Align::Left));
			ref.setItalic(true);
			p.setFont(ref);
			p.setPen(onColour(accent));
			p.drawText(box, int(Qt::AlignLeft | Qt::AlignVCenter | Qt::TextWordWrap), r.caption);
		}
		drawText(p, g.text, text, words, textColour, k, bare, s.align);
		break;
	}
	case Layout::Torn: {
		const QPainterPath band = tornPath(g.area, k);
		paintFill(p, g.area.adjusted(0, -14 * k, 0, 0), band, s, r.picture);
		// Fine contour lines in the corner, like a map.
		p.save();
		p.setClipPath(band);
		QColor line = lighter(accent);
		line.setAlpha(80);
		p.setPen(QPen(line, 2 * k));
		p.setBrush(Qt::NoBrush);
		for (int i = 0; i < 7; ++i)
			p.drawEllipse(QPointF(W * 0.97, g.area.top() + g.area.height() * 0.3), (60 + i * 38) * k,
				      (40 + i * 26) * k);
		p.restore();
		const QColor boxColour(10, 10, 16, 225);
		p.fillRect(g.caption, boxColour);
		p.fillRect(g.text.adjusted(-30 * k, -16 * k, 30 * k, 16 * k), boxColour);
		if (hasCaption) {
			const QRectF box = g.caption.adjusted(20 * k, 8 * k, -20 * k, -8 * k);
			const QString ref = r.caption.toUpper();
			p.setFont(fitFont(ref, box, 64 * k, 18 * k, 800, int(Align::Left)));
			p.setPen(lighter(accent));
			p.drawText(box, int(Qt::AlignLeft | Qt::AlignVCenter | Qt::TextWordWrap), ref);
		}
		drawText(p, g.text, text, words, textColour, k, bare, s.align);
		break;
	}
	case Layout::Rule: {
		QPainterPath path;
		path.addRoundedRect(g.area, g.radius, g.radius);
		paintFill(p, g.area, path, s, r.picture);
		if (s.align == int(Align::Right))
			p.fillRect(QRectF(g.area.right() - 10 * k, g.area.top(), 10 * k, g.area.height()), accent);
		else if (s.align == int(Align::Centre))
			p.fillRect(QRectF(g.area.left(), g.area.top(), g.area.width(), 8 * k), accent);
		else
			p.fillRect(QRectF(g.area.left(), g.area.top(), 10 * k, g.area.height()), accent);
		drawText(p, g.text, text, words, textColour, k, bare, s.align);
		drawCaption(p, g.caption, r.caption, 30 * k, accent, horizontal(s.align), k, bare);
		break;
	}
	case Layout::Quote: {
		QPainterPath path;
		path.addRoundedRect(g.area, g.radius, g.radius);
		paintFill(p, g.area, path, s, r.picture);
		// The large opening quotation mark, half above the words.
		QFont mark = fontOf(190 * k, 700);
		p.setFont(mark);
		QColor markColour = accent;
		markColour.setAlpha(bare ? 255 : 230);
		p.setPen(markColour);
		const QRectF markBox(g.area.left() + 26 * k, g.area.top() - 54 * k, 150 * k, 170 * k);
		if (s.align != int(Align::Right))
			p.drawText(markBox, Qt::AlignLeft | Qt::AlignTop, QString(QChar(0x201C)));
		else
			p.drawText(QRectF(g.area.right() - 176 * k, markBox.top(), 150 * k, markBox.height()),
				   Qt::AlignRight | Qt::AlignTop, QString(QChar(0x201D)));
		drawText(p, g.text, text, words, textColour, k, bare, s.align);
		drawCaption(p, g.caption, r.caption, 32 * k, accent, horizontal(s.align), k, bare);
		break;
	}
	case Layout::LowerThird:
	default: {
		paintFill(p, g.area, QPainterPath(), s, r.picture);
		if (!bare)
			p.fillRect(QRectF(0, g.area.top(), W, 5 * k), accent);
		drawText(p, g.text, text, words, textColour, k, bare, s.align);
		drawCaption(p, g.caption, r.caption, 28 * k, accent, captionAlign(s.align), k, bare);
		break;
	}
	}
	return image;
}

} // namespace laycue::slides
