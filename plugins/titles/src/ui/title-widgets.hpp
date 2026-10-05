// The two pictures of the Titles panel: the big preview of the title being edited, and the small
// template cards. Both draw with the same renderer as the title source, so what you see here is
// exactly what goes on air.
#pragma once

#include "core/ticker.hpp"
#include "core/title-clock.hpp"
#include "render/title-renderer.hpp"

#include <QAbstractButton>
#include <QElapsedTimer>
#include <QImage>
#include <QTimer>
#include <QWidget>

namespace laycue::titles::ui {

// A 16:9 stage with the title on it. Idle, it shows the finished title; after play() it replays the
// arrival the same way the source does, and a red ring says it is on air.
class TitlePreview : public QWidget {
	Q_OBJECT

public:
	explicit TitlePreview(QWidget *parent = nullptr);

	void setTitle(const TitleTemplate &tpl, const TitleText &text, bool useBrand, const Rgba &brand);
	void setPlacement(int align, int percent); // where it sits and how big (see Placement)
	void setOnAir(bool onAir);
	// Starts the arrival (or the exit) so the preview moves like the real title.
	void play();
	void stop();
	bool onAir() const { return onAir_; }

	bool hasHeightForWidth() const override { return true; }
	int heightForWidth(int width) const override { return width * 9 / 16; }
	QSize sizeHint() const override { return {440, 248}; }
	QSize minimumSizeHint() const override { return {240, 135}; }

protected:
	void paintEvent(QPaintEvent *event) override;

private:
	void tick();
	void configureClock();

	TitleTemplate tpl_;
	TitleText text_;
	bool useBrand_ = false;
	Rgba brand_;
	int align_ = 0;
	int percent_ = 100;
	bool onAir_ = false;
	bool playing_ = false; // the arrival or exit is being drawn
	TitleClock clock_;
	TickerState ticker_;
	QTimer timer_;
	QElapsedTimer elapsed_;
	qint64 lastMs_ = 0;
};

// One template in the gallery: its picture, its name, and a ring when it is the chosen one.
class TemplateCard : public QAbstractButton {
	Q_OBJECT

public:
	explicit TemplateCard(QWidget *parent = nullptr);

	void setImage(const QImage &image);
	void setName(const QString &name);
	void setCustom(bool custom);
	void setSelectedCard(bool selected);

	bool hasHeightForWidth() const override { return true; }
	int heightForWidth(int width) const override;
	QSize sizeHint() const override { return {150, heightForWidth(150)}; }

protected:
	void paintEvent(QPaintEvent *event) override;
	void enterEvent(QEnterEvent *event) override;
	void leaveEvent(QEvent *event) override;
	void keyPressEvent(QKeyEvent *event) override;

private:
	QImage image_;
	QString name_;
	bool custom_ = false;
	bool selected_ = false;
	bool hovered_ = false;
};

// The stage background the preview and the cards draw titles over: dark, with a soft light, so a title
// is judged on something that is not flat black. Returns an image of the given size.
QImage stageBackground(int width, int height);

} // namespace laycue::titles::ui
