#include "check.hpp"

#include "core/detect.hpp"

#include <cstdint>
#include <vector>

using laycue::DetectOptions;
using laycue::Rect;
using laycue::Rgb;
using laycue::WindowKind;

namespace {

// A simple RGBA test picture.
struct Picture {
	int width;
	int height;
	std::vector<uint8_t> pixels;

	Picture(int w, int h, uint8_t r, uint8_t g, uint8_t b, uint8_t a = 255)
		: width(w),
		  height(h),
		  pixels(static_cast<size_t>(w) * h * 4)
	{
		fill({0, 0, w, h}, r, g, b, a);
	}

	void fill(const Rect &rect, uint8_t r, uint8_t g, uint8_t b, uint8_t a = 255)
	{
		for (int y = rect.y; y < rect.y + rect.height; ++y) {
			for (int x = rect.x; x < rect.x + rect.width; ++x) {
				uint8_t *p = &pixels[(static_cast<size_t>(y) * width + x) * 4];
				p[0] = r;
				p[1] = g;
				p[2] = b;
				p[3] = a;
			}
		}
	}

	laycue::ImageView view() const { return {width, height, width * 4, pixels.data()}; }
};

bool near(const Rect &actual, const Rect &expected, int tolerance)
{
	const auto close = [tolerance](int a, int b) {
		return a - b <= tolerance && b - a <= tolerance;
	};
	return close(actual.x, expected.x) && close(actual.y, expected.y) &&
	       close(actual.x + actual.width, expected.x + expected.width) &&
	       close(actual.y + actual.height, expected.y + expected.height);
}

} // namespace

TEST_CASE("detect: finds a see-through window exactly")
{
	Picture picture(1920, 1080, 20, 30, 80);
	const Rect window{612, 174, 1101, 619}; // odd sizes, not on the working grid
	picture.fill(window, 0, 0, 0, 0);
	DetectOptions options;
	const auto result = laycue::detectWindow(picture.view(), options);
	CHECK(result.found);
	CHECK(result.window == window);
	CHECK(!result.lowConfidence);
}

TEST_CASE("detect: finds a window touching the image edges")
{
	Picture picture(1920, 1080, 200, 60, 10);
	const Rect window{316, 0, 1604, 820};
	picture.fill(window, 0, 0, 0, 0);
	const auto result = laycue::detectWindow(picture.view(), DetectOptions{});
	CHECK(result.found);
	CHECK(result.window == window);
}

TEST_CASE("detect: picks the largest window when there are several")
{
	Picture picture(1280, 720, 10, 10, 10);
	picture.fill({40, 40, 200, 120}, 0, 0, 0, 0);
	const Rect big{400, 200, 700, 400};
	picture.fill(big, 0, 0, 0, 0);
	const auto result = laycue::detectWindow(picture.view(), DetectOptions{});
	CHECK(result.found);
	CHECK(result.window == big);
}

TEST_CASE("detect: no window in an opaque image")
{
	Picture picture(800, 450, 10, 10, 10);
	const auto result = laycue::detectWindow(picture.view(), DetectOptions{});
	CHECK(!result.found);
}

TEST_CASE("detect: ignores specks smaller than 1% of the image")
{
	Picture picture(800, 450, 10, 10, 10);
	picture.fill({100, 100, 20, 20}, 0, 0, 0, 0);
	const auto result = laycue::detectWindow(picture.view(), DetectOptions{});
	CHECK(!result.found);
}

TEST_CASE("detect: an L-shaped window is found but flagged 'check the edges'")
{
	Picture picture(1280, 720, 10, 10, 10);
	picture.fill({100, 100, 800, 200}, 0, 0, 0, 0);
	picture.fill({100, 300, 200, 300}, 0, 0, 0, 0);
	const auto result = laycue::detectWindow(picture.view(), DetectOptions{});
	CHECK(result.found);
	CHECK(result.lowConfidence);
	CHECK(result.fillRatio < 0.9);
}

TEST_CASE("detect: samples a real-world green, not pure green")
{
	Picture picture(1320, 1020, 17, 17, 17);
	picture.fill({414, 174, 872, 490}, 0x00, 0xB1, 0x3F);
	picture.fill({50, 50, 30, 30}, 0x00, 0xFF, 0x00); // a small pure-green logo
	const auto key = laycue::sampleKeyColour(picture.view());
	CHECK(key.has_value());
	CHECK_EQ(int(key->g), 0xB1);
	CHECK_EQ(int(key->b), 0x3F);
}

TEST_CASE("detect: finds a #00B13F green window with slight noise")
{
	Picture picture(1320, 1020, 17, 17, 17);
	const Rect window{414, 174, 872, 490};
	picture.fill(window, 0x00, 0xB1, 0x3F);
	// JPEG-like noise inside the window
	for (int y = window.y; y < window.y + window.height; y += 7)
		picture.fill({window.x + (y % 50) * 10, y, 3, 1}, 0x08, 0xA8, 0x46);
	DetectOptions options;
	options.kind = WindowKind::Green;
	const auto result = laycue::detectWindow(picture.view(), options);
	CHECK(result.found);
	CHECK(near(result.window, window, 1));
	CHECK(!result.lowConfidence);
}

TEST_CASE("detect: auto uses see-through first, then green")
{
	Picture green(1280, 720, 30, 30, 30);
	green.fill({300, 100, 600, 400}, 0x00, 0xB1, 0x3F);
	auto result = laycue::detectWindowAuto(green.view());
	CHECK(result.found);
	CHECK(result.kind == WindowKind::Green);

	Picture clear(1280, 720, 30, 30, 30);
	clear.fill({300, 100, 600, 400}, 0, 0, 0, 0);
	result = laycue::detectWindowAuto(clear.view());
	CHECK(result.found);
	CHECK(result.kind == WindowKind::SeeThrough);
}

TEST_CASE("detect: no green in the image means no green window")
{
	Picture picture(640, 360, 200, 40, 40);
	DetectOptions options;
	options.kind = WindowKind::Green;
	CHECK(!laycue::sampleKeyColour(picture.view()).has_value());
	CHECK(!laycue::detectWindow(picture.view(), options).found);
}

TEST_CASE("detect: empty image is handled")
{
	const laycue::ImageView empty{};
	CHECK(!laycue::detectWindowAuto(empty).found);
}
