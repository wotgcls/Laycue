// The social platforms' own icons, drawn as vector paths (no image files, no extra Qt module).
// The shapes come from Simple Icons (CC0, https://simpleicons.org); see docs/ICONS.md. The platforms'
// names and logos belong to their owners and are used here only to point viewers to the operator's own
// page on that platform.
#pragma once

#include <QPainterPath>

namespace laycue::titles {

enum class Brand { YouTube, Instagram, TikTok, Facebook };

// The icon in a 24 x 24 box, filled with the non-zero rule (as the source shapes are drawn).
QPainterPath brandIconPath(Brand brand);

} // namespace laycue::titles
