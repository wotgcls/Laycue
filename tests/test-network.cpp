#include "check.hpp"

#include "core/adaptive.hpp"
#include "core/health.hpp"
#include "core/network-settings.hpp"
#include "core/recommend.hpp"
#include "core/report.hpp"
#include "core/speed-analysis.hpp"
#include "core/stats.hpp"

#include <algorithm>
#include <cmath>
#include <functional>

using namespace laycue::network;

TEST_CASE("network stats: percentiles, median, mean and spread")
{
	const std::vector<double> v = {1, 2, 3, 4, 5};
	CHECK(median(v) == 3.0);
	CHECK(percentile(v, 0) == 1.0);
	CHECK(percentile(v, 100) == 5.0);
	CHECK(percentile(v, 25) == 2.0);
	CHECK(mean(v) == 3.0);
	CHECK(stddev(v) > 1.41 && stddev(v) < 1.42);
	CHECK(median({}) == 0.0);
	CHECK(stddev({5}) == 0.0);
}

TEST_CASE("network speed analysis: ignores the warm-up and reports the steady speed")
{
	std::vector<UploadSlice> slices;
	slices.push_back({1.0, 100000}); // warm-up, slow
	slices.push_back({1.0, 400000}); // still inside the warm-up window (starts at 1.0 < 1.5)
	for (int i = 0; i < 8; ++i)
		slices.push_back({1.0, 1250000.0}); // 10 Mbps
	const UploadMeasure m = analyseUpload(slices, {20, 22, 21, 19}, 4, 0);
	CHECK(m.valid);
	CHECK(m.medianMbps > 9.9 && m.medianMbps < 10.1);
	CHECK(m.lowMbps > 9.9 && m.lowMbps < 10.1);
	CHECK(m.jitter < 0.01);
	CHECK(m.rttMs > 20 && m.rttMs < 21);
	CHECK(m.failedShare == 0.0);
}

TEST_CASE("network speed analysis: too little data is not a measurement")
{
	CHECK(!analyseUpload({{1.0, 1000}, {1.0, 1000}}, {}, 0, 0).valid);
	CHECK(!analyseUpload({}, {}, 0, 0).valid);
	// Slices that are too short to trust are dropped.
	std::vector<UploadSlice> tiny(10, UploadSlice{0.1, 1000});
	CHECK(!analyseUpload(tiny, {}, 0, 0, 0.0).valid);
}

namespace {
UploadMeasure steady(double mbps)
{
	UploadMeasure m;
	m.valid = true;
	m.medianMbps = mbps;
	m.lowMbps = mbps * 0.95;
	m.jitter = 0.05;
	return m;
}
} // namespace

TEST_CASE("network recommend: a fast connection is held to the platform's limit")
{
	const Recommendation r = recommend(steady(40.0), platformById("twitch"));
	CHECK(r.valid);
	CHECK_EQ(r.videoKbps, 6000);
	CHECK(r.limitedByPlatform);
	CHECK_EQ(r.height, 1080);
	CHECK_EQ(r.fps, 60);
	CHECK(r.rating == Rating::Excellent);
}

TEST_CASE("network recommend: leaves room for the audio and the dips (about 75% of what you can count on)")
{
	const Recommendation r = recommend(steady(8.0), platformById("youtube")); // 8 Mbps up
	CHECK(r.videoKbps <= 5600);                                               // 75% of 8 Mbps = 6000, minus audio
	CHECK(r.videoKbps >= 4500);
	CHECK(r.videoKbps + r.audioKbps < 8000 * 0.8);
	CHECK(r.rating == Rating::Good || r.rating == Rating::Tight);
}

TEST_CASE("network recommend: dips matter more than the average")
{
	UploadMeasure m = steady(10.0);
	m.lowMbps = 3.0; // it dips hard
	const Recommendation r = recommend(m, platformById("youtube"));
	CHECK(r.videoKbps <= 2500);
}

