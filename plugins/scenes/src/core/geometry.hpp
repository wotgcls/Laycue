// Rectangle and placement maths shared by overlays and pushbacks. Pure logic: no libobs, no Qt.
#pragma once

#include "core/detect.hpp"

namespace laycue {

// A rectangle in canvas pixels (or source pixels, where the function says so).
struct RectF {
	double x = 0;
	double y = 0;
	double w = 0;
	double h = 0;

	double right() const { return x + w; }
	double bottom() const { return y + h; }
	double area() const { return w > 0 && h > 0 ? w * h : 0; }
	bool empty() const { return w <= 0 || h <= 0; }
};

RectF intersect(const RectF &a, const RectF &b);

// Overlaps that don't count (CLAUDE.md, Exclusivity): smaller than 2% of the smaller
// spot's area, or under 8 px in either direction.
constexpr double kMinOverlapShare = 0.02;
constexpr double kMinOverlapPixels = 8.0;
bool overlapsMeaningfully(const RectF &a, const RectF &b);

// A 2D transform as OBS reports it for a scene item's box: it maps the unit square
// (0..1, 0..1) of the displayed item to canvas pixels.  x' = a*x + c*y + tx, y' = b*x + d*y + ty.
struct Affine {
	double a = 1, b = 0, c = 0, d = 1, tx = 0, ty = 0;
};

struct Crop {
	int left = 0;
	int top = 0;
	int right = 0;
	int bottom = 0;
};

// Where a rectangle of a source's own pixels ends up on the canvas, given how the scene
// item shows the source (its crop and box transform). The result is the bounding box of the
// mapped corners, clipped to the part of the source the item actually shows. Empty if none of
// `part` is visible.
RectF sourceRectToCanvas(double sourceWidth, double sourceHeight, const Crop &crop, const Affine &box,
			 const RectF &part);

// Where a subject goes inside a window: its position and size on the canvas, and how much to
// crop off the source (in source pixels). Show it with OBS_BOUNDS_STRETCH and bounds = placement size.
struct Placement {
	RectF box;
	Crop crop;
};

// Fill: the subject covers the whole window, cropped evenly to the window's shape.
Placement fillWindow(double sourceWidth, double sourceHeight, const RectF &window);

// Whole: the subject is scaled to fit inside the window and centred. Nothing is cropped.
Placement fitInsideWindow(double sourceWidth, double sourceHeight, const RectF &window);

// Where the pushback picture sits on the canvas: the whole canvas if its shape is within 2% of
// the canvas's, otherwise scaled to fit and centred.
RectF imagePlacementOnCanvas(double imageWidth, double imageHeight, double canvasWidth, double canvasHeight);

// The detected window (in image pixels) as a rectangle on the canvas.
RectF windowOnCanvas(const Rect &windowInImage, double imageWidth, double imageHeight, const RectF &imagePlacement);

} // namespace laycue
