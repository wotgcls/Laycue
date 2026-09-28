#include "ui/controls.hpp"

#include "ui/tokens.hpp"

#include <obs-module.h>

#include <algorithm>

#include <QDynamicPropertyChangeEvent>
#include <QHBoxLayout>
#include <QKeyEvent>
#include <QLabel>
#include <QMouseEvent>
#include <QPainter>
#include <QPainterPath>
#include <QPushButton>
#include <QVBoxLayout>

namespace laycue::ui {

// ----- Surface -----

Surface::Surface(int level, QWidget *parent) : QFrame(parent), level_(level)
{
	setAttribute(Qt::WA_StyledBackground, false);
	setPadding(12, 12, 12, 12);
}

void Surface::setPadding(int left, int top, int right, int bottom)
{
	const int m = size::shadowMargin;
	if (layout())
		layout()->setContentsMargins(left + m, top + m, right + m, bottom + m + 2);
	setContentsMargins(0, 0, 0, 0);
	// Remember for layouts assigned later.
	setProperty("padL", left);
	setProperty("padT", top);
	setProperty("padR", right);
	setProperty("padB", bottom);
}

void Surface::paintEvent(QPaintEvent *)
{
	QPainter p(this);
	p.setRenderHint(QPainter::Antialiasing);
	const int m = size::shadowMargin;
	const QRectF body = QRectF(rect()).adjusted(m, m, -m, -m - 2);
	const qreal radius = level_ == 2 ? 16 : 12;

	// Soft shadow: a few faint rounded rectangles, wider and lower each time.
	p.setPen(Qt::NoPen);
	const int layers = level_ == 2 ? 6 : 4;
	for (int i = layers; i >= 1; --i) {
		QColor shadow(0, 0, 0, (level_ == 2 ? 26 : 20) - i * 2);
		p.setBrush(shadow);
		p.drawRoundedRect(body.adjusted(-i, -i + (level_ == 2 ? 3 : 2), i, i + (level_ == 2 ? 4 : 2)),
				  radius + i, radius + i);
	}

	// Surface fill and hairline border.
	const QColor fill = level_ == 2 ? color::stage : color::graphite;
	p.setBrush(fill);
	p.setPen(QPen(QColor(255, 255, 255, level_ == 2 ? 22 : 14), 1));
	p.drawRoundedRect(body, radius, radius);

	// Top highlight: light catching the upper edge.
	QPainterPath clip;
	clip.addRoundedRect(body, radius, radius);
	p.setClipPath(clip);
	p.setPen(QPen(QColor(255, 255, 255, level_ == 2 ? 30 : 18), 1));
	p.drawLine(QPointF(body.left() + radius, body.top() + 0.5), QPointF(body.right() - radius, body.top() + 0.5));
}

// ----- Segmented -----

Segmented::Segmented(QWidget *parent) : QWidget(parent)
{
	setFocusPolicy(Qt::TabFocus);
	setMouseTracking(true);
	setCursor(Qt::PointingHandCursor);
	setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
	setAccessibleName(QStringLiteral("Segmented"));
}

void Segmented::setItems(const QStringList &labels)
{
	labels_ = labels;
	enabled_.clear();
	for (int i = 0; i < labels.size(); ++i)
		enabled_.push_back(true);
	current_ = std::clamp(current_, 0, std::max(0, int(labels.size()) - 1));
	updateGeometry();
	update();
}

void Segmented::setCurrent(int index)
{
	if (index == current_ || index < 0 || index >= labels_.size())
		return;
	current_ = index;
	update();
}

void Segmented::setSegmentEnabled(int index, bool enabled)
{
	if (index >= 0 && index < enabled_.size()) {
		enabled_[index] = enabled;
		update();
	}
}

QSize Segmented::sizeHint() const
{
	int width = 8;
	QFontMetrics metrics(uiFont(12, 600));
	for (const QString &label : labels_)
		width += metrics.horizontalAdvance(label) + 28;
	return {width, controlHeight()};
}

QSize Segmented::minimumSizeHint() const
{
	return {sizeHint().width() * 3 / 4, controlHeight()};
}

int Segmented::controlHeight() const
{
	return property("large").toBool() ? size::touch : size::controlHeight;
}

QRectF Segmented::segmentRect(int index) const
{
	const QRectF track = QRectF(rect()).adjusted(2, 2, -2, -2);
	const qreal each = track.width() / std::max(1, static_cast<int>(labels_.size()));
	return QRectF(track.left() + each * index, track.top(), each, track.height());
}

int Segmented::segmentAt(const QPoint &pos) const
{
	for (int i = 0; i < labels_.size(); ++i) {
		if (segmentRect(i).contains(pos))
			return i;
	}
	return -1;
}

void Segmented::paintEvent(QPaintEvent *)
{
	QPainter p(this);
	p.setRenderHint(QPainter::Antialiasing);
	const QRectF track = QRectF(rect()).adjusted(0.5, 0.5, -0.5, -0.5);
	const qreal radius = size::radius + 1;

	// Sunken track.
	p.setPen(QPen(QColor(255, 255, 255, 12), 1));
	p.setBrush(color::recess);
	p.drawRoundedRect(track, radius, radius);

	for (int i = 0; i < labels_.size(); ++i) {
		const QRectF seg = segmentRect(i).adjusted(1, 1, -1, -1);
		const bool on = i == current_;
		if (on) {
			// Raised selected segment, with a top highlight and a small shadow.
			p.setPen(Qt::NoPen);
			p.setBrush(QColor(0, 0, 0, 90));
			p.drawRoundedRect(seg.adjusted(0, 1.5, 0, 1.5), radius - 2, radius - 2);
			p.setBrush(color::control);
			p.drawRoundedRect(seg, radius - 2, radius - 2);
			p.setPen(QPen(QColor(255, 255, 255, 26), 1));
			p.drawLine(QPointF(seg.left() + radius, seg.top() + 0.5),
				   QPointF(seg.right() - radius, seg.top() + 0.5));
		} else if (i == hovered_ && enabled_[i]) {
			p.setPen(Qt::NoPen);
			p.setBrush(QColor(255, 255, 255, 10));
			p.drawRoundedRect(seg, radius - 2, radius - 2);
		}
		p.setPen(!enabled_[i] ? color::textDisabled : on ? color::cream : color::textTertiary);
		p.setFont(uiFont(12, 600));
		p.drawText(seg, Qt::AlignCenter, labels_[i]);
	}

	if (hasFocus()) {
		p.setBrush(Qt::NoBrush);
		p.setPen(QPen(color::setup, 2));
		p.drawRoundedRect(QRectF(rect()).adjusted(1, 1, -1, -1), radius, radius);
	}
}

void Segmented::mousePressEvent(QMouseEvent *event)
{
	pressed_ = segmentAt(event->pos());
}

void Segmented::mouseReleaseEvent(QMouseEvent *event)
{
	const int released = segmentAt(event->pos());
	if (released >= 0 && released == pressed_ && enabled_[released] && released != current_) {
		current_ = released;
		update();
		emit changed(current_);
	}
	pressed_ = -1;
}

void Segmented::keyPressEvent(QKeyEvent *event)
{
	int step = 0;
	if (event->key() == Qt::Key_Left)
		step = -1;
	else if (event->key() == Qt::Key_Right)
		step = 1;
	else
		return QWidget::keyPressEvent(event);
	for (int i = current_ + step; i >= 0 && i < labels_.size(); i += step) {
		if (enabled_[i]) {
			current_ = i;
			update();
			emit changed(current_);
			break;
		}
	}
}

void Segmented::mouseMoveEvent(QMouseEvent *event)
{
	const int now = segmentAt(event->pos());
	if (now != hovered_) {
		hovered_ = now;
		update();
	}
}

void Segmented::leaveEvent(QEvent *)
{
	hovered_ = -1;
	update();
}

// ----- Switch -----

Switch::Switch(QWidget *parent) : QAbstractButton(parent)
{
	setCheckable(true);
	setCursor(Qt::PointingHandCursor);
	setFocusPolicy(Qt::TabFocus);
	setFixedSize(sizeHint());
	animation_.setDuration(120);
	animation_.setEasingCurve(QEasingCurve::OutCubic);
	connect(&animation_, &QVariantAnimation::valueChanged, this,
		[this](const QVariant &value) { setKnob(value.toReal()); });
}

void Switch::setKnob(qreal value)
{
	knob_ = value;
	update();
}

void Switch::setOn(bool on)
{
	animation_.stop();
	QSignalBlocker blocker(this);
	setChecked(on);
	knob_ = on ? 1 : 0;
	update();
}

void Switch::nextCheckState()
{
	QAbstractButton::nextCheckState();
	animation_.stop();
	animation_.setStartValue(knob_);
	animation_.setEndValue(isChecked() ? 1.0 : 0.0);
	animation_.start();
}

void Switch::paintEvent(QPaintEvent *)
{
	QPainter p(this);
	p.setRenderHint(QPainter::Antialiasing);
	const QRectF track = QRectF(rect()).adjusted(1, 1, -1, -1);
	const qreal r = track.height() / 2;

	QColor off = color::control;
	QColor on = color::setup;
	auto mix = [](const QColor &a, const QColor &b, qreal t) {
		return QColor(int(a.red() + (b.red() - a.red()) * t), int(a.green() + (b.green() - a.green()) * t),
			      int(a.blue() + (b.blue() - a.blue()) * t));
	};
	p.setPen(Qt::NoPen);
	p.setBrush(mix(off, on, knob_));
	p.drawRoundedRect(track, r, r);

	const qreal d = track.height() - 6;
	const qreal x = track.left() + 3 + (track.width() - d - 6) * knob_;
	p.setBrush(QColor(0, 0, 0, 70));
	p.drawEllipse(QRectF(x, track.top() + 3.8, d, d));
	p.setBrush(color::cream);
	p.drawEllipse(QRectF(x, track.top() + 3, d, d));

	if (hasFocus()) {
		p.setBrush(Qt::NoBrush);
		p.setPen(QPen(color::setup, 2));
		p.drawRoundedRect(QRectF(rect()).adjusted(0, 0, 0, 0), r + 1, r + 1);
	}
	if (!isEnabled()) {
		p.setPen(Qt::NoPen);
		p.setBrush(QColor(0, 0, 0, 110));
		p.drawRoundedRect(track, r, r);
	}
}

// ----- TipCard -----

TipCard::TipCard(const QString &title, const QString &body, QWidget *parent) : Surface(1, parent)
{
	auto *root = new QHBoxLayout(this);
	setPadding(12, 10, 12, 10);
	root->setSpacing(10);

	auto *dot = new QLabel(this);
	dot->setFixedSize(8, 8);
	dot->setStyleSheet(QStringLiteral("background: %1; border-radius: 4px;").arg(hex(color::setup)));
	root->addWidget(dot, 0, Qt::AlignTop);

	auto *text = new QVBoxLayout();
	text->setSpacing(2);
	auto *heading = new QLabel(title, this);
	heading->setFont(uiFont(13, 700));
	heading->setStyleSheet(labelStyle(color::cream, heading->font()));
	heading->setWordWrap(true); // a long title must wrap, not widen the whole panel
	auto *copy = new QLabel(body, this);
	copy->setFont(uiFont(12, 400));
	copy->setStyleSheet(labelStyle(color::textSecondary, copy->font()));
	copy->setWordWrap(true);
	text->addWidget(heading);
	text->addWidget(copy);
	root->addLayout(text, 1);

	auto *close =
		new ActionButton(QString::fromUtf8(obs_module_text("Laycue.GotIt")), ActionButton::Kind::Link, this);
	connect(close, &QAbstractButton::clicked, this, &TipCard::dismissed);
	root->addWidget(close, 0, Qt::AlignTop);
}

// ----- ActionButton -----

ActionButton::ActionButton(const QString &text, Kind kind, QWidget *parent) : QAbstractButton(parent), kind_(kind)
{
	setText(text);
	setCursor(Qt::PointingHandCursor);
	setFocusPolicy(Qt::TabFocus);
	setAccessibleName(text);
	// Follow the panel's size setting from the moment the button exists.
	for (QWidget *w = parent; w; w = w->parentWidget()) {
		if (w->property("large").isValid()) {
			large_ = w->property("large").toBool();
			break;
		}
	}
	applyHeight();
}

void ActionButton::setLarge(bool large)
{
	large_ = large;
	applyHeight();
}

void ActionButton::setKind(Kind kind)
{
	kind_ = kind;
	update();
}

void ActionButton::applyHeight()
{
	setFixedHeight(large_ ? size::touch : size::controlHeight);
	updateGeometry();
}

bool ActionButton::event(QEvent *event)
{
	if (event->type() == QEvent::DynamicPropertyChange) {
		auto *change = static_cast<QDynamicPropertyChangeEvent *>(event);
		if (change->propertyName() == "large") {
			large_ = property("large").toBool();
			applyHeight();
		}
	}
	return QAbstractButton::event(event);
}

QSize ActionButton::sizeHint() const
{
	const int padding = kind_ == Kind::Link ? 20 : 30;
	return {QFontMetrics(uiFont(13, 600)).horizontalAdvance(text()) + padding,
		large_ ? size::touch : size::controlHeight};
}

void ActionButton::enterEvent(QEnterEvent *event)
{
	hovered_ = true;
	update();
	QAbstractButton::enterEvent(event);
}

void ActionButton::leaveEvent(QEvent *event)
{
	hovered_ = false;
	update();
	QAbstractButton::leaveEvent(event);
}

void ActionButton::paintEvent(QPaintEvent *)
{
	QPainter p(this);
	p.setRenderHint(QPainter::Antialiasing);
	QRectF box = QRectF(rect()).adjusted(0.5, 0.5, -0.5, -0.5);
	if (isDown() && isEnabled()) {
		const QPointF c = box.center();
		box = QRectF(c.x() - box.width() * 0.49, c.y() - box.height() * 0.49, box.width() * 0.98,
			     box.height() * 0.98);
	}

	QColor fill = Qt::transparent;
	QColor border = Qt::transparent;
	QColor text = color::cream;
	switch (kind_) {
	case Kind::Primary:
		fill = hovered_ ? QColor(0xFB, 0xF6, 0xEA) : color::cream;
		text = color::ink;
		break;
	case Kind::Secondary:
	case Kind::Danger:
		fill = hovered_ ? QColor(0x36, 0x34, 0x30) : color::control;
		border = QColor(255, 255, 255, 26);
		text = kind_ == Kind::Danger ? color::live : color::cream;
		break;
	case Kind::Link:
		fill = hovered_ ? QColor(46, 139, 255, 30) : Qt::transparent;
		text = color::setup;
		break;
	}
	if (!isEnabled()) {
		fill = kind_ == Kind::Link ? Qt::transparent : color::graphite;
		border = kind_ == Kind::Link ? Qt::transparent : QColor(255, 255, 255, 10);
		text = color::textDisabled;
	}

	p.setPen(border.alpha() ? QPen(border, 1) : Qt::NoPen);
	p.setBrush(fill);
	p.drawRoundedRect(box, size::radius, size::radius);
	p.setPen(text);
	p.setFont(uiFont(13, 600));
	p.drawText(box, Qt::AlignCenter,
		   QFontMetrics(p.font()).elidedText(this->text(), Qt::ElideRight, int(box.width()) - 12));

	if (hasFocus()) {
		p.setBrush(Qt::NoBrush);
		p.setPen(QPen(color::setup, 2));
		p.drawRoundedRect(QRectF(rect()).adjusted(1, 1, -1, -1), size::radius, size::radius);
	}
}

// ----- GearButton -----

GearButton::GearButton(QWidget *parent) : QAbstractButton(parent)
{
	setCursor(Qt::PointingHandCursor);
	setFocusPolicy(Qt::TabFocus);
	setFixedSize(sizeHint());
}

void GearButton::enterEvent(QEnterEvent *event)
{
	hovered_ = true;
	update();
	QAbstractButton::enterEvent(event);
}

void GearButton::leaveEvent(QEvent *event)
{
	hovered_ = false;
	update();
	QAbstractButton::leaveEvent(event);
}

void GearButton::paintEvent(QPaintEvent *)
{
	QPainter p(this);
	p.setRenderHint(QPainter::Antialiasing);
	const QRectF box = QRectF(rect()).adjusted(1, 1, -1, -1);
	p.setPen(Qt::NoPen);
	p.setBrush(isDown() ? QColor(255, 255, 255, 26) : hovered_ ? QColor(255, 255, 255, 16) : Qt::transparent);
	p.drawRoundedRect(box, size::radius, size::radius);

	// A ring with eight teeth.
	const QPointF c = box.center();
	QPen pen(hovered_ ? color::cream : color::textSecondary, 2);
	pen.setCapStyle(Qt::RoundCap);
	p.setPen(pen);
	p.setBrush(Qt::NoBrush);
	p.drawEllipse(c, 5.0, 5.0);
	for (int i = 0; i < 8; ++i) {
		p.save();
		p.translate(c);
		p.rotate(i * 45.0);
		p.drawLine(QPointF(0, -7.0), QPointF(0, -9.0));
		p.restore();
	}
	if (hasFocus()) {
		p.setPen(QPen(color::setup, 2));
		p.drawRoundedRect(box, size::radius, size::radius);
	}
}

QLabel *makeChip(QWidget *parent, const QString &text, const QColor &dot)
{
	auto *chip = new QLabel(parent);
	chip->setText(QStringLiteral("<span style=\"color:%1\">●</span>&nbsp;%2").arg(hex(dot), text.toHtmlEscaped()));
	chip->setTextFormat(Qt::RichText);
	chip->setFont(uiFont(11, 600));
	chip->setStyleSheet(QStringLiteral("color: %1; background: %2; border-radius: 10px; padding: 2px 8px; "
					   "font-size: 11px; font-weight: 600;")
				    .arg(hex(color::cream), hex(color::recess)));
	return chip;
}

} // namespace laycue::ui
