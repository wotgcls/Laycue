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
