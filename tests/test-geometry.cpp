#include "check.hpp"

#include "core/geometry.hpp"

using laycue::Affine;
using laycue::Crop;
using laycue::RectF;

namespace {
bool same(double a, double b)
{
	return a - b < 0.01 && b - a < 0.01;
}
} // namespace

TEST_CASE("geometry: intersect of overlapping and separate rects")
{
	const RectF overlap = laycue::intersect({0, 0, 100, 100}, {50, 60, 100, 100});
	CHECK(same(overlap.x, 50) && same(overlap.y, 60) && same(overlap.w, 50) && same(overlap.h, 40));
	CHECK(laycue::intersect({0, 0, 10, 10}, {20, 20, 10, 10}).empty());
	CHECK(laycue::intersect({0, 0, 10, 10}, {10, 0, 10, 10}).empty()); // touching edges only
}

TEST_CASE("geometry: tiny overlaps don't count")
{
	// 5 px sliver: under 8 px.
	CHECK(!laycue::overlapsMeaningfully({0, 0, 100, 100}, {95, 0, 100, 100}));
	// 8 px each way, but only 0.64% of the smaller spot's area.
	CHECK(!laycue::overlapsMeaningfully({0, 0, 1000, 1000}, {992, 992, 100, 100}));
	CHECK(laycue::overlapsMeaningfully({0, 0, 100, 100}, {50, 50, 100, 100}));
}

TEST_CASE("geometry: 2% rule uses the smaller spot")
{
	// Overlap 10x10 = 100 px². Smaller spot is 100x100 = 10000 → 1%: ignored.
	CHECK(!laycue::overlapsMeaningfully({0, 0, 2000, 1000}, {1990, 990, 100, 100}));
	// Same overlap against a 40x40 spot = 6.25%: counts.
	CHECK(laycue::overlapsMeaningfully({0, 0, 2000, 1000}, {1990, 990, 40, 40}));
}

TEST_CASE("geometry: an unmoved, uncropped source maps 1:1 through a unit box at 1:1 scale")
{
	// Item at (100, 50), source 400x200 shown at full size: unit box → 400x200 at (100,50).
	const Affine box{400, 0, 0, 200, 100, 50};
	const RectF r = laycue::sourceRectToCanvas(400, 200, Crop{}, box, {40, 20, 100, 60});
	CHECK(same(r.x, 140) && same(r.y, 70) && same(r.w, 100) && same(r.h, 60));
}

TEST_CASE("geometry: scaled and cropped source")
{
	// Source 400x200, crop 100 off the left. Shown area 300x200, drawn at half size at (10, 10).
	const Affine box{150, 0, 0, 100, 10, 10};
	const Crop crop{100, 0, 0, 0};
	// A rect in the source at x 100..200 is the first third of the shown area.
	const RectF r = laycue::sourceRectToCanvas(400, 200, crop, box, {100, 0, 100, 200});
	CHECK(same(r.x, 10) && same(r.w, 50) && same(r.h, 100));
	// A rect entirely inside the crop is not visible.
	CHECK(laycue::sourceRectToCanvas(400, 200, crop, box, {0, 0, 90, 200}).empty());
}

TEST_CASE("geometry: fill crops the wider source evenly")
{
	// 1920x1080 camera into a 1200x600 window (2:1): the source is taller, so trim top and bottom.
	const laycue::Placement p = laycue::fillWindow(1920, 1080, {100, 50, 1200, 600});
	CHECK(p.crop.left == 0 && p.crop.right == 0);
	CHECK(p.crop.top == 60 && p.crop.bottom == 60); // keep 1920/2 = 960 rows
	CHECK(same(p.box.x, 100) && same(p.box.w, 1200) && same(p.box.h, 600));
}

TEST_CASE("geometry: fill trims the sides of a wide source")
{
	// 1920x1080 into a square window: keep 1080 columns.
	const laycue::Placement p = laycue::fillWindow(1920, 1080, {0, 0, 500, 500});
	CHECK(p.crop.left == 420 && p.crop.right == 420);
	CHECK(p.crop.top == 0 && p.crop.bottom == 0);
}

TEST_CASE("geometry: fill of a matching shape crops nothing")
{
	const laycue::Placement p = laycue::fillWindow(1920, 1080, {0, 0, 872, 490.5});
	CHECK(p.crop.left == 0 && p.crop.right == 0 && p.crop.top == 0 && p.crop.bottom == 0);
}

TEST_CASE("geometry: whole camera is centred inside the window")
{
	const laycue::Placement p = laycue::fitInsideWindow(1920, 1080, {100, 100, 600, 600});
	CHECK(same(p.box.w, 600) && same(p.box.h, 337.5));
	CHECK(same(p.box.x, 100) && same(p.box.y, 100 + (600 - 337.5) / 2));
	CHECK(p.crop.left == 0 && p.crop.top == 0);
}

TEST_CASE("geometry: pushback picture placement")
{
	// 16:9 picture on a 16:9 canvas covers it.
	RectF r = laycue::imagePlacementOnCanvas(1920, 1080, 1920, 1080);
	CHECK(same(r.w, 1920) && same(r.h, 1080));
	// 1320x1020 screenshot is fitted and centred, not stretched.
	r = laycue::imagePlacementOnCanvas(1320, 1020, 1920, 1080);
	CHECK(same(r.h, 1080) && r.w < 1920 && same(r.x, (1920 - r.w) / 2));
}

TEST_CASE("geometry: window in the picture lands on the canvas")
{
	// Full-canvas 1920x1080 picture at 1:1: same numbers.
	RectF w = laycue::windowOnCanvas({314, 0, 1606, 824}, 1920, 1080, {0, 0, 1920, 1080});
	CHECK(same(w.x, 314) && same(w.y, 0) && same(w.w, 1606) && same(w.h, 824));
	// A 960x540 picture scaled 2x to the canvas doubles the window.
	w = laycue::windowOnCanvas({100, 50, 400, 200}, 960, 540, {0, 0, 1920, 1080});
	CHECK(same(w.x, 200) && same(w.y, 100) && same(w.w, 800) && same(w.h, 400));
}