TEST_CASE("network recommend: a slow connection is flagged, and an unsteady one gets a lighter picture")
{
	const Recommendation slow = recommend(steady(1.0), platformById("twitch"));
	CHECK(slow.tooSlow);
	CHECK(slow.rating == Rating::Poor);
	CHECK(slow.videoKbps == bitrateLadder().front());

	UploadMeasure shaky = steady(9.0);
	shaky.jitter = 0.6;
	const Recommendation a = recommend(steady(9.0), platformById("youtube"));
	const Recommendation b = recommend(shaky, platformById("youtube"));
	CHECK(b.unstable);
	CHECK(b.videoKbps < a.videoKbps);
	CHECK((b.height < a.height || b.fps < a.fps));
	CHECK(b.rating != Rating::Excellent);
}

TEST_CASE("network recommend: no measurement, no recommendation")
{
	CHECK(!recommend(UploadMeasure(), platformById("twitch")).valid);
	CHECK_EQ(platformById("nope").id, platforms().front().id);
	CHECK_EQ(customPlatform(99999).maxVideoKbps, 50000);
}

TEST_CASE("network health: a clean stream is good, drops and congestion are named")
{
	HealthMonitor h;
	for (int i = 0; i < 10; ++i)
		h.add({4500, 4500, 0, 30, 0.0});
	HealthReport r = h.report();
	CHECK(r.state == HealthState::Good);
	CHECK_EQ(r.score, 100);
	CHECK(r.reasons.empty());

	HealthMonitor rough;
	for (int i = 0; i < 10; ++i)
		rough.add({2000, 4500, 3, 30, 0.5});
	r = rough.report();
	CHECK(r.state == HealthState::Poor);
	CHECK(r.dropPercent > 9.0);
	CHECK(r.reasons.size() == 3);

	HealthMonitor few;
	few.add({4500, 4500, 0, 30, 0});
	CHECK(few.report().state == HealthState::Idle); // too little to judge
}

TEST_CASE("network health: only the last half minute counts")
{
	HealthMonitor h(10);
	for (int i = 0; i < 10; ++i)
		h.add({1000, 4500, 5, 30, 0.8});
	for (int i = 0; i < 10; ++i)
		h.add({4500, 4500, 0, 30, 0.0});
	CHECK(h.report().state == HealthState::Good);
	CHECK_EQ(int(h.size()), 10);
}

TEST_CASE("network report: totals, worst minute and verdict")
{
	ReportBuilder b;
	b.start();
	for (int i = 0; i < 120; ++i) {
		const bool bad = i >= 60 && i < 120; // the second minute is rough
		b.add({4000.0, 4500.0, bad ? 6 : 0, 30, 0.0});
	}
	b.bitrateChanged(3600);
	const StreamReport r = b.finish();
	CHECK_EQ(int(r.seconds), 120);
	CHECK(r.averageKbps > 3999 && r.averageKbps < 4001);
	CHECK(r.dropPercent > 9.0 && r.dropPercent < 11.0);
	CHECK_EQ(r.worstMinuteStart, 60L);
	CHECK(r.worstMinuteDropPercent > 19.0);
	CHECK_EQ(r.bitrateChanges, 1);
	CHECK_EQ(r.lowestKbps, 3600);
	CHECK(r.verdict == Verdict::Rough);

	ReportBuilder calm;
	calm.start();
	for (int i = 0; i < 90; ++i)
		calm.add({4500, 4500, 0, 30, 0});
	CHECK(calm.finish().verdict == Verdict::Smooth);
}

// ---- The controller, against simulated bad networks ----------------------------------------------------

