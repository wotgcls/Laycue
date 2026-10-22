// The bitrate controller: lowers the video bitrate quickly when the connection cannot keep up, and raises
// it again slowly, remembering where it failed so it does not keep trying the same too-high number.
// Pure logic: it decides; it never touches OBS. Call update() once a second.
//
// Why a controller of our own and not only OBS's "dynamic bitrate"? OBS's drops fast and recovers slowly
// (minutes), forgets nothing it learned, works for RTMP only, and cannot say why it changed. This one:
//  * has a floor you set (never below it) and a ceiling (never above what you chose),
//  * recovers on a short, bounded ramp,
//  * learns: if a raise is followed by trouble, it caps itself just under that level for a while and waits
//    twice as long before trying again (up to 8 times the normal wait),
//  * checks that the encoder really obeyed a lowering and stops, saying so, if it did not (no loops),
//  * tells you the reason for every change.
#pragma once

namespace laycue::network {

struct AdaptiveConfig {
	int floorKbps = 1000;
	int ceilingKbps = 6000;
	int stepDownPercent = 20;       // how much to lower on trouble
	int badSecondsBeforeDrop = 2;   // trouble must last this long
	int cooldownSeconds = 10;       // after lowering, wait before lowering again
	int raiseAfterGoodSeconds = 15; // calm seconds needed before a raise
	int minStepUpKbps = 250;
	int probeMemorySeconds = 300; // how long a failed level stays capped
};

struct AdaptiveSample {
	double congestion = -1.0; // 0..1, or negative if unknown
	int droppedFrames = 0;
	int totalFrames = 0;
	double sentKbps = 0.0;
};

enum class AdaptiveReason {
	None,
	Dropped,    // frames were dropped
	Congestion, // the output reported congestion
	Recovering, // the connection has been calm: stepping back up
	AtFloor,    // in trouble but already at the floor
	Rejected,   // the encoder did not follow the change: stopped
};

struct AdaptiveDecision {
	bool change = false;
	int newKbps = 0;
	AdaptiveReason reason = AdaptiveReason::None;
};

class AdaptiveController {
public:
	void configure(const AdaptiveConfig &config);
	// `currentKbps` is what the encoder is set to now.
	void start(int currentKbps);
	void stop();

	AdaptiveDecision update(const AdaptiveSample &sample);

	// Forget where it failed: the operator knows the connection is better now ("try full bitrate").
	void resetLimits();

	bool active() const { return active_; }
	bool frozen() const { return frozen_; } // stopped because the encoder ignored a change
	int current() const { return current_; }
	// The ceiling in force right now: the configured ceiling, or lower while a failed level is remembered.
	int effectiveCeiling() const;

private:
	AdaptiveConfig config_;
	bool active_ = false;
	bool frozen_ = false;
	int current_ = 0;
	long now_ = 0;
	int badRun_ = 0;
	int goodRun_ = 0;
	long lastDropAt_ = -1000000;
	long lastRaiseAt_ = -1000000;
	int raisedFrom_ = 0;
	int raiseBackoff_ = 0; // each failed raise doubles the calm time needed before the next one (up to 8x)
	int softCeiling_ = 0;
	long softCeilingUntil_ = -1;

	// Checking that a lowering was obeyed.
	bool verifying_ = false;
	long verifyFrom_ = 0;
	long verifyUntil_ = 0;
	int verifyTarget_ = 0;
	double verifySum_ = 0.0;
	int verifyCount_ = 0;
};

} // namespace laycue::network
