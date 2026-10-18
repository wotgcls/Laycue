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
	if (style.decor == int(Decor::OpenBible))
		drawOpenBible(p, S, seconds, accent);
	else
		drawGlowBible(p, S, seconds, accent);
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
