// Layout templates: split screen, picture in picture and so on. A template is a list of slots,
// each a share of the canvas; Laycue puts one source in each slot. Pure logic: no libobs, no Qt.
#pragma once

#include "core/geometry.hpp"

#include <string>
#include <vector>

namespace laycue {

// A slot as fractions of the canvas (0..1).
struct SlotShape {
	double x = 0;
	double y = 0;
	double w = 1;
	double h = 1;
	const char *nameKey = ""; // locale key for the slot's label, e.g. "Laycue.Slot.Main"
};

struct LayoutTemplate {
	const char *id;      // saved in the config, never changes
	const char *nameKey; // locale key
	const char *hintKey; // locale key: when to use it
	std::vector<SlotShape> shapes;
};

// All built-in templates, in the order they are offered.
const std::vector<LayoutTemplate> &layoutTemplates();

// The template with this id, or null.
const LayoutTemplate *findTemplate(const std::string &id);

// Split screens (two parts side by side or one above the other) can have the first part wider or taller.
// `split` is the first part's share in percent. 0 means the template's own proportion.
constexpr int kMinSplit = 20;
constexpr int kMaxSplit = 80;
constexpr int kSplitStep = 5;
bool hasSplit(const LayoutTemplate &layout);
int defaultSplit(const LayoutTemplate &layout); // the template's own first-part share, in percent (0 if none)
// The template with its first part given `split` percent (clamped to kMinSplit..kMaxSplit). A template
// that can't be split, or split == 0, comes back unchanged.
LayoutTemplate withSplit(const LayoutTemplate &layout, int split);

// Each slot's rectangle on a canvas. `gap` is the space left between slots and around the edge
// (canvas pixels); slots never overlap unless the template says so (picture in picture).
std::vector<RectF> slotRects(const LayoutTemplate &layout, double canvasWidth, double canvasHeight, double gap);

} // namespace laycue
