#include "check.hpp"

#include "core/layouts.hpp"

using laycue::RectF;

#include <cmath>
#define CHECK_NEAR(a, b, e) CHECK(std::fabs((a) - (b)) < (e))

TEST_CASE("layouts: every template has slots and a known id")
{
	CHECK(!laycue::layoutTemplates().empty());
	for (const auto &layout : laycue::layoutTemplates()) {
		CHECK(!layout.shapes.empty());
		CHECK(laycue::findTemplate(layout.id) == &layout);
	}
	CHECK(laycue::findTemplate("nope") == nullptr);
}

TEST_CASE("layouts: side by side splits the canvas in two with no gap")
{
	const auto *layout = laycue::findTemplate("side-by-side");
	const auto rects = laycue::slotRects(*layout, 1920, 1080, 0);
	CHECK_EQ(int(rects.size()), 2);
	CHECK_EQ(int(rects[0].w), 960);
	CHECK_EQ(int(rects[1].x), 960);
	CHECK_EQ(int(rects[1].h), 1080);
}

TEST_CASE("layouts: a gap keeps slots apart and inside the canvas")
{
	const auto *layout = laycue::findTemplate("four-grid");
	const auto rects = laycue::slotRects(*layout, 1920, 1080, 24);
	CHECK_EQ(int(rects.size()), 4);
	for (const RectF &r : rects) {
		CHECK(r.x >= 24 - 0.001);
		CHECK(r.y >= 24 - 0.001);
		CHECK(r.right() <= 1920 - 24 + 0.001);
		CHECK(r.bottom() <= 1080 - 24 + 0.001);
	}
	CHECK(!laycue::overlapsMeaningfully(rects[0], rects[1]));
	CHECK(!laycue::overlapsMeaningfully(rects[0], rects[2]));
	CHECK(rects[1].x - rects[0].right() >= 24 - 0.001);
}

TEST_CASE("layouts: picture in picture keeps the small slot over the main one")
{
	const auto *layout = laycue::findTemplate("picture-in-picture");
	const auto rects = laycue::slotRects(*layout, 1920, 1080, 0);
	CHECK(rects[1].w < rects[0].w);
	CHECK(rects[1].right() <= 1920);
	CHECK(rects[1].bottom() <= 1080);
}

TEST_CASE("layouts: a huge gap is limited and an empty canvas gives nothing")
{
	const auto *layout = laycue::findTemplate("stacked");
	const auto rects = laycue::slotRects(*layout, 1920, 1080, 5000);
	for (const RectF &r : rects)
		CHECK(r.w > 0 && r.h > 0);
	CHECK(laycue::slotRects(*layout, 0, 0, 10).empty());
}

TEST_CASE("layouts: a two-part split can give the first part more or less room")
{
	using namespace laycue;
	const LayoutTemplate *side = findTemplate("side-by-side");
	const LayoutTemplate *big = findTemplate("main-and-side");
	const LayoutTemplate *stacked = findTemplate("stacked");
	CHECK(side && big && stacked);
	CHECK(hasSplit(*side) && hasSplit(*big) && hasSplit(*stacked));
	CHECK(!hasSplit(*findTemplate("four-grid")) && !hasSplit(*findTemplate("full")) &&
	      !hasSplit(*findTemplate("picture-in-picture")));
	CHECK_EQ(defaultSplit(*side), 50);
	CHECK_EQ(defaultSplit(*big), 67);

	const LayoutTemplate wide = withSplit(*side, 70);
	CHECK_NEAR(wide.shapes[0].w, 0.7, 1e-9);
	CHECK_NEAR(wide.shapes[1].x, 0.7, 1e-9);
	CHECK_NEAR(wide.shapes[1].w, 0.3, 1e-9);
	const std::vector<RectF> rects = slotRects(wide, 1920, 1080, 0);
	CHECK_NEAR(rects[0].w, 1344, 1e-6);
	CHECK_NEAR(rects[1].x, 1344, 1e-6);
	CHECK_NEAR(rects[0].w + rects[1].w, 1920, 1e-6); // nothing lost or doubled

	const LayoutTemplate tall = withSplit(*stacked, 30);
	CHECK_NEAR(tall.shapes[0].h, 0.3, 1e-9);
	CHECK_NEAR(tall.shapes[1].y, 0.3, 1e-9);

	CHECK_NEAR(withSplit(*side, 5).shapes[0].w, kMinSplit / 100.0, 1e-9);         // clamped
	CHECK_NEAR(withSplit(*side, 99).shapes[0].w, kMaxSplit / 100.0, 1e-9);        // clamped
	CHECK_NEAR(withSplit(*side, 0).shapes[0].w, 0.5, 1e-9);                       // 0 = the template's own
	CHECK_NEAR(withSplit(*findTemplate("four-grid"), 70).shapes[0].w, 0.5, 1e-9); // can't be split: unchanged
}
