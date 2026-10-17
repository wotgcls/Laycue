#include "slides/slide-render.hpp"

#include <obs-module.h>

#include <QFont>
#include <QFontDatabase>
#include <QFontMetricsF>
#include <QLinearGradient>
#include <QPainter>
#include <QPainterPath>
#include <QStringList>

#include <algorithm>

namespace laycue::slides {

namespace {

QString g_family = QStringLiteral("Outfit");

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

QFont fontOf(double px, int weight)
{
	QFont f(g_family);
	f.setPixelSize(std::max(8, int(px)));
	f.setWeight(QFont::Weight(weight));
	return f;
}

// The biggest size (down to minPx) at which the text, wrapped, fits the box.
QFont fitFont(const QString &text, const QRectF &box, double maxPx, double minPx, int weight)
{
	for (double px = maxPx; px > minPx; px -= 2.0) {
		const QFont f = fontOf(px, weight);
		const QRectF need = QFontMetricsF(f).boundingRect(box, Qt::AlignCenter | Qt::TextWordWrap, text);
		if (need.height() <= box.height() && need.width() <= box.width() + 1.0)
			return f;
	}
	return fontOf(minPx, weight);
}

// Words with a soft shadow, and an outline when nothing is behind them, so they read on any video.
void drawText(QPainter &p, const QRectF &box, const QString &text, const QFont &font, const QColor &colour,
	      double scale, bool outline)
{
	p.setFont(font);
	const int flags = Qt::AlignCenter | Qt::TextWordWrap;
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

} // namespace

void loadSlideFonts()
{
	static bool loaded = false;
	if (loaded)
		return;
	loaded = true;
	for (const char *file : {"fonts/Outfit-SemiBold.ttf", "fonts/Outfit-Bold.ttf"}) {
		char *path = obs_module_file(file);
		if (!path)
			continue;
		const int id = QFontDatabase::addApplicationFont(QString::fromUtf8(path));
		bfree(path);
		const QStringList families = QFontDatabase::applicationFontFamilies(id);
		if (!families.isEmpty())
			g_family = families.front();
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
	const double k = H / 1080.0;               // canvas scale
	const double size = s.sizePercent / 100.0; // the operator's text size
	const QString text = applyCase(r.text, TextCase(s.textCase));
	const QColor textColour = rgb(s.text), accent = rgb(s.accent);
	const bool bare = s.fill == int(Fill::None);

	QPainter p(&image);
	p.setRenderHint(QPainter::Antialiasing, true);
	p.setRenderHint(QPainter::TextAntialiasing, true);
	p.setRenderHint(QPainter::SmoothPixmapTransform, true);

	switch (Layout(s.layout)) {
	case Layout::FullScreen: {
		paintFill(p, QRectF(0, 0, W, H), QPainterPath(), s, r.picture);
		const QRectF box(W * 0.08, H * 0.12, W * 0.84, H * 0.66);
		drawText(p, box, text, fitFont(text, box, 96 * k * size, 26 * k, 700), textColour, k, bare);
		drawCaption(p, QRectF(W * 0.08, H * 0.80, W * 0.84, H * 0.10), r.caption, 50 * k, accent,
			    Qt::AlignRight, k, bare);
		break;
	}
	case Layout::Card: {
		const QRectF card(W * 0.08, H * 0.665, W * 0.84, H * 0.28);
		QPainterPath path;
		path.addRoundedRect(card, 22 * k, 22 * k);
		paintFill(p, card, path, s, r.picture);
		if (!bare) {
			QLinearGradient edge(card.topLeft(), card.bottomRight());
			edge.setColorAt(0, lighter(accent));
			edge.setColorAt(0.5, accent);
			edge.setColorAt(1, lighter(accent));
			p.setPen(QPen(QBrush(edge), 4 * k));
			p.drawPath(path);
		}
		if (!r.caption.isEmpty()) {
			p.setFont(fontOf(26 * k, 700));
			const double pillW = QFontMetricsF(p.font()).horizontalAdvance(r.caption) + 36 * k;
			const QRectF pill(card.left() + 28 * k, card.top() - 20 * k, pillW, 40 * k);
			QPainterPath pp;
			pp.addRoundedRect(pill, 20 * k, 20 * k);
			p.fillPath(pp, accent);
			p.setPen(onColour(accent));
			p.drawText(pill, Qt::AlignCenter, r.caption);
		}
		const QRectF box = card.adjusted(40 * k, 30 * k, -40 * k, -22 * k);
		drawText(p, box, text, fitFont(text, box, 58 * k * size, 20 * k, 600), textColour, k, bare);
		break;
	}
	case Layout::LowerThird:
	default: {
		const QRectF band(0, H * 0.70, W, H * 0.25);
		paintFill(p, band, QPainterPath(), s, r.picture);
		if (!bare)
			p.fillRect(QRectF(0, band.top(), W, 5 * k), accent);
		const double captionRoom = r.caption.isEmpty() ? 40 : 76;
		const QRectF box(W * 0.06, band.top() + 22 * k, W * 0.88, band.height() - captionRoom * k);
		drawText(p, box, text, fitFont(text, box, 62 * k * size, 20 * k, 600), textColour, k, bare);
		drawCaption(p, QRectF(W * 0.06, band.bottom() - 52 * k, W * 0.88, 40 * k), r.caption, 28 * k, accent,
			    Qt::AlignRight, k, bare);
		break;
	}
	}
	return image;
}

} // namespace laycue::slides
