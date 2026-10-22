// Everything about how one Laycue Clock looks and counts, as plain values. The dock edits one of these and writes
// it to the clock source; the source reads it back. No OBS, no Qt.
#pragma once

#include <cstdint>
#include <string>

namespace laycue::clock {

struct ClockSettings {
	int mode = 2;                   // Mode: 0 countdown, 1 count up, 2 until a time of day (the default)
	std::string duration = "05:00"; // typed by the operator, read with parseDuration
	std::string target = "10:00";   // typed by the operator, read with parseTimeOfDay
	std::string label = "Service starts in";
	int design = 0;           // Design, 0..7
	int background = 0;       // Background: 0 none (overlay), 1 card, 2 full screen
	int warnSeconds = 60;     // the clock changes colour when this little time is left
	bool overtime = true;     // after zero, keep counting up as "+00:12"
	uint32_t text = 0xFFFFFF; // 0xRRGGBB
	uint32_t accent = 0xFF8C28;
	uint32_t warning = 0xFFB31A;
	uint32_t over = 0xFF4D2E;
	uint32_t panel = 0x111110;
	int panelOpacity = 86; // percent, for the card or the full screen colour
	int align = 1;         // 0 left, 1 centre, 2 right
	int vertical = 1;      // 0 top, 1 middle, 2 bottom
	int sizePercent = 100; // 40..200

	bool operator==(const ClockSettings &o) const
	{
		return mode == o.mode && duration == o.duration && target == o.target && label == o.label &&
		       design == o.design && background == o.background && warnSeconds == o.warnSeconds &&
		       overtime == o.overtime && text == o.text && accent == o.accent && warning == o.warning &&
		       over == o.over && panel == o.panel && panelOpacity == o.panelOpacity && align == o.align &&
		       vertical == o.vertical && sizePercent == o.sizePercent;
	}
};

constexpr int kMinSizePercent = 40;
constexpr int kMaxSizePercent = 200;

// Brings every value into range and every text to a sensible length; never throws.
void sanitize(ClockSettings &s);

} // namespace laycue::clock
