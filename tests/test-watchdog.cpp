#include "check.hpp"

#include "core/watchdog.hpp"

#include <cstddef>
#include <cstdint>

using laycue::FeedState;
using laycue::FeedWatchdog;
using laycue::WatchdogSample;

namespace {

// A moving feed: light that never repeats itself.
WatchdogSample moving(size_t seed)
{
	return WatchdogSample{0x1000 + static_cast<uint64_t>(seed), 90, 96};
}

// The same picture over and over.
WatchdogSample frozen()
{
	return WatchdogSample{0x5432, 90, 96};
}

// Nothing lit anywhere (a dead camera draws black).
WatchdogSample blank()
{
	return WatchdogSample{0, 0, 96};
}

FeedState six(FeedWatchdog &w, const WatchdogSample &s)
{
	FeedState last = FeedState::Unknown;
	for (int i = 0; i < 6; ++i)
		last = w.push(s);
	return last;
}

} // namespace

TEST_CASE("watchdog: it says nothing until a full window of samples")
{
	FeedWatchdog w;
	CHECK(w.push(moving(1)) == FeedState::Unknown);
	CHECK(w.push(moving(2)) == FeedState::Unknown);
	CHECK(w.push(moving(3)) == FeedState::Unknown);
	CHECK(w.push(moving(4)) == FeedState::Unknown);
	CHECK(w.push(moving(5)) == FeedState::Unknown);
	CHECK(w.push(moving(6)) == FeedState::Healthy);
}

TEST_CASE("watchdog: a feed that moves, and a feed that sits still, are told apart")
{
	FeedWatchdog w;
	CHECK_EQ(six(w, moving(1)), FeedState::Healthy);
	FeedWatchdog still;
	CHECK_EQ(six(still, frozen()), FeedState::Frozen);
}

TEST_CASE("watchdog: darkness or silence reads as blank")
{
	FeedWatchdog dark;
	CHECK_EQ(six(dark, blank()), FeedState::Blank);
	FeedWatchdog silent;
	CHECK_EQ(six(silent, WatchdogSample{0, 0, 0}), FeedState::Blank); // no picture at all
}

TEST_CASE("watchdog: one odd sample cannot condemn a healthy feed")
{
	FeedWatchdog w;
	six(w, moving(1));
	CHECK(w.push(WatchdogSample{0, 0, 0}) != FeedState::Blank); // one dark frame among movers
	CHECK(w.push(moving(7)) == FeedState::Healthy);
}

TEST_CASE("watchdog: a freeze needs the same picture every time")
{
	FeedWatchdog w;
	CHECK_EQ(six(w, frozen()), FeedState::Frozen);
	CHECK(w.push(moving(2)) == FeedState::Healthy); // and a return to movement recovers
}

TEST_CASE("watchdog: after a fire, a reset re-arms it")
{
	FeedWatchdog w;
	CHECK_EQ(six(w, blank()), FeedState::Blank);
	w.reset();
	CHECK(w.verdict() == FeedState::Unknown);
	CHECK_EQ(six(w, moving(1)), FeedState::Healthy);
}

TEST_CASE("watchdog: darkness among stillness is blank, not frozen")
{
	FeedWatchdog w;
	CHECK_EQ(six(w, blank()), FeedState::Blank); // identical black frames never read as a freeze
}
