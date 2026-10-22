// Draws the frame picture for a layout with a background: a gold or silver edge around every slot, on a see-through
// canvas-sized picture. Qt only (no libobs), so it can be tested on its own.
#pragma once

#include "core/frame-style.hpp"
#include "core/geometry.hpp"

#include <string>
#include <vector>

namespace laycue {

// Writes a PNG of `canvasWidth` x `canvasHeight`: for each slot rectangle, an edge of `thickness` pixels drawn just
// outside it. Returns false if the picture could not be written.
bool renderFramePicture(const std::string &path, int canvasWidth, int canvasHeight, const std::vector<RectF> &frames,
			double thickness, Metal metal);

} // namespace laycue
