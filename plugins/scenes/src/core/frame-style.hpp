// The frame drawn around each slot of a layout that has a background picture. Whatever the picture looks like,
// the frame is metal: gold or silver, whichever stands out better against it. Pure logic: no libobs, no Qt.
#pragma once

#include <cstdint>
#include <vector>

namespace laycue {

enum class Metal { Gold, Silver };

struct Rgb8 {
	uint8_t r = 0;
	uint8_t g = 0;
	uint8_t b = 0;
};

// The picture's average colour (see-through pixels count for less, fully clear ones not at all). Samples about
// 40,000 pixels so a big picture is quick. A picture with nothing visible gives mid grey.
Rgb8 averageColor(const std::vector<uint8_t> &rgbaPixels, int width, int height);

// Gold on blues, purples, greens, dark reds and light pictures; silver on warm yellow/orange pictures (where gold
// would melt into the background) and on dark grey ones.
Metal chooseMetal(Rgb8 average);

// The three tones of a metal edge, light to dark; the frame is drawn as a diagonal gradient through them.
struct MetalTones {
	Rgb8 light;
	Rgb8 mid;
	Rgb8 dark;
};
MetalTones metalTones(Metal metal);

// Frame sizes for a canvas, in canvas pixels: the space around and between slots, and the thickness of the
// edge. The edge always fits inside half the space between two slots.
double frameGap(double canvasWidth, double canvasHeight);
double frameThickness(double canvasWidth, double canvasHeight);

} // namespace laycue
