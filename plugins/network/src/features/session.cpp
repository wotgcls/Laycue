#include "features/session.hpp"

#include "obs/stream-stats.hpp"

#include <obs-module.h>
#include <plugin-support.h>

#include <algorithm>

namespace laycue::network {

namespace {
constexpr int kChartSeconds = 180;
constexpr int kStartupSeconds = 5; // the first seconds of a connection are never judged
} // namespace

Session::Session(QObject *parent) : QObject(parent)
{
	timer_.setInterval(1000);
	connect(&timer_, &QTimer::timeout, this, &Session::tick);
	clock_.start();
	timer_.start();
}

void Session::setSettings(const Settings &settings)
{
	const bool adaptiveChanged = settings.adaptive != settings_.adaptive;
	settings_ = settings;
	if (!streaming_)
		return;
	if (adaptiveChanged) {
		if (settings_.adaptive)
			engageController(targetKbps_);
		else
			controller_.stop();
	} else if (controller_.active()) {
		AdaptiveConfig cfg;
		cfg.floorKbps = settings_.floorKbps;
		cfg.ceilingKbps = std::max(settings_.floorKbps, settings_.ceilingFromProfile
									? controller_.effectiveCeiling()
									: settings_.ceilingKbps);
		controller_.configure(cfg);
	}
}

void Session::engageController(int videoKbps)
{
	blockedByObsDynamic_ = false;
	if (!settings_.adaptive || videoKbps <= 0)
		return;
	if (obsDynamicBitrateEnabled()) {
		// Two controllers on one encoder would fight. Leave it to OBS's and say so.
		blockedByObsDynamic_ = true;
		return;
	}
	AdaptiveConfig cfg;
	const int ceiling = settings_.ceilingFromProfile ? videoKbps : settings_.ceilingKbps;
	cfg.ceilingKbps = std::max(ceiling, 600);
	cfg.floorKbps = std::min(settings_.floorKbps, cfg.ceilingKbps);
	controller_.configure(cfg);
	controller_.start(videoKbps);
}

void Session::onStarted(int videoKbps)
{
	streaming_ = true;
	secondsSinceStart_ = 0;
	targetKbps_ = videoKbps;
	monitor_.clear();
	health_ = HealthReport();
	chart_.clear();
	report_.start();
	lastReason_ = AdaptiveReason::None;
	engageController(videoKbps);
	emit streamStarted();
}

void Session::onStopped()
{
	streaming_ = false;
	const StreamReport report = report_.finish();
	controller_.stop();
	blockedByObsDynamic_ = false;
	targetKbps_ = 0;
	health_ = HealthReport();
	emit streamStopped(report);
}

void Session::tryFullBitrate()
{
	controller_.resetLimits();
}

void Session::tick()
{
	try {
		const qint64 now = clock_.elapsed();
		const double dt = std::clamp(double(now - lastMs_) / 1000.0, 0.2, 3.0);
		lastMs_ = now;

		StreamSnapshot snap;
		const bool live = readStreamSnapshot(snap);
		if (live && !streaming_) {
			onStarted(snap.videoKbps);
			lastBytes_ = snap.totalBytes;
			lastDropped_ = snap.droppedFrames;
			lastFrames_ = snap.totalFrames;
			emit updated();
			return;
		}
		if (!live && streaming_) {
			onStopped();
			emit updated();
			return;
		}
		if (!live) {
			emit updated(); // lets the panel keep its "not streaming" view fresh
			return;
		}

		++secondsSinceStart_;
		targetKbps_ = snap.videoKbps;
		const uint64_t bytes = snap.totalBytes >= lastBytes_ ? snap.totalBytes - lastBytes_ : 0;
		LiveSample s;
		s.sentKbps = double(bytes) * 8.0 / dt / 1000.0;
		s.targetKbps = double(snap.videoKbps + settings_.audioKbps);
		s.droppedFrames = std::max(0, snap.droppedFrames - lastDropped_);
		s.totalFrames = std::max(0, snap.totalFrames - lastFrames_);
		s.congestion = snap.congestion;
		lastBytes_ = snap.totalBytes;
		lastDropped_ = snap.droppedFrames;
		lastFrames_ = snap.totalFrames;

		if (secondsSinceStart_ > kStartupSeconds) {
			monitor_.add(s);
			report_.add(s);
			health_ = monitor_.report();
		}
		chart_.push_back({s.sentKbps, double(snap.videoKbps), s.droppedFrames > 0});
		while (chart_.size() > size_t(kChartSeconds))
			chart_.pop_front();

		if (controller_.active() && secondsSinceStart_ > kStartupSeconds) {
			AdaptiveSample a;
			a.congestion = s.congestion;
			a.droppedFrames = s.droppedFrames;
			a.totalFrames = s.totalFrames;
			a.sentKbps = std::max(0.0, s.sentKbps - double(settings_.audioKbps));
			const AdaptiveDecision d = controller_.update(a);
			if (d.change) {
				const int from = snap.videoKbps;
				if (setLiveVideoBitrate(d.newKbps)) {
					targetKbps_ = d.newKbps;
					lastReason_ = d.reason;
					report_.bitrateChanged(d.newKbps);
					emit bitrateChanged(from, d.newKbps, int(d.reason));
				}
			} else if (d.reason == AdaptiveReason::Rejected) {
				lastReason_ = d.reason;
				emit adaptiveStopped(int(d.reason));
			}
		}
		emit updated();
	} catch (...) {
		obs_log(LOG_ERROR, "Laycue Network: could not read the stream");
	}
}

} // namespace laycue::network
