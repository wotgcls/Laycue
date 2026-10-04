// The live side of the Network plugin: once a second it reads how the stream is doing, keeps the health
// score and the report, and (if the operator turned auto-adjust on) lets the controller change the bitrate.
// UI thread only.
#pragma once

#include "core/adaptive.hpp"
#include "core/health.hpp"
#include "core/network-settings.hpp"
#include "core/report.hpp"

#include <QElapsedTimer>
#include <QObject>
#include <QTimer>

#include <deque>

namespace laycue::network {

// One point of the live chart.
struct ChartPoint {
	double sentKbps = 0.0;
	double targetKbps = 0.0; // video bitrate setting
	bool dropped = false;    // frames were dropped in this second
};

class Session : public QObject {
	Q_OBJECT

public:
	explicit Session(QObject *parent = nullptr);

	// The operator changed settings (auto-adjust on or off, floor, ceiling).
	void setSettings(const Settings &settings);

	bool streaming() const { return streaming_; }
	int targetKbps() const { return targetKbps_; }
	int ceilingKbps() const { return controller_.active() ? controller_.effectiveCeiling() : 0; }
	bool adaptiveActive() const { return controller_.active(); }
	bool adaptiveFrozen() const { return controller_.frozen(); }
	bool blockedByObsDynamic() const { return blockedByObsDynamic_; }
	const HealthReport &health() const { return health_; }
	const std::deque<ChartPoint> &chart() const { return chart_; }
	AdaptiveReason lastReason() const { return lastReason_; }

	// Forget the levels the controller learned were too high ("try full bitrate").
	void tryFullBitrate();

signals:
	void updated();
	void streamStarted();
	void streamStopped(const laycue::network::StreamReport &report);
	void bitrateChanged(int fromKbps, int toKbps, int reason); // reason is an AdaptiveReason
	void adaptiveStopped(int reason);

private:
	void tick();
	void onStarted(int videoKbps);
	void onStopped();
	void engageController(int videoKbps);

	Settings settings_;
	QTimer timer_;
	QElapsedTimer clock_;
	qint64 lastMs_ = 0;

	bool streaming_ = false;
	uint64_t lastBytes_ = 0;
	int lastDropped_ = 0;
	int lastFrames_ = 0;
	int targetKbps_ = 0;
	int secondsSinceStart_ = 0;
	bool blockedByObsDynamic_ = false;

	HealthMonitor monitor_;
	HealthReport health_;
	ReportBuilder report_;
	AdaptiveController controller_;
	AdaptiveReason lastReason_ = AdaptiveReason::None;
	std::deque<ChartPoint> chart_;
};

} // namespace laycue::network
