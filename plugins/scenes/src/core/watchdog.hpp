// Watching one source for going blank or frozen, judged from the small pictures the
// FrameGrabber already brings. Pure logic: no libobs, no Qt.
#pragma once

#include <cstddef>
#include <cstdint>
#include <vector>

namespace laycue {

// One look at the watched source: what its pixels said.
struct WatchdogSample {
	uint64_t checksum = 0; // the pixels folded together; a frozen feed repeats the same number
	size_t lit = 0;        // how many of the pixels examined carry any light
	size_t looked = 0;     // how many pixels were examined (0 = no picture arrived at all)
};

enum class FeedState {
	Unknown, // not enough samples yet
	Healthy, // the feed moves, or at least carries light that changes
	Blank,   // no light anywhere in the window, or no pictures at all
	Frozen,  // the same picture, over and over
};

// Keeps the last few samples and turns them into a verdict. A verdict only holds once every
// remembered sample agrees, so one odd frame cannot fire the fail-over.
class FeedWatchdog {
public:
	explicit FeedWatchdog(size_t rounds = 6); // a sample every ~250ms → ~1.5s of agreement

	void reset();
	FeedState push(const WatchdogSample &sample); // remembers the sample and returns the verdict
	FeedState verdict() const;

private:
	size_t rounds_;
	std::vector<WatchdogSample> samples_;
};

} // namespace laycue
