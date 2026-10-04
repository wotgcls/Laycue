// Finds the "window" in a pushback graphic: the see-through or green area the camera
// shows through. Pure logic: no libobs, no Qt.
//
// Steps (see CLAUDE.md, "Hole detection"):
//   1. Mark every pixel that is part of the window (alpha below a threshold, or close to
//      the key green in colour).
//   2. On a copy about 640 px wide, find the largest connected area of window pixels.
//   3. Take its bounding rectangle, scale it back up and refine each edge at full size.
//   4. Report how much of that rectangle really is window (fill ratio); under 0.9 the
//      result is flagged "check the edges".
#pragma once

#include <cstdint>
#include <optional>

namespace laycue {

struct Rect {
	int x = 0;
	int y = 0;
	int width = 0;
	int height = 0;

	bool operator==(const Rect &other) const
	{
		return x == other.x && y == other.y && width == other.width && height == other.height;
	}
};

struct Rgb {
	uint8_t r = 0;
	uint8_t g = 0;
	uint8_t b = 0;
};

// A read-only view of 8-bit RGBA pixels (row by row, top to bottom). Not owned.
struct ImageView {
	int width = 0;
	int height = 0;
	int stride = 0; // bytes per row
	const uint8_t *pixels = nullptr;

	const uint8_t *at(int x, int y) const { return pixels + static_cast<long>(y) * stride + x * 4; }
};

enum class WindowKind {
	SeeThrough, // the window is transparent (PNG/WebP, or after OBS's Chroma Key filter)
	Green,      // the window is a green area in an opaque image
};

struct DetectOptions {
	WindowKind kind = WindowKind::SeeThrough;
	int alphaThreshold = 16;  // see-through: alpha below this is window
	int chromaTolerance = 28; // green: CbCr distance from the key colour below this is window
	std::optional<Rgb> key;   // green: the key colour; sampled from the image when empty
};

struct DetectResult {
	bool found = false;
	WindowKind kind = WindowKind::SeeThrough;
	Rect window;               // in the image's own pixels
	double fillRatio = 0;      // window pixels ÷ rectangle area
	bool lowConfidence = true; // fillRatio under 0.9: "check the edges"
	Rgb key;                   // green: the key colour used
};

// The most common strongly green colour (g > r+60 and g > b+60), or nothing if the image
// has no strongly green pixels. Real designs use greens like #00B13F, not pure #00FF00.
std::optional<Rgb> sampleKeyColour(const ImageView &image);

// Finds the window using one kind of detection.
DetectResult detectWindow(const ImageView &image, const DetectOptions &options);

// Tries see-through first, then green (with a sampled key colour).
DetectResult detectWindowAuto(const ImageView &image);

// The smallest window Laycue accepts, as a share of the whole image (1%).
constexpr double kMinWindowShare = 0.01;
constexpr double kConfidentFill = 0.9;

} // namespace laycue
