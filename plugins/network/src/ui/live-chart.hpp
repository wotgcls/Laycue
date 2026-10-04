// The live chart: what the stream sent each second over the last three minutes, against the bitrate it is
// set to, with a marker where frames were dropped. One series, so no legend box; a one-line key explains the
// marks, and the numbers are also in the text next to it (never colour alone).
#pragma once

#include "features/session.hpp"

#include <QWidget>

#include <deque>

namespace laycue::network::ui {

class LiveChart : public QWidget {
	Q_OBJECT

public:
	explicit LiveChart(QWidget *parent = nullptr);

	void setData(const std::deque<ChartPoint> &points);
	QSize sizeHint() const override { return {420, 150}; }
	QSize minimumSizeHint() const override { return {240, 120}; }

protected:
	void paintEvent(QPaintEvent *event) override;

private:
	std::deque<ChartPoint> points_;
};

} // namespace laycue::network::ui
