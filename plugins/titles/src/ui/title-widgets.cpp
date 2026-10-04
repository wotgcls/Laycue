#include "ui/title-widgets.hpp"

#include "ui/tokens.hpp"

#include <QKeyEvent>
#include <QLinearGradient>
#include <QPainter>
#include <QPainterPath>
#include <QRadialGradient>

#include <algorithm>

namespace laycue::titles::ui {

using namespace laycue::ui;

QImage stageBackground(int width, int height)
{
	QImage img(std::max(width, 1), std::max(height, 1), QImage::Format_ARGB32_Premultiplied);
	QPainter p(&img);
	QLinearGradient base(0, 0, 0, height);
	base.setColorAt(0.0, QColor(0x3A, 0x3F, 0x4D));
	base.setColorAt(1.0, QColor(0x17, 0x18, 0x1E));
	p.fillRect(img.rect(), base);
	QRadialGradient glow(width * 0.62, height * 0.32, width * 0.5);
	glow.setColorAt(0.0, QColor(255, 236, 200, 90));
	glow.setColorAt(1.0, QColor(255, 236, 200, 0));
	p.fillRect(img.rect(), glow);
	return img;
}

// ---- TitlePreview ----------------------------------------------------------------------------------------

TitlePreview::TitlePreview(QWidget *parent) : QWidget(parent)
{
	setMinimumHeight(135);
	setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
	timer_.setInterval(16);
	connect(&timer_, &QTimer::timeout, this, &TitlePreview::tick);
	tpl_ = builtInTemplates().front();
	configureClock();
}

void TitlePreview::configureClock()
{
	clock_.configure(tpl_.inSeconds, tpl_.outSeconds, 0.0); // the panel decides when to hide, not the preview
	const FontSet *fonts = sharedFonts();
	ticker_.configure(fonts ? tickerStripWidth(*fonts, tpl_, text_) : 1.0, tpl_.tickerSpeed);
}

void TitlePreview::setTitle(const TitleTemplate &tpl, const TitleText &text, bool useBrand, const Rgba &brand)
{
	tpl_ = tpl;
	text_ = text;
	useBrand_ = useBrand;
	brand_ = brand;
	configureClock();
	update();
}

void TitlePreview::setOnAir(bool onAir)
{
	if (onAir_ == onAir)
		return;
	onAir_ = onAir;
	update();
}

void TitlePreview::play()
{
	clock_.reset();
	clock_.show();
	playing_ = true;
	elapsed_.start();
	lastMs_ = 0;
	timer_.start();
	update();
}

void TitlePreview::stop()
{
	if (!playing_)
		return;
	clock_.hide();
	if (!timer_.isActive()) {
		elapsed_.start();
		lastMs_ = 0;
		timer_.start();
	}
}

void TitlePreview::tick()
{
	const qint64 now = elapsed_.elapsed();
	const double dt = std::min(0.1, double(now - lastMs_) / 1000.0);
	lastMs_ = now;
	clock_.advance(dt);
	if (tpl_.tickerSpeed > 0.0 && clock_.visible())
		ticker_.advance(dt);
	if (!clock_.visible()) {
		playing_ = false;
		timer_.stop();
	}
	update();
}

void TitlePreview::paintEvent(QPaintEvent *)
{
	QPainter p(this);
	p.setRenderHint(QPainter::Antialiasing, true);
	const QRectF area = QRectF(rect()).adjusted(2, 2, -2, -2);
	QPainterPath clip;
	clip.addRoundedRect(area, size::pictureRadius, size::pictureRadius);
	p.setClipPath(clip);
	p.drawImage(area.toRect(), stageBackground(area.width(), area.height()));

	if (const FontSet *fonts = sharedFonts()) {
		RenderRequest req;
		req.tpl = tpl_;
		req.text = text_;
		req.canvasWidth = std::max(1, int(area.width()));
		req.canvasHeight = std::max(1, int(area.height()));
		req.useAccentOverride = useBrand_;
		req.accentOverride = brand_;
		if (playing_) {
			req.frame = {clock_.in(), clock_.out(), ticker_.offset()};
		} else {
			req.frame = {1.0, 0.0, ticker_.offset()};
		}
		const RenderedTitle r = renderTitle(*fonts, req);
		if (!r.image.isNull())
			p.drawImage(area.left() + r.x, area.top() + r.y, r.image);
	}

	p.setClipping(false);
	p.setBrush(Qt::NoBrush);
	p.setPen(QPen(onAir_ ? color::live : QColor(255, 255, 255, 28), onAir_ ? 2.0 : 1.0));
	p.drawRoundedRect(area, size::pictureRadius, size::pictureRadius);
}

// ---- TemplateCard ----------------------------------------------------------------------------------------

TemplateCard::TemplateCard(QWidget *parent) : QAbstractButton(parent)
{
	setCheckable(false);
	setCursor(Qt::PointingHandCursor);
	setFocusPolicy(Qt::TabFocus);
}

void TemplateCard::setImage(const QImage &image)
{
	image_ = image;
	update();
}

void TemplateCard::setName(const QString &name)
{
	name_ = name;
	setAccessibleName(name);
	update();
}

void TemplateCard::setCustom(bool custom)
{
	custom_ = custom;
	update();
}

void TemplateCard::setSelectedCard(bool selected)
{
	selected_ = selected;
	update();
}

int TemplateCard::heightForWidth(int width) const
{
	return (width - 12) * 9 / 16 + 12 + 26;
}

void TemplateCard::enterEvent(QEnterEvent *)
{
	hovered_ = true;
	update();
}

void TemplateCard::leaveEvent(QEvent *)
{
	hovered_ = false;
	update();
}

void TemplateCard::keyPressEvent(QKeyEvent *event)
{
	if (event->key() == Qt::Key_Space || event->key() == Qt::Key_Return || event->key() == Qt::Key_Enter)
		click();
	else
		QAbstractButton::keyPressEvent(event);
}

void TemplateCard::paintEvent(QPaintEvent *)
{
	QPainter p(this);
	p.setRenderHint(QPainter::Antialiasing, true);
	p.setRenderHint(QPainter::SmoothPixmapTransform, true);
	const QRectF outer = QRectF(rect()).adjusted(1, 1, -1, -1);
	p.setPen(Qt::NoPen);
	p.setBrush(selected_ ? color::stage : hovered_ ? color::graphiteHover : color::graphite);
	p.drawRoundedRect(outer, size::radius + 2, size::radius + 2);

	const QRectF pic(outer.left() + 6, outer.top() + 6, outer.width() - 12, (outer.width() - 12) * 9 / 16);
	QPainterPath clip;
	clip.addRoundedRect(pic, 6, 6);
	p.save();
	p.setClipPath(clip);
	if (!image_.isNull())
		p.drawImage(pic, image_);
	else
		p.fillRect(pic, color::recess);
	p.restore();

	p.setPen(selected_ ? color::cream : color::textSecondary);
	p.setFont(uiFont(12, selected_ ? 600 : 500));
	const QRectF label(outer.left() + 8, pic.bottom() + 2, outer.width() - 16, 24);
	p.drawText(label, Qt::AlignLeft | Qt::AlignVCenter,
		   p.fontMetrics().elidedText(name_, Qt::ElideRight, int(label.width() - (custom_ ? 16 : 0))));
	if (custom_) {
		p.setPen(color::overlay);
		p.drawText(label, Qt::AlignRight | Qt::AlignVCenter, QStringLiteral("★"));
	}

	if (selected_ || hasFocus()) {
		p.setBrush(Qt::NoBrush);
		p.setPen(QPen(color::setup, 2));
		p.drawRoundedRect(outer, size::radius + 2, size::radius + 2);
	}
}

} // namespace laycue::titles::ui
