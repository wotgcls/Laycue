#include "ui/live-chart.hpp"

#include "ui/text.hpp"
#include "ui/tokens.hpp"

#include <QLocale>
#include <QPainter>
#include <QPainterPath>

#include <algorithm>
#include <cmath>

namespace laycue::network::ui {

using namespace laycue::ui;

namespace {

QString kbps(double v)
{
	return QLocale().toString(int(std::lround(v)));
}

// A round number above the data, so the top of the chart is easy to read.
double niceMax(double v)
{
	if (v <= 1000.0)
		return 1000.0;
	const double step = v <= 4000.0 ? 1000.0 : v <= 12000.0 ? 2000.0 : 5000.0;
	return std::ceil(v / step) * step;
}

} // namespace

LiveChart::LiveChart(QWidget *parent) : QWidget(parent)
{
	setMinimumHeight(84);
	setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
	setFixedHeight(96);
}

void LiveChart::setData(const std::deque<ChartPoint> &points)
{
	points_ = points;
	if (points_.empty()) {
		setAccessibleDescription(lt("Laycue.Network.ChartEmpty"));
	} else {
		const ChartPoint &last = points_.back();
		int drops = 0;
		for (const ChartPoint &p : points_)
			drops += p.dropped ? 1 : 0;
		setAccessibleDescription(lt("Laycue.Network.ChartSummary")
						 .arg(kbps(last.sentKbps))
						 .arg(kbps(last.targetKbps))
						 .arg(drops));
	}
	update();
}

void LiveChart::paintEvent(QPaintEvent *)
{
	QPainter p(this);
	p.setRenderHint(QPainter::Antialiasing, true);

	const QRectF plot(36, 8, width() - 36 - 78, height() - 8 - 22);
	const QFont labelFont = uiFont(10, 500);
	p.setFont(labelFont);

	double top = 1000.0;
	for (const ChartPoint &pt : points_)
		top = std::max({top, pt.sentKbps * 1.1, pt.targetKbps * 1.1});
	top = niceMax(top);

	// A recessive grid: three faint lines with their values.
	for (int i = 0; i <= 2; ++i) {
		const double y = plot.bottom() - plot.height() * i / 2.0;
		p.setPen(QPen(QColor(255, 255, 255, i == 0 ? 36 : 16), 1));
		p.drawLine(QPointF(plot.left(), y), QPointF(plot.right(), y));
		p.setPen(color::textTertiary);
		p.drawText(QRectF(0, y - 8, plot.left() - 6, 16), Qt::AlignRight | Qt::AlignVCenter,
			   kbps(top * i / 2.0));
	}
	p.setPen(color::textTertiary);
	p.drawText(QRectF(plot.left(), plot.bottom() + 4, 60, 14), Qt::AlignLeft | Qt::AlignVCenter,
		   lt("Laycue.Network.ChartAgo"));
	p.drawText(QRectF(plot.right() - 60, plot.bottom() + 4, 60, 14), Qt::AlignRight | Qt::AlignVCenter,
		   lt("Laycue.Network.ChartNow"));

	if (points_.size() < 2) {
		p.setPen(color::textSecondary);
		p.setFont(uiFont(12, 500));
		p.drawText(plot, Qt::AlignCenter, lt("Laycue.Network.ChartWaiting"));
		return;
	}

	constexpr int kWindow = 180;
	const double stepX = plot.width() / double(kWindow - 1);
	const auto xAt = [&](size_t i) {
		return plot.right() - stepX * double(points_.size() - 1 - i);
	};
	const auto yAt = [&](double v) {
		return plot.bottom() - plot.height() * std::clamp(v / top, 0.0, 1.0);
	};

	// The bitrate it is set to: a quiet stepped line.
	QPainterPath target;
	for (size_t i = 0; i < points_.size(); ++i) {
		const QPointF pt(xAt(i), yAt(points_[i].targetKbps));
		if (i == 0)
			target.moveTo(pt);
		else {
			target.lineTo(QPointF(pt.x(), yAt(points_[i - 1].targetKbps)));
			target.lineTo(pt);
		}
	}
	QPen targetPen(color::textTertiary, 1.5, Qt::DashLine);
	p.setPen(targetPen);
	p.setBrush(Qt::NoBrush);
	p.drawPath(target);

	// What was really sent: the one loud line.
	QPainterPath sent;
	for (size_t i = 0; i < points_.size(); ++i) {
		const QPointF pt(xAt(i), yAt(points_[i].sentKbps));
		i == 0 ? sent.moveTo(pt) : sent.lineTo(pt);
	}
	p.setPen(QPen(color::cream, 2.0, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
	p.drawPath(sent);

	// Dropped frames: an amber triangle on the baseline, with a ring of chart colour so it stays visible on the line.
	for (size_t i = 0; i < points_.size(); ++i) {
		if (!points_[i].dropped)
			continue;
		const double x = xAt(i), y = plot.bottom() - 2;
		QPolygonF tri;
		tri << QPointF(x, y - 9) << QPointF(x - 5, y) << QPointF(x + 5, y);
		p.setPen(QPen(color::stage, 1.5));
		p.setBrush(color::overlay);
		p.drawPolygon(tri);
	}

	// The latest value, labelled directly at the end of the line.
	const ChartPoint &last = points_.back();
	p.setPen(color::cream);
	p.setFont(monoFont(11));
	p.drawText(QRectF(plot.right() + 6, yAt(last.sentKbps) - 9, 72, 18), Qt::AlignLeft | Qt::AlignVCenter,
		   kbps(last.sentKbps));
}

} // namespace laycue::network::ui
