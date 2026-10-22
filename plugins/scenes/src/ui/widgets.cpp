#include "ui/widgets.hpp"

#include "ui/tokens.hpp"

#include <obs-module.h>

#include <QEnterEvent>
#include <QKeyEvent>
#include <QPainter>
#include <QPainterPath>

namespace laycue::ui {

namespace {

constexpr int kCardPadding = 5;
constexpr int kCardNameHeight = 30;

// Draws `image` to fill `rect` (cropping if the shapes differ), clipped to rounded corners.
void drawPicture(QPainter &p, const QRectF &rect, const QImage &image, qreal radius)
{
	QPainterPath clip;
	clip.addRoundedRect(rect, radius, radius);
	p.save();
	p.setClipPath(clip);
	p.fillRect(rect, Qt::black);
	if (!image.isNull()) {
		const QSizeF scaled = QSizeF(image.size()).scaled(rect.size(), Qt::KeepAspectRatioByExpanding);
		const QRectF target(rect.center().x() - scaled.width() / 2, rect.center().y() - scaled.height() / 2,
				    scaled.width(), scaled.height());
		p.setRenderHint(QPainter::SmoothPixmapTransform);
		p.drawImage(target, image);
	}
	p.restore();
}

// A dark rounded pill with a coloured dot and a short label, e.g. "● LIVE · Worship".
void drawPill(QPainter &p, const QPointF &topLeft, const QString &text, const QColor &dot, int pixelSize)
{
	const QFont font = uiFont(pixelSize, 600);
	const QFontMetricsF metrics(font);
	const qreal dotSize = pixelSize * 0.6;
	const qreal height = pixelSize + 9;
	const qreal width = 8 + dotSize + 6 + metrics.horizontalAdvance(text) + 9;
	const QRectF pill(topLeft, QSizeF(width, height));

	p.setPen(Qt::NoPen);
	p.setBrush(QColor(0, 0, 0, 165));
	p.drawRoundedRect(pill, height / 2, height / 2);
	p.setBrush(dot);
	p.drawEllipse(QRectF(pill.left() + 8, pill.center().y() - dotSize / 2, dotSize, dotSize));
	p.setPen(color::cream);
	p.setFont(font);
	p.drawText(QRectF(pill.left() + 8 + dotSize + 6, pill.top(), width, height), Qt::AlignVCenter | Qt::AlignLeft,
		   text);
}

} // namespace

// ----- PictureView -----

PictureView::PictureView(QWidget *parent) : QWidget(parent)
{
	QSizePolicy policy(QSizePolicy::Preferred, QSizePolicy::Preferred);
	policy.setHeightForWidth(true);
	setSizePolicy(policy);
	setAttribute(Qt::WA_OpaquePaintEvent, false);
}

void PictureView::setImage(const QImage &image)
{
	image_ = image;
	update();
}

void PictureView::clearImage()
{
	image_ = QImage();
	update();
}

void PictureView::setRing(const QColor &color, qreal width)
{
	ringColor_ = color;
	ringWidth_ = width;
	update();
}

void PictureView::setPill(const QString &text, const QColor &dotColor)
{
	pillText_ = text;
	pillDot_ = dotColor;
	update();
}

void PictureView::setPlaceholder(const QString &text)
{
	placeholder_ = text;
	update();
}

void PictureView::paintEvent(QPaintEvent *)
{
	QPainter p(this);
	p.setRenderHint(QPainter::Antialiasing);
	const qreal inset = ringWidth_ > 0 ? ringWidth_ / 2 : 0;
	const QRectF rect = QRectF(this->rect()).adjusted(inset, inset, -inset, -inset);

	if (image_.isNull()) {
		QPainterPath shape;
		shape.addRoundedRect(rect, size::pictureRadius, size::pictureRadius);
		p.fillPath(shape, color::graphite);
		if (!placeholder_.isEmpty()) {
			p.setPen(color::textTertiary);
			p.setFont(uiFont(12, 500));
			p.drawText(rect.adjusted(12, 8, -12, -8), Qt::AlignCenter | Qt::TextWordWrap, placeholder_);
		}
	} else {
		drawPicture(p, rect, image_, size::pictureRadius);
	}

	if (ringWidth_ > 0) {
		p.setPen(QPen(ringColor_, ringWidth_));
		p.setBrush(Qt::NoBrush);
		p.drawRoundedRect(rect, size::pictureRadius, size::pictureRadius);
	}
	if (!pillText_.isEmpty())
		drawPill(p, rect.topLeft() + QPointF(8, 8), pillText_, pillDot_, width() > 240 ? 11 : 10);
}

// ----- ShotCard -----

ShotCard::ShotCard(QWidget *parent) : QAbstractButton(parent)
{
	QSizePolicy policy(QSizePolicy::Preferred, QSizePolicy::Preferred);
	policy.setHeightForWidth(true);
	setSizePolicy(policy);
	setCursor(Qt::PointingHandCursor);
	setFocusPolicy(Qt::TabFocus);
}

void ShotCard::setImage(const QImage &image)
{
	image_ = image;
	update();
}

void ShotCard::setName(const QString &name)
{
	name_ = name;
	setAccessibleName(name);
	update();
}

void ShotCard::setState(State state)
{
	if (state_ == state)
		return;
	state_ = state;
	update();
}

int ShotCard::heightForWidth(int width) const
{
	const int pictureWidth = width - 2 * kCardPadding;
	return kCardPadding + pictureWidth * 9 / 16 + kCardNameHeight;
}

bool ShotCard::event(QEvent *event)
{
	// Space belongs to Go Live in this panel, even when a card has keyboard focus.
	if (event->type() == QEvent::ShortcutOverride) {
		auto *key = static_cast<QKeyEvent *>(event);
		if (key->key() == Qt::Key_Space) {
			event->ignore();
			return false;
		}
	}
	return QAbstractButton::event(event);
}

void ShotCard::keyPressEvent(QKeyEvent *event)
{
	if (event->key() == Qt::Key_Return || event->key() == Qt::Key_Enter) {
		click();
		return;
	}
	if (event->key() == Qt::Key_Space) {
		event->ignore(); // let the panel's Go Live shortcut have it
		return;
	}
	QAbstractButton::keyPressEvent(event);
}

void ShotCard::enterEvent(QEnterEvent *event)
{
	hovered_ = true;
	update();
	QAbstractButton::enterEvent(event);
}

void ShotCard::leaveEvent(QEvent *event)
{
	hovered_ = false;
	update();
	QAbstractButton::leaveEvent(event);
}

void ShotCard::paintEvent(QPaintEvent *)
{
	QPainter p(this);
	p.setRenderHint(QPainter::Antialiasing);

	QRectF card = QRectF(rect()).adjusted(1, 1, -1, -1);
	if (isDown()) {
		// Press feedback: shrink to 98% around the centre.
		const QPointF c = card.center();
		card = QRectF(c.x() - card.width() * 0.49, c.y() - card.height() * 0.49, card.width() * 0.98,
			      card.height() * 0.98);
	}

	QColor fill = hovered_ ? color::graphiteHover : color::graphite;
	QColor ring = color::line;
	qreal ringWidth = 1;
	if (state_ == State::Next) {
		fill = color::nextTint;
		ring = color::next;
		ringWidth = 2;
	} else if (state_ == State::Live) {
		ring = color::live;
		ringWidth = 1.5;
	}
	if (hasFocus()) {
		ring = color::setup;
		ringWidth = 2;
	}

	p.setPen(QPen(ring, ringWidth));
	p.setBrush(fill);
	p.drawRoundedRect(card, size::radius + 2, size::radius + 2);

	const qreal pictureWidth = card.width() - 2 * kCardPadding;
	const QRectF picture(card.left() + kCardPadding, card.top() + kCardPadding, pictureWidth,
			     pictureWidth * 9 / 16);
	drawPicture(p, picture, image_, 6);
	if (image_.isNull()) {
		QPainterPath shape;
		shape.addRoundedRect(picture, 6, 6);
		p.fillPath(shape, color::line);
	}

	if (state_ == State::Live)
		drawPill(p, picture.topLeft() + QPointF(5, 5), QString::fromUtf8(obs_module_text("Laycue.Tag.Live")),
			 color::live, 10);
	else if (state_ == State::Next)
		drawPill(p, picture.topLeft() + QPointF(5, 5), QString::fromUtf8(obs_module_text("Laycue.Tag.UpNext")),
			 color::next, 10);

	const QRectF nameRect(card.left() + kCardPadding + 3, picture.bottom() + 4, card.width() - 2 * kCardPadding - 6,
			      card.bottom() - picture.bottom() - 6);
	p.setPen(color::cream);
	p.setFont(uiFont(13, 600));
	p.drawText(nameRect, Qt::AlignLeft | Qt::AlignVCenter,
		   QFontMetrics(p.font()).elidedText(name_, Qt::ElideRight, int(nameRect.width())));
}

// ----- FeedButton -----

FeedButton::FeedButton(const QString &text, QWidget *parent) : QAbstractButton(parent)
{
	setText(text);
	setCheckable(true);
	setCursor(Qt::PointingHandCursor);
	setFocusPolicy(Qt::TabFocus);
	setFixedHeight(size::touch);
	setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
	setAccessibleName(text);
}

QSize FeedButton::sizeHint() const
{
	return {QFontMetrics(uiFont(12, 600)).horizontalAdvance(text()) + 24, size::touch};
}

void FeedButton::enterEvent(QEnterEvent *event)
{
	hovered_ = true;
	update();
	QAbstractButton::enterEvent(event);
}

void FeedButton::leaveEvent(QEvent *event)
{
	hovered_ = false;
	update();
	QAbstractButton::leaveEvent(event);
}

void FeedButton::paintEvent(QPaintEvent *)
{
	QPainter p(this);
	p.setRenderHint(QPainter::Antialiasing);
	QRectF box = QRectF(rect()).adjusted(1, 1, -1, -1);
	if (isDown()) {
		const QPointF c = box.center();
		box = QRectF(c.x() - box.width() * 0.49, c.y() - box.height() * 0.49, box.width() * 0.98,
			     box.height() * 0.98);
	}
	const bool on = isChecked();
	p.setPen(QPen(on ? color::live : QColor(255, 255, 255, 26), on ? 1.5 : 1));
	p.setBrush(on ? QColor(0x2A, 0x17, 0x14) : (hovered_ ? QColor(0x36, 0x34, 0x30) : color::control));
	p.drawRoundedRect(box, size::radius, size::radius);
	p.setPen(color::cream);
	p.setFont(uiFont(12, 600));
	const QString shown = QFontMetrics(p.font()).elidedText(text(), Qt::ElideRight, int(box.width()) - 16);
	p.drawText(box, Qt::AlignCenter, shown);
	if (hasFocus()) {
		p.setBrush(Qt::NoBrush);
		p.setPen(QPen(color::setup, 2));
		p.drawRoundedRect(QRectF(rect()).adjusted(1, 1, -1, -1), size::radius, size::radius);
	}
}

} // namespace laycue::ui
