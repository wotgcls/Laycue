#include "core/watchdog.hpp"

namespace laycue {

FeedWatchdog::FeedWatchdog(size_t rounds) : rounds_(rounds > 0 ? rounds : 1) {}

void FeedWatchdog::reset()
{
	samples_.clear();
}

FeedState FeedWatchdog::push(const WatchdogSample &sample)
{
	if (samples_.size() >= rounds_)
		samples_.erase(samples_.begin());
	samples_.push_back(sample);
	return verdict();
}

FeedState FeedWatchdog::verdict() const
{
	if (samples_.size() < rounds_)
		return FeedState::Unknown;
	bool blank = true;
	bool frozen = true;
	bool lit = false;
	const WatchdogSample &first = samples_.front();
	for (const WatchdogSample &s : samples_) {
		if (s.lit > 0) {
			blank = false;
			lit = true;
		}
		if (s.checksum != first.checksum)
			frozen = false;
	}
	if (blank)
		return FeedState::Blank; // everything dark, or nothing arriving at all
	if (frozen && lit)
		return FeedState::Frozen;
	return FeedState::Healthy;
}

} // namespace laycue
