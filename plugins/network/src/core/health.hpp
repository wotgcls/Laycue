// A live "how is the stream doing" score from the last half minute of numbers OBS reports. Pure logic.
#pragma once

#include <deque>
#include <vector>

namespace laycue::network {

// One second of a live stream, as OBS reports it.
struct LiveSample {
	double sentKbps = 0.0;    // what was actually sent
	double targetKbps = 0.0;  // what the encoder is set to
	int droppedFrames = 0;    // dropped during this second
	int totalFrames = 0;      // frames output during this second
	double congestion = -1.0; // 0..1 as OBS reports it, or negative if this output cannot say
};

enum class HealthState { Idle, Good, Watch, Poor };
enum class HealthReason { Drops, Congestion, LowSendRate };

struct HealthReport {
	HealthState state = HealthState::Idle;
	int score = 100; // 0..100
	std::vector<HealthReason> reasons;
	double dropPercent = 0.0; // over the window
	double averageKbps = 0.0; // over the window
	double averageCongestion = -1.0;
};

class HealthMonitor {
public:
	explicit HealthMonitor(size_t windowSeconds = 30) : window_(windowSeconds) {}

	void add(const LiveSample &sample);
	void clear() { samples_.clear(); }
	size_t size() const { return samples_.size(); }
	HealthReport report() const;

private:
	size_t window_;
	std::deque<LiveSample> samples_;
};

} // namespace laycue::network
