#include "slides/slide-render.hpp"

#include <obs-module.h>

#include <QFont>
#include <QFontDatabase>
#include <QFontMetricsF>
#include <QLinearGradient>
#include <QPainter>
#include <QPainterPath>

#include <algorithm>

namespace laycue::slides {

namespace {

QString g_family = QStringLiteral("Outfit");

const QColor kGold(0xE0, 0xA9, 0x1E);
const QColor kGoldLight(0xFF, 0xE9, 0x9A);
const QColor kInk(0x0C, 0x10, 0x1C);
const QColor kNavy(0x14, 0x21, 0x3D);

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

// Text with a soft shadow (and an outline for the panel-less look), so it reads on any picture.
void drawText(QPainter &p, const QRectF &box, const QString &text, const QFont &font, const QColor &colour,
	      double scale, bool outline)
{
	p.setFont(font);
	const int flags = Qt::AlignCenter | Qt::TextWordWrap;
	p.setPen(QColor(0, 0, 0, outline ? 200 : 130));
	for (const QPointF offset : {QPointF(0, 3 * scale), QPointF(2 * scale, 3 * scale)})
		p.drawText(box.translated(offset), flags, text);
	if (outline) {
		for (const QPointF offset : {QPointF(-2 * scale, 0), QPointF(2 * scale, 0), QPointF(0, -2 * scale)})
			p.drawText(box.translated(offset), flags, text);
	}
	p.setPen(colour);
	p.drawText(box, flags, text);
}

void drawCaption(QPainter &p, const QRectF &box, const QString &caption, double px, const QColor &colour,
		 Qt::Alignment align, double scale)
{
	if (caption.isEmpty())
		return;
	p.setFont(fontOf(px, 700));
	p.setPen(QColor(0, 0, 0, 140));
	p.drawText(box.translated(0, 2 * scale), int(align | Qt::AlignVCenter), caption);
	p.setPen(colour);
	p.drawText(box, int(align | Qt::AlignVCenter), caption);
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

QImage renderSlide(const SlideRequest &r)
{
	const int W = std::max(16, r.canvasWidth), H = std::max(16, r.canvasHeight);
	QImage image(W, H, QImage::Format_ARGB32_Premultiplied);
	image.fill(Qt::transparent);
	if (r.text.trimmed().isEmpty() && r.caption.trimmed().isEmpty())
		return image;
	const double s = H / 1080.0;
	QPainter p(&image);
	p.setRenderHint(QPainter::Antialiasing, true);
	p.setRenderHint(QPainter::TextAntialiasing, true);
	p.setRenderHint(QPainter::SmoothPixmapTransform, true);

	switch (r.look) {
	case Look::Full: {
		const QRectF all(0, 0, W, H);
		if (!r.background.isNull()) {
			// Cover the canvas, cropping the picture evenly.
			const QSizeF size =
				r.background.size().scaled(all.size().toSize(), Qt::KeepAspectRatioByExpanding);
			const QRectF target((W - size.width()) / 2.0, (H - size.height()) / 2.0, size.width(),
					    size.height());
			p.drawImage(target, r.background);
			p.fillRect(all, QColor(0, 0, 0, 70)); // a little darker, so white text always reads
		} else {
			QLinearGradient g(0, 0, W, H);
			g.setColorAt(0, QColor(0x14, 0x24, 0x5C));
			g.setColorAt(1, QColor(0x3A, 0x1E, 0x5E));
			p.fillRect(all, g);
		}
		const QRectF box(W * 0.08, H * 0.12, W * 0.84, H * 0.66);
		drawText(p, box, r.text, fitFont(r.text, box, 96 * s, 30 * s, 700), Qt::white, s, false);
		drawCaption(p, QRectF(W * 0.08, H * 0.80, W * 0.84, H * 0.10), r.caption, 52 * s, kGold, Qt::AlignRight,
			    s);
		break;
	}
	case Look::Card: {
		const QRectF card(W * 0.08, H * 0.665, W * 0.84, H * 0.28);
		QPainterPath path;
		path.addRoundedRect(card, 22 * s, 22 * s);
		p.fillPath(path, QColor(kNavy.red(), kNavy.green(), kNavy.blue(), 236));
		QLinearGradient edge(card.topLeft(), card.bottomRight());
		edge.setColorAt(0, kGoldLight);
		edge.setColorAt(0.5, kGold);
		edge.setColorAt(1, kGoldLight);
		p.setPen(QPen(QBrush(edge), 4 * s));
		p.drawPath(path);
		if (!r.caption.isEmpty()) {
			p.setFont(fontOf(26 * s, 700));
			const double pillW = QFontMetricsF(p.font()).horizontalAdvance(r.caption) + 36 * s;
			const QRectF pill(card.left() + 28 * s, card.top() - 20 * s, pillW, 40 * s);
			QPainterPath pp;
			pp.addRoundedRect(pill, 20 * s, 20 * s);
			p.fillPath(pp, kGold);
			p.setPen(kInk);
			p.drawText(pill, Qt::AlignCenter, r.caption);
		}
		const QRectF box = card.adjusted(40 * s, 30 * s, -40 * s, -22 * s);
		drawText(p, box, r.text, fitFont(r.text, box, 58 * s, 22 * s, 600), Qt::white, s, false);
		break;
	}
	case Look::Clean: {
		const QRectF box(W * 0.07, H * 0.70, W * 0.86, H * 0.22);
		drawText(p, box, r.text, fitFont(r.text, box, 64 * s, 24 * s, 700), Qt::white, s, true);
		drawCaption(p, QRectF(W * 0.07, H * 0.925, W * 0.86, H * 0.05), r.caption, 26 * s, kGoldLight,
			    Qt::AlignCenter, s);
		break;
	}
	case Look::Bar:
	default: {
		const QRectF band(0, H * 0.70, W, H * 0.25);
		QLinearGradient g(band.topLeft(), band.bottomLeft());
		g.setColorAt(0, QColor(kInk.red(), kInk.green(), kInk.blue(), 225));
		g.setColorAt(1, QColor(kInk.red(), kInk.green(), kInk.blue(), 245));
		p.fillRect(band, g);
		p.fillRect(QRectF(0, band.top(), W, 5 * s), kGold);
		const QRectF box(W * 0.06, band.top() + 22 * s, W * 0.88,
				 band.height() - (r.caption.isEmpty() ? 40 : 76) * s);
		drawText(p, box, r.text, fitFont(r.text, box, 60 * s, 22 * s, 600), Qt::white, s, false);
		drawCaption(p, QRectF(W * 0.06, band.bottom() - 52 * s, W * 0.88, 40 * s), r.caption, 28 * s, kGold,
			    Qt::AlignRight, s);
		break;
	}
	}
	return image;
}

} // namespace laycue::slides
