// Checks window detection on the owner's real pushback graphics (tests/fixtures/pushbacks).
// CMake converts them to PAM files first (a raw pixel format this test can read without an
// image library); if ImageMagick isn't installed, this test isn't registered.
//
//   laycue-fixture-tests <folder with the .pam files>
#include "core/detect.hpp"

#include <cstdint>
#include <fstream>
#include <iostream>
#include <sstream>
#include <string>
#include <vector>

namespace {

struct PamImage {
	int width = 0;
	int height = 0;
	std::vector<uint8_t> pixels; // RGBA
};

// Reads a PAM file with DEPTH 4 (RGB_ALPHA) or 3 (RGB), MAXVAL 255.
bool readPam(const std::string &path, PamImage &image)
{
	std::ifstream file(path, std::ios::binary);
	std::string line;
	if (!std::getline(file, line) || line != "P7")
		return false;
	int depth = 0;
	while (std::getline(file, line) && line != "ENDHDR") {
		std::istringstream fields(line);
		std::string key;
		fields >> key;
		if (key == "WIDTH")
			fields >> image.width;
		else if (key == "HEIGHT")
			fields >> image.height;
		else if (key == "DEPTH")
			fields >> depth;
	}
	if (image.width <= 0 || image.height <= 0 || (depth != 3 && depth != 4))
		return false;
	std::vector<uint8_t> raw(static_cast<size_t>(image.width) * image.height * depth);
	if (!file.read(reinterpret_cast<char *>(raw.data()), static_cast<std::streamsize>(raw.size())))
		return false;
	image.pixels.resize(static_cast<size_t>(image.width) * image.height * 4);
	for (size_t i = 0, n = static_cast<size_t>(image.width) * image.height; i < n; ++i) {
		for (int c = 0; c < 3; ++c)
			image.pixels[i * 4 + c] = raw[i * depth + c];
		image.pixels[i * 4 + 3] = depth == 4 ? raw[i * depth + 3] : 255;
	}
	return true;
}

struct Expected {
	const char *file;
	laycue::WindowKind kind;
	laycue::Rect window;
};

} // namespace

int main(int argc, char **argv)
{
	if (argc < 2) {
		std::cerr << "usage: laycue-fixture-tests <folder>\n";
		return 2;
	}
	const std::string folder = argv[1];
	// From tests/fixtures/pushbacks/README.md. Must land within 2 px (PRD F1).
	const Expected cases[] = {
		{"give-cheerfully-green.pam", laycue::WindowKind::Green, {414, 174, 872, 490}},
		{"daily-prayers-green.pam", laycue::WindowKind::Green, {414, 174, 872, 490}},
		{"announcement-transparent.pam", laycue::WindowKind::SeeThrough, {314, 0, 1606, 824}},
	};

	int failures = 0;
	for (const Expected &expected : cases) {
		PamImage image;
		if (!readPam(folder + "/" + expected.file, image)) {
			std::cout << "  FAIL  " << expected.file << ": could not read\n";
			++failures;
			continue;
		}
		const laycue::ImageView view{image.width, image.height, image.width * 4, image.pixels.data()};
		const laycue::DetectResult result = laycue::detectWindowAuto(view);
		const laycue::Rect &w = result.window;
		const laycue::Rect &e = expected.window;
		const auto close = [](int a, int b) {
			return a - b <= 2 && b - a <= 2;
		};
		const bool ok = result.found && result.kind == expected.kind && !result.lowConfidence &&
				close(w.x, e.x) && close(w.y, e.y) && close(w.x + w.width, e.x + e.width) &&
				close(w.y + w.height, e.y + e.height);
		std::cout << (ok ? "  ok    " : "  FAIL  ") << expected.file << ": found " << w.x << "," << w.y << " "
			  << w.width << "x" << w.height << " (fill " << result.fillRatio << "), expected " << e.x << ","
			  << e.y << " " << e.width << "x" << e.height << "\n";
		failures += ok ? 0 : 1;
	}
	return failures == 0 ? 0 : 1;
}
