#include "core/adaptive.hpp"

#include <algorithm>
#include <cmath>

namespace laycue::network {

void AdaptiveController::configure(const AdaptiveConfig &config)
{
	config_ = config;
	config_.floorKbps = std::max(config_.floorKbps, 300);
	config_.ceilingKbps = std::max(config_.ceilingKbps, config_.floorKbps);
	config_.stepDownPercent = std::clamp(config_.stepDownPercent, 5, 60);
	config_.badSecondsBeforeDrop = std::max(config_.badSecondsBeforeDrop, 1);
	config_.cooldownSeconds = std::max(config_.cooldownSeconds, 1);
	config_.raiseAfterGoodSeconds = std::max(config_.raiseAfterGoodSeconds, 3);
	config_.minStepUpKbps = std::max(config_.minStepUpKbps, 50);
	if (current_ > 0)
		current_ = std::clamp(current_, config_.floorKbps, config_.ceilingKbps);
}

void AdaptiveController::start(int currentKbps)
{
	active_ = true;
	frozen_ = false;
	current_ = std::clamp(currentKbps, config_.floorKbps, config_.ceilingKbps);
	now_ = 0;
	badRun_ = goodRun_ = 0;
	lastDropAt_ = lastRaiseAt_ = -1000000;
	raisedFrom_ = 0;
	raiseBackoff_ = 0;
	softCeiling_ = 0;
	softCeilingUntil_ = -1;
	verifying_ = false;
}

void AdaptiveController::stop()
{
	active_ = false;
	verifying_ = false;
}

int AdaptiveController::effectiveCeiling() const
{
	if (now_ < softCeilingUntil_ && softCeiling_ > 0)
		return std::min(config_.ceilingKbps, std::max(softCeiling_, config_.floorKbps));
	return config_.ceilingKbps;
}

void AdaptiveController::resetLimits()
{
	softCeiling_ = 0;
	softCeilingUntil_ = -1;
	raisedFrom_ = 0;
	raiseBackoff_ = 0;
	lastDropAt_ = -1000000;
	goodRun_ = std::max(goodRun_, config_.raiseAfterGoodSeconds);
}

AdaptiveDecision AdaptiveController::update(const AdaptiveSample &s)
{
	AdaptiveDecision none;
	if (!active_ || frozen_)
		return none;
	++now_;

	// Did the encoder obey the last lowering? After a few seconds to settle, it should send close to the new target.
	if (verifying_) {
		if (now_ >= verifyFrom_ && s.sentKbps > 0.0) {
			verifySum_ += s.sentKbps;
			++verifyCount_;
		}
		if (now_ >= verifyUntil_) {
			verifying_ = false;
			if (verifyCount_ >= 3 && verifySum_ / verifyCount_ > double(verifyTarget_) * 1.4) {
				frozen_ = true;
				AdaptiveDecision d;
				d.reason = AdaptiveReason::Rejected;
				return d; // no change: it is the report that matters
			}
		}
	}

	const bool congested = s.congestion >= 0.15;
	const bool dropped = s.droppedFrames > 0;
	const bool bad = dropped || congested;
	if (bad) {
		++badRun_;
		goodRun_ = 0;
	} else {
		badRun_ = 0;
		++goodRun_;
	}

	if (badRun_ >= config_.badSecondsBeforeDrop && now_ - lastDropAt_ >= config_.cooldownSeconds) {
		int percent = config_.stepDownPercent;
		if (s.totalFrames > 0 && double(s.droppedFrames) / double(s.totalFrames) > 0.05)
			percent += 10; // heavy loss: a bigger step
		const int next =
			std::max(config_.floorKbps, int(std::lround(double(current_) * (100 - percent) / 100.0)));
		AdaptiveDecision d;
		d.reason = dropped ? AdaptiveReason::Dropped : AdaptiveReason::Congestion;
		if (next >= current_) {
			d.reason = AdaptiveReason::AtFloor;
			badRun_ = 0;
			return d;
		}
		// Trouble soon after a raise: that level is too much for now. Remember it.
		if (now_ - lastRaiseAt_ <= 60 && raisedFrom_ > 0) {
			softCeiling_ = raisedFrom_;
			softCeilingUntil_ = now_ + config_.probeMemorySeconds;
			raiseBackoff_ = std::min(raiseBackoff_ + 1, 3);
		}
		current_ = next;
		lastDropAt_ = now_;
		badRun_ = goodRun_ = 0;
		verifying_ = true;
		verifyFrom_ = now_ + 4;
		verifyUntil_ = now_ + 10;
		verifyTarget_ = next;
		verifySum_ = 0.0;
		verifyCount_ = 0;
		d.change = true;
		d.newKbps = next;
		return d;
	}

	const int limit = effectiveCeiling();
	if (raiseBackoff_ > 0 && now_ - lastDropAt_ >= 180)
		raiseBackoff_ = 0; // a long calm stretch: forgive
	const int calmNeeded = config_.raiseAfterGoodSeconds << raiseBackoff_;
	if (goodRun_ >= calmNeeded && current_ < limit && now_ - lastDropAt_ >= config_.cooldownSeconds) {
		// Steps are a third of the way to the limit, but never more than a quarter of the current rate, so a
		// raise that turns out too high overshoots only a little.
		const int step = std::max(config_.minStepUpKbps, std::min((limit - current_) / 3, current_ / 4));
		const int next = std::min(limit, current_ + step);
		AdaptiveDecision d;
		d.change = true;
		d.newKbps = next;
		d.reason = AdaptiveReason::Recovering;
		raisedFrom_ = current_;
		lastRaiseAt_ = now_;
		current_ = next;
		goodRun_ = 0;
		return d;
	}
	return none;
}

} // namespace laycue::network
