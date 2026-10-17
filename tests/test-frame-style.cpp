#include "check.hpp"

#include "core/frame-style.hpp"

using namespace laycue;

namespace {
std::vector<uint8_t> solid(int w, int h, uint8_t r, uint8_t g, uint8_t b, uint8_t a = 255)
{
	std::vector<uint8_t> p;
	for (int i = 0; i < w * h; ++i) {
		p.push_back(r);
		p.push_back(g);
		p.push_back(b);
		p.push_back(a);
	}
	return p;
}
} // namespace

TEST_CASE("frame: the average colour of a picture, ignoring clear pixels")
{
	const Rgb8 blue = averageColor(solid(10, 10, 20, 40, 200), 10, 10);
	CHECK(blue.b == 200 && blue.r == 20);
	std::vector<uint8_t> half = solid(10, 10, 255, 0, 0);
	for (size_t i = 0; i < 50; ++i)
		half[i * 4 + 3] = 0; // half the picture is see-through: it does not pull the average
	CHECK(averageColor(half, 10, 10).r == 255);
	CHECK(averageColor({}, 0, 0).g == 128);                        // nothing to look at: mid grey
	CHECK(averageColor(solid(4, 4, 255, 0, 0, 0), 4, 4).g == 128); // fully clear: mid grey
}

TEST_CASE("frame: gold or silver, whichever stands out")
{
	CHECK(chooseMetal({30, 60, 190}) == Metal::Gold);    // deep blue, like the reference
	CHECK(chooseMetal({90, 40, 140}) == Metal::Gold);    // purple
	CHECK(chooseMetal({20, 120, 60}) == Metal::Gold);    // green
	CHECK(chooseMetal({240, 240, 245}) == Metal::Gold);  // near white
	CHECK(chooseMetal({230, 160, 40}) == Metal::Silver); // warm orange
	CHECK(chooseMetal({220, 200, 60}) == Metal::Silver); // yellow
	CHECK(chooseMetal({25, 25, 28}) == Metal::Silver);   // black
}

TEST_CASE("frame: the edge always fits inside half the space between slots")
{
	for (double h : {360.0, 720.0, 1080.0, 2160.0}) {
		const double w = h * 16.0 / 9.0;
		CHECK(frameThickness(w, h) < frameGap(w, h) / 2.0);
		CHECK(frameThickness(w, h) >= 2.0);
	}
	const MetalTones gold = metalTones(Metal::Gold), silver = metalTones(Metal::Silver);
	CHECK(gold.light.r > gold.dark.r && silver.light.r > silver.dark.r);
}
