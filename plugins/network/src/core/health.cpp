#include "core/health.hpp"

#include <algorithm>

namespace laycue::network {

void HealthMonitor::add(const LiveSample &sample)
{
	samples_.push_back(sample);
	while (samples_.size() > window_)
		samples_.pop_front();
}

HealthReport HealthMonitor::report() const
{
	HealthReport r;
	if (samples_.size() < 3)
		return r; // Idle: not enough to judge
	long dropped = 0, frames = 0;
	double sent = 0.0, target = 0.0, congestion = 0.0;
	int congestionSamples = 0;
	for (const LiveSample &s : samples_) {
		dropped += s.droppedFrames;
		frames += s.totalFrames;
		sent += s.sentKbps;
		target += s.targetKbps;
		if (s.congestion >= 0.0) {
			congestion += s.congestion;
			++congestionSamples;
		}
	}
	const double n = double(samples_.size());
	r.averageKbps = sent / n;
	r.dropPercent = frames > 0 ? 100.0 * double(dropped) / double(frames) : 0.0;
	r.averageCongestion = congestionSamples > 0 ? congestion / congestionSamples : -1.0;

	int penalty = 0;
	if (r.dropPercent >= 5.0) {
		penalty += 60;
		r.reasons.push_back(HealthReason::Drops);
	} else if (r.dropPercent >= 1.0) {
		penalty += 35;
		r.reasons.push_back(HealthReason::Drops);
	} else if (dropped > 0) {
		penalty += 12;
		r.reasons.push_back(HealthReason::Drops);
	}
	if (r.averageCongestion >= 0.3) {
		penalty += 30;
		r.reasons.push_back(HealthReason::Congestion);
	} else if (r.averageCongestion >= 0.1) {
		penalty += 15;
		r.reasons.push_back(HealthReason::Congestion);
	}
	if (target > 0.0 && sent / target < 0.8) {
		penalty += 20;
		r.reasons.push_back(HealthReason::LowSendRate);
	}
	r.score = std::clamp(100 - penalty, 0, 100);
	r.state = r.score >= 85 ? HealthState::Good : r.score >= 60 ? HealthState::Watch : HealthState::Poor;
	return r;
}

} // namespace laycue::network
