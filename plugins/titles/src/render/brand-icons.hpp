// The social platforms' own icons, drawn as vector paths (no image files, no extra Qt module).
// The shapes come from Simple Icons (CC0, https://simpleicons.org); see docs/ICONS.md. The platforms'
// names and logos belong to their owners and are used here only to point viewers to the operator's own
// page on that platform.
#pragma once

#include <QPainterPath>

#include <vector>

namespace laycue::titles {

enum class Brand { YouTube, Instagram, TikTok, Facebook };

// The icon in a 24 x 24 box, filled with the non-zero rule (as the source shapes are drawn).
QPainterPath brandIconPath(Brand brand);

// Mixlr's logo is a red square with a ring of dots, some of them half see-through. It is not in any open icon
// set, so it is described here by the position of each dot, measured from the logo file the operator supplied
// (data/brand-icons/mixlr-logo-source.png). Positions are fractions of the square (0..1).
struct LogoDot {
	float x;
	float y;
	bool faint; // drawn at half strength
};
constexpr float kMixlrDotRadius = 16.7f / 400.0f;
constexpr unsigned kMixlrRed = 0xED1C24;
const std::vector<LogoDot> &mixlrDots();

} // namespace laycue::titles
