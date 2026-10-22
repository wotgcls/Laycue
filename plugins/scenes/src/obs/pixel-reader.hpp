// Gets the pixels Laycue measures pushback windows on: from an image file, or from an OBS
// source as OBS draws it (including its filters, such as Chroma Key).
#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace laycue {

// 8-bit RGBA pixels, row by row, no padding.
struct RgbaImage {
	int width = 0;
	int height = 0;
	std::vector<uint8_t> pixels;

	bool empty() const { return width <= 0 || height <= 0 || pixels.empty(); }
};

// Decodes an image file (PNG, JPG, WebP, …) with OBS's own image loader.
// Returns an empty image if the file can't be read.
RgbaImage loadImageFile(const std::string &path);

// Draws the source at its own size, including its filters, and reads the pixels back.
// Call on the UI thread; it waits for OBS's graphics thread (a few milliseconds).
// Returns an empty image if the source is gone or has no picture.
RgbaImage captureSource(const std::string &sourceUuid);

struct VideoSourceInfo {
	std::string uuid;
	std::string name;
};

// Every source with a picture (images, cameras, media…), sorted by name. Scenes excluded.
std::vector<VideoSourceInfo> listVideoSources();

} // namespace laycue
