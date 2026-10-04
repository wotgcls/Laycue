#include "core/ticker.hpp"

#include <algorithm>
#include <cmath>

namespace laycue::titles {

void TickerState::configure(double stripWidth, double pixelsPerSecond)
{
	stripWidth_ = std::max(stripWidth, 1.0);
	speed_ = std::max(pixelsPerSecond, 0.0);
	offset_ = std::fmod(offset_, stripWidth_);
}

void TickerState::advance(double seconds)
{
	if (seconds <= 0.0)
		return;
	offset_ = std::fmod(offset_ + speed_ * seconds, stripWidth_);
}

int TickerState::copiesNeeded(double viewWidth) const
{
	// One copy partly off the left edge, then enough to reach the right edge, plus one spare.
	return static_cast<int>(std::ceil(std::max(viewWidth, 0.0) / stripWidth_)) + 1;
}

} // namespace laycue::titles
