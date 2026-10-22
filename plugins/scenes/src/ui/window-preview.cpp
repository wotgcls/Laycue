#include "ui/window-preview.hpp"

#include "ui/labels.hpp"
#include "ui/tokens.hpp"

#include <QPainter>
#include <QPainterPath>

#include <algorithm>

namespace laycue::ui {

WindowPreview::WindowPreview(QWidget *parent) : QWidget(parent)
{
	QSizePolicy policy(QSizePolicy::Preferred, QSizePolicy::Preferred);
	policy.setHeightForWidth(true);
	setSizePolicy(policy);
	setMinimumHeight(160);
}

void WindowPreview::setPicture(const QImage &image)
{
	image_ = image;
	found_ = false;
	updateGeometry();
	update();
}

void WindowPreview::setWindow(const Rect &window, bool found, bool confident)
{
	window_ = window;
	found_ = found;
	confident_ = confident;
	update();
}

int WindowPreview::heightForWidth(int width) const
{
	if (image_.isNull())
		return width * 9 / 16;
	return std::max(160, width * image_.height() / std::max(1, image_.width()));
}

void WindowPreview::paintEvent(QPaintEvent *)
{
	QPainter p(this);
	p.setRenderHint(QPainter::Antialiasing);
	const QRectF area = QRectF(rect()).adjusted(0.5, 0.5, -0.5, -0.5);

	QPainterPath frame;
	frame.addRoundedRect(area, size::pictureRadius, size::pictureRadius);
	p.fillPath(frame, color::graphite);
	if (image_.isNull()) {
		p.setPen(color::textTertiary);
		p.setFont(uiFont(12, 500));
		p.drawText(area.adjusted(16, 8, -16, -8), Qt::AlignCenter | Qt::TextWordWrap,
			   lt("Laycue.Finder.Empty"));
		return;
	}

	// Fit the picture inside the area.
	const QSizeF fitted = QSizeF(image_.size()).scaled(area.size(), Qt::KeepAspectRatio);
	const QRectF target(area.center().x() - fitted.width() / 2, area.center().y() - fitted.height() / 2,
			    fitted.width(), fitted.height());
	const qreal scale = fitted.width() / image_.width();

	p.save();
	p.setClipPath(frame);
	// A checkerboard under see-through areas, so they read as "empty".
	const int cell = 10;
	for (int y = 0; y * cell < target.height(); ++y) {
		for (int x = 0; x * cell < target.width(); ++x) {
			const QRectF square(target.left() + x * cell, target.top() + y * cell, cell, cell);
			p.fillRect(square.intersected(target), (x + y) % 2 ? color::line : color::graphite);
		}
	}
	p.setRenderHint(QPainter::SmoothPixmapTransform);
	p.drawImage(target, image_);

	if (found_) {
		const QRectF window(target.left() + window_.x * scale, target.top() + window_.y * scale,
				    window_.width * scale, window_.height * scale);
		// Dim everything except the window, then outline it.
		QPainterPath outside;
		outside.addRect(target);
		QPainterPath hole;
		hole.addRect(window);
		QColor dim = color::ink;
		dim.setAlpha(150);
		p.fillPath(outside.subtracted(hole), dim);
		p.setPen(QPen(confident_ ? color::setup : color::overlay, 2));
		p.setBrush(Qt::NoBrush);
		p.drawRect(window.adjusted(1, 1, -1, -1));
	}
	p.restore();
}

} // namespace laycue::ui
