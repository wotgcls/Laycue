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

// Each slot's rectangle on a canvas. `gap` is the space left between slots and around the edge
// (canvas pixels); slots never overlap unless the template says so (picture in picture).
std::vector<RectF> slotRects(const LayoutTemplate &layout, double canvasWidth, double canvasHeight, double gap);

} // namespace laycue
