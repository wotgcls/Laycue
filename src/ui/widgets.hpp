// Small custom-painted widgets for the Laycue panel, drawn to the brand tokens.
#pragma once

#include <QAbstractButton>
#include <QColor>
#include <QImage>
#include <QString>
#include <QWidget>

namespace laycue::ui {

// A 16:9 picture (Live or Up next) with a coloured ring and a small tally pill.
// Shows a quiet placeholder message when there is no picture.
class PictureView : public QWidget {
	Q_OBJECT

public:
	explicit PictureView(QWidget *parent = nullptr);

	void setImage(const QImage &image);
	void clearImage();
	void setRing(const QColor &color, qreal width);            // width 0 = no ring
	void setPill(const QString &text, const QColor &dotColor); // empty text = no pill
	void setPlaceholder(const QString &text);

	bool hasHeightForWidth() const override { return true; }
	int heightForWidth(int width) const override { return width * 9 / 16; }
	QSize sizeHint() const override { return {320, 180}; }

protected:
	void paintEvent(QPaintEvent *event) override;

private:
	QImage image_;
	QColor ringColor_;
	qreal ringWidth_ = 0;
	QString pillText_;
	QColor pillDot_;
	QString placeholder_;
};

// A tappable card for one shot: still picture, name and Live / Up next state.
class ShotCard : public QAbstractButton {
	Q_OBJECT

public:
	enum class State { Idle, Next, Live };

	explicit ShotCard(QWidget *parent = nullptr);

	void setImage(const QImage &image);
	void setName(const QString &name);
	void setState(State state);
	State state() const { return state_; }

	bool hasHeightForWidth() const override { return true; }
	int heightForWidth(int width) const override;
	QSize sizeHint() const override { return {160, heightForWidth(160)}; }

protected:
	void paintEvent(QPaintEvent *event) override;
	bool event(QEvent *event) override;
	void keyPressEvent(QKeyEvent *event) override;
	void enterEvent(QEnterEvent *event) override;
	void leaveEvent(QEvent *event) override;

private:
	QImage image_;
	QString name_;
	State state_ = State::Idle;
	bool hovered_ = false;
};

// A choice button drawn by Laycue (a camera inside a pushback): when checked it is outlined in
// Tally Red, because that camera is on air.
class FeedButton : public QAbstractButton {
	Q_OBJECT

public:
	explicit FeedButton(const QString &text, QWidget *parent = nullptr);

	QSize sizeHint() const override;

protected:
	void paintEvent(QPaintEvent *event) override;
	void enterEvent(QEnterEvent *event) override;
	void leaveEvent(QEvent *event) override;
	void nextCheckState() override {} // never toggles itself: the panel says which one is showing

private:
	bool hovered_ = false;
};

} // namespace laycue::ui
