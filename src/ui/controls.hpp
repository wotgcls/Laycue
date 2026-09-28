// The small controls the panel is built from, drawn to the Laycue tokens: raised surfaces with
// depth, a segmented control, a switch and a dismissible tip card.
#pragma once

#include <QAbstractButton>
#include <QFrame>
#include <QStringList>
#include <QVariantAnimation>
#include <QWidget>

class QLabel;
class QHBoxLayout;

namespace laycue::ui {

// A raised panel: rounded, with a faint top highlight and a soft shadow beneath it, so it reads as
// sitting above the panel background. Level 1 = a list or form; level 2 = the stage (Live, Up next).
// Children go in its layout; margins already leave room for the shadow.
class Surface : public QFrame {
	Q_OBJECT

public:
	explicit Surface(int level = 1, QWidget *parent = nullptr);

	// Space inside the surface, on top of the shadow margin.
	void setPadding(int left, int top, int right, int bottom);

protected:
	void paintEvent(QPaintEvent *event) override;

private:
	int level_;
};

// A row of mutually exclusive choices ("Shots · Pushbacks · Overlays", "Normal | Studio").
class Segmented : public QWidget {
	Q_OBJECT

public:
	explicit Segmented(QWidget *parent = nullptr);

	void setItems(const QStringList &labels);
	void setCurrent(int index); // does not emit changed()
	int current() const { return current_; }
	void setSegmentEnabled(int index, bool enabled);

	QSize sizeHint() const override;
	QSize minimumSizeHint() const override;

signals:
	void changed(int index);

protected:
	void paintEvent(QPaintEvent *event) override;
	void mousePressEvent(QMouseEvent *event) override;
	void mouseReleaseEvent(QMouseEvent *event) override;
	void keyPressEvent(QKeyEvent *event) override;
	void leaveEvent(QEvent *event) override;
	void mouseMoveEvent(QMouseEvent *event) override;

private:
	int controlHeight() const;
	int segmentAt(const QPoint &pos) const;
	QRectF segmentRect(int index) const;

	QStringList labels_;
	QList<bool> enabled_;
	int current_ = 0;
	int pressed_ = -1;
	int hovered_ = -1;
};

// An on/off switch.
class Switch : public QAbstractButton {
	Q_OBJECT
	Q_PROPERTY(qreal knob READ knob WRITE setKnob)

public:
	explicit Switch(QWidget *parent = nullptr);

	// Sets the state without animating or emitting (for filling in a page).
	void setOn(bool on);

	QSize sizeHint() const override { return {42, 26}; }
	qreal knob() const { return knob_; }
	void setKnob(qreal value);

protected:
	void paintEvent(QPaintEvent *event) override;
	void nextCheckState() override;

private:
	qreal knob_ = 0;
	QVariantAnimation animation_;
};

// A quiet card with a short tip and a "Got it" button. Emits dismissed() when closed.
class TipCard : public Surface {
	Q_OBJECT

public:
	TipCard(const QString &title, const QString &body, QWidget *parent = nullptr);

signals:
	void dismissed();
};

// A small coloured pill ("Scripture", "Live"...) used in lists.
QLabel *makeChip(QWidget *parent, const QString &text, const QColor &dot);

} // namespace laycue::ui
