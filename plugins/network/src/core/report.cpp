#include "core/report.hpp"

#include <algorithm>

namespace laycue::network {

void ReportBuilder::start()
{
	*this = ReportBuilder();
	running_ = true;
}

void ReportBuilder::add(const LiveSample &s)
{
	if (!running_)
		return;
	const size_t minute = size_t(seconds_ / 60);
	if (minutes_.size() <= minute)
		minutes_.resize(minute + 1);
	minutes_[minute].dropped += s.droppedFrames;
	minutes_[minute].frames += s.totalFrames;
	++seconds_;
	sentSum_ += s.sentKbps;
	dropped_ += s.droppedFrames;
	frames_ += s.totalFrames;
	if (s.targetKbps > 0.0 && (lowest_ == 0 || int(s.targetKbps) < lowest_))
		lowest_ = int(s.targetKbps);
}

void ReportBuilder::bitrateChanged(int newKbps)
{
	if (!running_)
		return;
	++changes_;
	if (newKbps > 0 && (lowest_ == 0 || newKbps < lowest_))
		lowest_ = newKbps;
}

StreamReport ReportBuilder::finish() const
{
	StreamReport r;
	r.seconds = seconds_;
	r.averageKbps = seconds_ > 0 ? sentSum_ / double(seconds_) : 0.0;
	r.dropPercent = frames_ > 0 ? 100.0 * double(dropped_) / double(frames_) : 0.0;
	r.bitrateChanges = changes_;
	r.lowestKbps = lowest_;
	for (size_t i = 0; i < minutes_.size(); ++i) {
		const double pct =
			minutes_[i].frames > 0 ? 100.0 * double(minutes_[i].dropped) / double(minutes_[i].frames) : 0.0;
		if (pct > r.worstMinuteDropPercent) {
			r.worstMinuteDropPercent = pct;
			r.worstMinuteStart = long(i) * 60;
		}
	}
	r.verdict = r.dropPercent >= 3.0 || r.worstMinuteDropPercent >= 10.0 ? Verdict::Rough
		    : r.dropPercent >= 0.5 || r.bitrateChanges >= 4          ? Verdict::Bumpy
									     : Verdict::Smooth;
	return r;
}

} // namespace laycue::network
