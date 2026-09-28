#include "core/geometry.hpp"

#include <algorithm>
#include <cmath>

namespace laycue {

RectF intersect(const RectF &a, const RectF &b)
{
	const double left = std::max(a.x, b.x);
	const double top = std::max(a.y, b.y);
	const double right = std::min(a.right(), b.right());
	const double bottom = std::min(a.bottom(), b.bottom());
	if (right <= left || bottom <= top)
		return {};
	return {left, top, right - left, bottom - top};
}

bool overlapsMeaningfully(const RectF &a, const RectF &b)
{
	const RectF overlap = intersect(a, b);
	if (overlap.empty() || overlap.w < kMinOverlapPixels || overlap.h < kMinOverlapPixels)
		return false;
	const double smaller = std::min(a.area(), b.area());
	return smaller > 0 && overlap.area() >= kMinOverlapShare * smaller;
}

RectF sourceRectToCanvas(double sourceWidth, double sourceHeight, const Crop &crop, const Affine &box,
			 const RectF &part)
{
	// The part of the source the item shows (everything except the crop).
	const RectF shown{static_cast<double>(crop.left), static_cast<double>(crop.top),
			  sourceWidth - crop.left - crop.right, sourceHeight - crop.top - crop.bottom};
	const RectF visible = intersect(part, shown);
	if (visible.empty() || shown.empty())
		return {};

	// Corners as fractions of the shown area, then through the box transform.
	const double u0 = (visible.x - shown.x) / shown.w;
	const double u1 = (visible.right() - shown.x) / shown.w;
	const double v0 = (visible.y - shown.y) / shown.h;
	const double v1 = (visible.bottom() - shown.y) / shown.h;
	const double us[2] = {u0, u1};
	const double vs[2] = {v0, v1};

	double minX = 0, minY = 0, maxX = 0, maxY = 0;
	bool first = true;
	for (double u : us) {
		for (double v : vs) {
			const double x = box.a * u + box.c * v + box.tx;
			const double y = box.b * u + box.d * v + box.ty;
			if (first) {
				minX = maxX = x;
				minY = maxY = y;
				first = false;
			} else {
				minX = std::min(minX, x);
				maxX = std::max(maxX, x);
				minY = std::min(minY, y);
				maxY = std::max(maxY, y);
			}
		}
	}
	return {minX, minY, maxX - minX, maxY - minY};
}

Placement fillWindow(double sourceWidth, double sourceHeight, const RectF &window)
{
	Placement result;
	result.box = window;
	if (sourceWidth <= 0 || sourceHeight <= 0 || window.empty())
		return result;

	const double windowShape = window.w / window.h;
	const double sourceShape = sourceWidth / sourceHeight;
	if (sourceShape > windowShape) {
		// Source is wider than the window: trim the sides evenly.
		const double keep = sourceHeight * windowShape;
		const double trim = (sourceWidth - keep) / 2;
		result.crop.left = static_cast<int>(std::floor(trim));
		result.crop.right = static_cast<int>(std::floor(trim));
	} else if (sourceShape < windowShape) {
		// Source is taller than the window: trim top and bottom evenly.
		const double keep = sourceWidth / windowShape;
		const double trim = (sourceHeight - keep) / 2;
		result.crop.top = static_cast<int>(std::floor(trim));
		result.crop.bottom = static_cast<int>(std::floor(trim));
	}
	return result;
}

Placement fitInsideWindow(double sourceWidth, double sourceHeight, const RectF &window)
{
	Placement result;
	result.box = window;
	if (sourceWidth <= 0 || sourceHeight <= 0 || window.empty())
		return result;

	const double scale = std::min(window.w / sourceWidth, window.h / sourceHeight);
	const double w = sourceWidth * scale;
	const double h = sourceHeight * scale;
	result.box = {window.x + (window.w - w) / 2, window.y + (window.h - h) / 2, w, h};
	return result;
}

RectF imagePlacementOnCanvas(double imageWidth, double imageHeight, double canvasWidth, double canvasHeight)
{
	if (imageWidth <= 0 || imageHeight <= 0 || canvasWidth <= 0 || canvasHeight <= 0)
		return {0, 0, canvasWidth, canvasHeight};
	const double imageShape = imageWidth / imageHeight;
	const double canvasShape = canvasWidth / canvasHeight;
	if (std::fabs(imageShape - canvasShape) / canvasShape <= 0.02)
		return {0, 0, canvasWidth, canvasHeight};
	return fitInsideWindow(imageWidth, imageHeight, {0, 0, canvasWidth, canvasHeight}).box;
}

RectF windowOnCanvas(const Rect &windowInImage, double imageWidth, double imageHeight, const RectF &imagePlacement)
{
	if (imageWidth <= 0 || imageHeight <= 0)
		return {};
	const double sx = imagePlacement.w / imageWidth;
	const double sy = imagePlacement.h / imageHeight;
	const double left = std::round(imagePlacement.x + windowInImage.x * sx);
	const double top = std::round(imagePlacement.y + windowInImage.y * sy);
	const double right = std::round(imagePlacement.x + (windowInImage.x + windowInImage.width) * sx);
	const double bottom = std::round(imagePlacement.y + (windowInImage.y + windowInImage.height) * sy);
	return {left, top, right - left, bottom - top};
}

} // namespace laycue