namespace {

struct Run {
	std::vector<int> bitrate; // the encoder's setting each second
	int changes = 0;
	int dropSeconds = 0;
	bool rejected = false;
};

// A connection whose capacity (kbps) changes over time. The encoder sends what it is set to, unless the
// connection can't carry it: then the rest is dropped and the output reports congestion. With
// `encoderIgnores`, the encoder never obeys a change, as some hardware encoders do not.
Run simulate(AdaptiveController &ctrl, int seconds, const std::function<double(int)> &capacity,
	     bool encoderIgnores = false, int startKbps = 6000)
{
	Run run;
	ctrl.start(startKbps);
	int encoder = startKbps;
	for (int t = 0; t < seconds; ++t) {
		const double cap = capacity(t);
		AdaptiveSample s;
		s.totalFrames = 30;
		s.sentKbps = std::min<double>(encoder, cap);
		if (encoder > cap) {
			const double lost = 1.0 - cap / encoder;
			s.droppedFrames = int(std::ceil(30 * lost));
			s.congestion = std::min(1.0, lost * 3.0);
			++run.dropSeconds;
		} else {
			s.congestion = 0.0;
		}
		const AdaptiveDecision d = ctrl.update(s);
		if (d.change) {
			++run.changes;
			if (!encoderIgnores)
				encoder = d.newKbps;
		}
		if (d.reason == AdaptiveReason::Rejected)
			run.rejected = true;
		run.bitrate.push_back(encoder);
	}
	return run;
}

AdaptiveController makeController(int floorKbps = 1000, int ceilingKbps = 6000)
{
	AdaptiveController c;
	AdaptiveConfig cfg;
	cfg.floorKbps = floorKbps;
	cfg.ceilingKbps = ceilingKbps;
	c.configure(cfg);
	return c;
}

} // namespace

TEST_CASE("network adaptive: finds a rate the connection can carry and stays there")
{
	AdaptiveController c = makeController();
	const Run run = simulate(c, 600, [](int) { return 3000.0; });
	CHECK(run.bitrate.back() <= 3000);
	CHECK(run.bitrate.back() >= 1000);
	// After the first minute it should be calm: no more than a couple of failed raises in the next nine.
	int lateDrops = 0;
	for (int t = 60; t < 600; ++t)
		lateDrops += (run.bitrate[t] > 3000) ? 1 : 0;
	CHECK(lateDrops <= 30);  // it may probe upward, but only briefly
	CHECK(run.changes < 22); // no endless see-saw
}

TEST_CASE("network adaptive: remembers where it failed instead of retrying it every 15 seconds")
{
	AdaptiveController c = makeController();
	const Run run = simulate(c, 900, [](int) { return 3000.0; });
	// Count how many separate times it went above capacity after settling.
	int overshoots = 0;
	bool over = false;
	for (int t = 90; t < 900; ++t) {
		const bool now = run.bitrate[t] > 3000;
		if (now && !over)
			++overshoots;
		over = now;
	}
	CHECK(overshoots <= 4);
}

TEST_CASE("network adaptive: comes back to the ceiling when the connection recovers")
{
	AdaptiveController c = makeController();
	const Run run = simulate(c, 900, [](int t) { return t < 200 ? 2000.0 : 20000.0; });
	CHECK(run.bitrate[199] <= 2000);
	CHECK_EQ(run.bitrate.back(), 6000);
	// A failed level is remembered for five minutes, so full speed returns within seven.
	int reached = -1;
	for (int t = 200; t < 900; ++t) {
		if (run.bitrate[t] == 6000) {
			reached = t;
			break;
		}
	}
	CHECK(reached > 0 && reached - 200 <= 420);
}

TEST_CASE("network adaptive: \"try full bitrate\" forgets the failed levels and climbs on a short ramp")
{
	AdaptiveController c = makeController();
	const Run before = simulate(c, 200, [](int) { return 2000.0; });
	CHECK(before.bitrate.back() <= 2000);
	c.resetLimits();
	int encoder = c.current();
	int reached = -1;
	for (int t = 0; t < 400 && reached < 0; ++t) {
		AdaptiveSample ok;
		ok.totalFrames = 30;
		ok.sentKbps = encoder;
		ok.congestion = 0.0;
		const AdaptiveDecision d = c.update(ok);
		if (d.change)
			encoder = d.newKbps;
		if (encoder == 6000)
			reached = t;
	}
	CHECK(reached > 0 && reached <= 150);
}

