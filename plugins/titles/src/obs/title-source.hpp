// The "Laycue Title" source: a native OBS source that draws one animated title. It needs no browser
// and no web server. Settings live in the scene like any other source; the Laycue Titles panel
// (and OBS's own Properties window, and hotkeys) all drive the same source.
#pragma once

#include "render/font-set.hpp"

namespace laycue::titles {

constexpr const char *kTitleSourceId = "laycue_title";

// Registers the source type. `fonts` must stay alive until the module unloads.
void registerTitleSource(const FontSet *fonts);

// Settings keys shared with the panel.
namespace key {
constexpr const char *templateId = "template";
constexpr const char *line1 = "line1";
constexpr const char *line2 = "line2";
constexpr const char *tag = "tag";
constexpr const char *custom = "custom";      // an obs_data object: the custom template's overrides
constexpr const char *accentOn = "accent_on"; // use the brand colour below instead of the template's accent
constexpr const char *accent = "accent";      // colour (OBS colour int)
constexpr const char *hold = "hold";          // seconds on air before it hides itself; -1 = the template's own
constexpr const char *onAir = "on_air";       // set by the panel to show or hide; never saved
} // namespace key

} // namespace laycue::titles
