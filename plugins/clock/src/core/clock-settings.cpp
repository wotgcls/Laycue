#include "core/clock-settings.hpp"

#include "core/clock-core.hpp"

#include <algorithm>

namespace laycue::clock {

namespace {

void clip(std::string &s, size_t max)
{
	if (s.size() > max)
		s.resize(max);
}

} // namespace

void sanitize(ClockSettings &s)
{
	s.mode = std::clamp(s.mode, 0, kModeCount - 1);
	s.design = std::clamp(s.design, 0, 7);
	s.background = std::clamp(s.background, 0, 2);
	s.warnSeconds = std::clamp(s.warnSeconds, 0, 3600);
	s.panelOpacity = std::clamp(s.panelOpacity, 0, 100);
	s.align = std::clamp(s.align, 0, 2);
	s.vertical = std::clamp(s.vertical, 0, 2);
	s.sizePercent = std::clamp(s.sizePercent, kMinSizePercent, kMaxSizePercent);
	s.text &= 0xFFFFFF;
	s.accent &= 0xFFFFFF;
	s.warning &= 0xFFFFFF;
	s.over &= 0xFFFFFF;
	s.panel &= 0xFFFFFF;
	clip(s.label, 80);
	clip(s.duration, 16);
	clip(s.target, 16);
	double seconds = 0.0;
	if (!parseDuration(s.duration, seconds))
		s.duration = "05:00";
	int secondOfDay = 0;
	if (!parseTimeOfDay(s.target, secondOfDay))
		s.target = "10:00";
}

} // namespace laycue::clock
