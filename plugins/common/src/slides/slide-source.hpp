// The OBS source behind Laycue Scripture and Laycue Lyrics. Each plugin registers its own source type with
// this (its own id and name); the panel drives it through its settings. Text fades out and the next fades in,
// so verses and lines never jump.
#pragma once

#include <string>

namespace laycue::slides {

// Settings keys the panel writes.
namespace key {
constexpr const char *text = "text";
constexpr const char *caption = "caption";
// ...and the style keys of slide-style-io (layout, fill, color1, color2, picture, textColor, accent, case, size)
constexpr const char *shown = "shown"; // false: fades out
} // namespace key

// Call once from obs_module_load. `nameKey` is the locale key for the name in OBS's Sources list.
void registerSlideSource(const char *id, const char *nameKey);

} // namespace laycue::slides
