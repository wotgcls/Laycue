// The "Laycue Clock" source: a timer drawn into the picture. It keeps its own time, so it runs whatever scene is
// showing, and the dock, hotkeys and Properties window all drive it through the same few commands.
#pragma once

namespace laycue::clock {

class ClockFonts;

constexpr const char *kClockSourceId = "laycue_clock";

namespace key {
constexpr const char *mode = "mode";
constexpr const char *duration = "duration";
constexpr const char *target = "target";
constexpr const char *label = "label";
constexpr const char *design = "design";
constexpr const char *background = "background";
constexpr const char *warn = "warn_seconds";
constexpr const char *overtime = "overtime";
constexpr const char *text = "text_colour";
constexpr const char *accent = "accent_colour";
constexpr const char *warning = "warning_colour";
constexpr const char *over = "over_colour";
constexpr const char *panel = "panel_colour";
constexpr const char *panelOpacity = "panel_opacity";
constexpr const char *align = "align";
constexpr const char *vertical = "vertical";
constexpr const char *sizePercent = "size_percent";
} // namespace key

void registerClockSource(const ClockFonts *fonts);

} // namespace laycue::clock