TEST_CASE("network adaptive: never goes below the floor or above the ceiling")
{
	AdaptiveController c = makeController(1200, 4000);
	const Run run = simulate(c, 600, [](int) { return 300.0; }, false, 4000);
	for (int b : run.bitrate)
		CHECK(b >= 1200 && b <= 4000);
	CHECK_EQ(run.bitrate.back(), 1200);

	AdaptiveController d = makeController(1000, 3000);
	const Run fast = simulate(d, 300, [](int) { return 50000.0; }, false, 2000);
	for (int b : fast.bitrate)
		CHECK(b <= 3000);
	CHECK_EQ(fast.bitrate.back(), 3000);
}

TEST_CASE("network adaptive: stops and says so if the encoder ignores a lowering")
{
	AdaptiveController c = makeController();
	const Run run = simulate(c, 120, [](int) { return 3000.0; }, true);
	CHECK(run.rejected);
	CHECK(c.frozen());
	// After that it stays out of the way: no more changes.
	const int changesAtFreeze = run.changes;
	AdaptiveSample bad;
	bad.droppedFrames = 20;
	bad.totalFrames = 30;
	for (int i = 0; i < 30; ++i)
		CHECK(!c.update(bad).change);
	CHECK(changesAtFreeze >= 1);
}

TEST_CASE("network adaptive: one bad second is not enough, and a lowering is followed by a cool-down")
{
	AdaptiveController c = makeController();
	c.start(6000);
	AdaptiveSample bad;
	bad.droppedFrames = 1;
	bad.totalFrames = 30;
	CHECK(!c.update(bad).change); // one second of trouble: wait
	const AdaptiveDecision d = c.update(bad);
	CHECK(d.change);
	CHECK(d.newKbps < 6000 && d.newKbps >= 4500);
	CHECK(d.reason == AdaptiveReason::Dropped);
	for (int i = 0; i < 5; ++i) // trouble continues, but the cool-down holds
		CHECK(!c.update(bad).change);
}

TEST_CASE("network adaptive: a stopped controller does nothing")
{
	AdaptiveController c = makeController();
	AdaptiveSample bad;
	bad.droppedFrames = 30;
	bad.totalFrames = 30;
	CHECK(!c.update(bad).change); // never started
	c.start(5000);
	c.stop();
	CHECK(!c.update(bad).change);
	CHECK(!c.active());
}

TEST_CASE("network settings: auto-adjust is off by default")
{
	Settings s;
	CHECK(!s.adaptive);
	CHECK(!s.testConsent);
}

TEST_CASE("network settings: sanitize keeps numbers in range")
{
	Settings s;
	s.floorKbps = -5;
	s.ceilingFromProfile = false;
	s.ceilingKbps = 10;
	s.testSeconds = 9999;
	sanitize(s);
	CHECK(s.floorKbps >= 1);
	CHECK(s.ceilingKbps >= s.floorKbps);
	CHECK(s.testSeconds <= 60);
}

TEST_CASE("network settings: history keeps only the newest")
{
	Settings s;
	for (int i = 0; i < int(kMaxHistory) + 5; ++i) {
		HistoryItem h;
		h.when = i;
		addHistory(s, h);
	}
	CHECK(s.history.size() == kMaxHistory);
	CHECK(s.history.back().when == int(kMaxHistory) + 4);
}

TEST_CASE("network adaptive: after failed raises it waits longer before trying again")
{
	AdaptiveController c = makeController(500, 3000);
	// A link that carries 1200 kbps, while the encoder starts at 3000: every raise above ~1200 fails.
	const Run run = simulate(c, 1800, [](int) { return 1200.0; }, false, 3000);
	int firstHalf = 0, secondHalf = 0;
	for (int t = 1; t < 1800; ++t)
		if (run.bitrate[t] != run.bitrate[t - 1])
			(t < 900 ? firstHalf : secondHalf) += 1;
	CHECK(secondHalf <= firstHalf);
	int overshoot = 0; // seconds spent clearly above what the link carries
	for (int kbps : run.bitrate)
		overshoot += kbps > 1400 ? 1 : 0;
	CHECK(overshoot < 100); // under 6% of the half hour
	CHECK(run.bitrate.back() <= 1300);
}
