#include "core/title-clock.hpp"

#include "core/easing.hpp"

#include <algorithm>

namespace laycue::titles {

void TitleClock::configure(double inSeconds, double outSeconds, double holdSeconds)
{
	inSeconds_ = std::max(inSeconds, 0.0);
	outSeconds_ = std::max(outSeconds, 0.0);
	holdSeconds_ = std::max(holdSeconds, 0.0);
}

void TitleClock::show()
{
	switch (phase_) {
	case Phase::Hidden:
		phase_ = Phase::In;
		in_ = 0.0;
		out_ = 0.0;
		held_ = 0.0;
		if (inSeconds_ <= 0.0) {
			phase_ = Phase::Hold;
			in_ = 1.0;
		}
		break;
	case Phase::Out:
		// Turn around: how visible it is now becomes how far in it is.
		in_ = clamp01(in_ * (1.0 - out_));
		out_ = 0.0;
		held_ = 0.0;
		phase_ = in_ >= 1.0 ? Phase::Hold : Phase::In;
		break;
	case Phase::In:
	case Phase::Hold:
		held_ = 0.0; // showing again restarts the auto-hide countdown
		break;
	}
}

void TitleClock::hide()
{
	switch (phase_) {
	case Phase::Hidden:
	case Phase::Out:
		break;
	case Phase::In:
	case Phase::Hold:
		phase_ = Phase::Out;
		out_ = 0.0;
		if (outSeconds_ <= 0.0)
			reset();
		break;
	}
}

void TitleClock::reset()
{
	phase_ = Phase::Hidden;
	in_ = 0.0;
	out_ = 0.0;
	held_ = 0.0;
}

void TitleClock::advance(double seconds)
{
	if (seconds <= 0.0)
		return;
	switch (phase_) {
	case Phase::Hidden:
		break;
	case Phase::In:
		in_ = inSeconds_ > 0.0 ? clamp01(in_ + seconds / inSeconds_) : 1.0;
		if (in_ >= 1.0) {
			phase_ = Phase::Hold;
			held_ = 0.0;
		}
		break;
	case Phase::Hold:
		held_ += seconds;
		if (holdSeconds_ > 0.0 && held_ >= holdSeconds_)
			hide();
		break;
	case Phase::Out:
		out_ = outSeconds_ > 0.0 ? clamp01(out_ + seconds / outSeconds_) : 1.0;
		if (out_ >= 1.0)
			reset();
		break;
	}
}

double TitleClock::holdRemaining() const
{
	if (holdSeconds_ <= 0.0 || phase_ == Phase::Hidden)
		return -1.0;
	return std::max(holdSeconds_ - held_, 0.0);
}

} // namespace laycue::titles
