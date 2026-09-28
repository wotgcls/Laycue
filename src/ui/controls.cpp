#include "ui/controls.hpp"

#include "ui/tokens.hpp"

#include <obs-module.h>

#include <algorithm>

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
	const qreal each = track.width() / std::max<int>(1, labels_.size());
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
	auto *copy = new QLabel(body, this);
	copy->setFont(uiFont(12, 400));
	copy->setStyleSheet(labelStyle(color::textSecondary, copy->font()));
	copy->setWordWrap(true);
	text->addWidget(heading);
	text->addWidget(copy);
	root->addLayout(text, 1);

	auto *close = new QPushButton(QString::fromUtf8(obs_module_text("Laycue.GotIt")), this);
	close->setObjectName("laycueLink");
	close->setCursor(Qt::PointingHandCursor);
	connect(close, &QPushButton::clicked, this, &TipCard::dismissed);
	root->addWidget(close, 0, Qt::AlignTop);
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
