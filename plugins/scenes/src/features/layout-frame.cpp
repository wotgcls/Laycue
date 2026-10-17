#include "features/layout-frame.hpp"

#include <QColor>
#include <QDir>
#include <QFileInfo>
#include <QImage>
#include <QLinearGradient>
#include <QPainter>
#include <QPainterPath>

namespace laycue {

namespace {
QColor toColor(Rgb8 c)
{
	return QColor(c.r, c.g, c.b);
}
} // namespace

bool renderFramePicture(const std::string &path, int canvasWidth, int canvasHeight, const std::vector<RectF> &frames,
			double thickness, Metal metal)
{
	if (canvasWidth <= 0 || canvasHeight <= 0 || path.empty())
		return false;
	QImage image(canvasWidth, canvasHeight, QImage::Format_ARGB32_Premultiplied);
	image.fill(Qt::transparent);
	{
		QPainter painter(&image);
		painter.setRenderHint(QPainter::Antialiasing, true);
		const MetalTones tones = metalTones(metal);
		const double radius = thickness * 1.2;
		for (const RectF &slot : frames) {
			const QRectF inner(slot.x, slot.y, slot.w, slot.h);
			const QRectF outer = inner.adjusted(-thickness, -thickness, thickness, thickness);
			QPainterPath ring;
			ring.setFillRule(Qt::OddEvenFill);
			ring.addRoundedRect(outer, radius, radius);
			ring.addRect(inner); // the picture inside stays square: the edge hugs it
			// A diagonal sheen: light, metal, dark, metal, light, as polished metal catches the light.
			QLinearGradient sheen(outer.topLeft(), outer.bottomRight());
			sheen.setColorAt(0.0, toColor(tones.light));
			sheen.setColorAt(0.28, toColor(tones.mid));
			sheen.setColorAt(0.5, toColor(tones.dark));
			sheen.setColorAt(0.72, toColor(tones.mid));
			sheen.setColorAt(1.0, toColor(tones.light));
			painter.fillPath(ring, sheen);
		}
	}
	QDir().mkpath(QFileInfo(QString::fromStdString(path)).absolutePath());
	return image.save(QString::fromStdString(path), "PNG");
}

} // namespace laycue
