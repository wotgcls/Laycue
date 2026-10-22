#include "core/layouts.hpp"

#include <algorithm>

namespace laycue {

const std::vector<LayoutTemplate> &layoutTemplates()
{
	static const std::vector<LayoutTemplate> all = {
		{"full", "Laycue.Layout.Full", "Laycue.Layout.FullHint", {{0, 0, 1, 1, "Laycue.Slot.Main"}}},
		{"side-by-side",
		 "Laycue.Layout.SideBySide",
		 "Laycue.Layout.SideBySideHint",
		 {{0, 0, 0.5, 1, "Laycue.Slot.Left"}, {0.5, 0, 0.5, 1, "Laycue.Slot.Right"}}},
		{"main-and-side",
		 "Laycue.Layout.MainAndSide",
		 "Laycue.Layout.MainAndSideHint",
		 {{0, 0, 2.0 / 3.0, 1, "Laycue.Slot.Main"}, {2.0 / 3.0, 0, 1.0 / 3.0, 1, "Laycue.Slot.Side"}}},
		{"picture-in-picture",
		 "Laycue.Layout.Pip",
		 "Laycue.Layout.PipHint",
		 {{0, 0, 1, 1, "Laycue.Slot.Main"}, {0.68, 0.66, 0.30, 0.30, "Laycue.Slot.Small"}}},
		{"stacked",
		 "Laycue.Layout.Stacked",
		 "Laycue.Layout.StackedHint",
		 {{0, 0, 1, 0.5, "Laycue.Slot.Top"}, {0, 0.5, 1, 0.5, "Laycue.Slot.Bottom"}}},
		{"four-grid",
		 "Laycue.Layout.Grid",
		 "Laycue.Layout.GridHint",
		 {{0, 0, 0.5, 0.5, "Laycue.Slot.One"},
		  {0.5, 0, 0.5, 0.5, "Laycue.Slot.Two"},
		  {0, 0.5, 0.5, 0.5, "Laycue.Slot.Three"},
		  {0.5, 0.5, 0.5, 0.5, "Laycue.Slot.Four"}}},
	};
	return all;
}

const LayoutTemplate *findTemplate(const std::string &id)
{
	for (const LayoutTemplate &layout : layoutTemplates()) {
		if (id == layout.id)
			return &layout;
	}
	return nullptr;
}

namespace {
// Two parts that share the canvas along one axis: the second starts where the first ends.
bool splitsHorizontally(const LayoutTemplate &t)
{
	return t.shapes.size() == 2 && t.shapes[0].h > 0.9999 && t.shapes[1].h > 0.9999 && t.shapes[0].x < 0.0001 &&
	       t.shapes[0].w + t.shapes[1].w > 0.9999 && t.shapes[1].x + t.shapes[1].w > 0.9999 &&
	       t.shapes[1].x > 0.0001;
}
bool splitsVertically(const LayoutTemplate &t)
{
	return t.shapes.size() == 2 && t.shapes[0].w > 0.9999 && t.shapes[1].w > 0.9999 && t.shapes[0].y < 0.0001 &&
	       t.shapes[0].h + t.shapes[1].h > 0.9999 && t.shapes[1].y + t.shapes[1].h > 0.9999 &&
	       t.shapes[1].y > 0.0001;
}
} // namespace

bool hasSplit(const LayoutTemplate &layout)
{
	return splitsHorizontally(layout) || splitsVertically(layout);
}

int defaultSplit(const LayoutTemplate &layout)
{
	if (splitsHorizontally(layout))
		return int(layout.shapes[0].w * 100.0 + 0.5);
	if (splitsVertically(layout))
		return int(layout.shapes[0].h * 100.0 + 0.5);
	return 0;
}

LayoutTemplate withSplit(const LayoutTemplate &layout, int split)
{
	if (split <= 0 || !hasSplit(layout))
		return layout;
	const double first = std::min(kMaxSplit, std::max(kMinSplit, split)) / 100.0;
	LayoutTemplate out = layout;
	if (splitsHorizontally(layout)) {
		out.shapes[0].w = first;
		out.shapes[1].x = first;
		out.shapes[1].w = 1.0 - first;
	} else {
		out.shapes[0].h = first;
		out.shapes[1].y = first;
		out.shapes[1].h = 1.0 - first;
	}
	return out;
}

std::vector<RectF> slotRects(const LayoutTemplate &layout, double canvasWidth, double canvasHeight, double gap)
{
	std::vector<RectF> rects;
	if (canvasWidth <= 0 || canvasHeight <= 0)
		return rects;
	gap = std::max(0.0, std::min(gap, std::min(canvasWidth, canvasHeight) / 8.0));
	const double half = gap / 2.0;
	for (const SlotShape &slot : layout.shapes) {
		RectF r{slot.x * canvasWidth, slot.y * canvasHeight, slot.w * canvasWidth, slot.h * canvasHeight};
		// Shrink each side by half a gap where it touches another slot, a full gap at the canvas edge
		// for the outer border; simply inset every slot by `half` and add `half` at the canvas edge.
		const double left = slot.x <= 0.0001 ? gap : half;
		const double top = slot.y <= 0.0001 ? gap : half;
		const double right = slot.x + slot.w >= 0.9999 ? gap : half;
		const double bottom = slot.y + slot.h >= 0.9999 ? gap : half;
		r.x += left;
		r.y += top;
		r.w -= left + right;
		r.h -= top + bottom;
		rects.push_back(r);
	}
	return rects;
}

} // namespace laycue
