// Small OBS scene helpers shared by pushbacks and layouts. Call from the UI thread.
#pragma once

#include "core/geometry.hpp"

#include <obs.h>

#include <string>

namespace laycue {

std::string str(const char *text);

// An unused source name based on `wanted`.
std::string freeSourceName(const std::string &wanted);

// The item in `scene` that shows the source with this UUID (not a new reference), or null.
obs_sceneitem_t *itemForSource(obs_scene_t *scene, const std::string &uuid);

struct Canvas {
	double w = 1920;
	double h = 1080;
};

// The canvas size OBS is using (1920x1080 only if OBS can't say).
Canvas readCanvas();

// Places an item with a top-left position, a stretched box and a crop.
void placeItem(obs_sceneitem_t *item, const RectF &box, const Crop &crop);

// Source size, or the canvas size if OBS doesn't know yet (a camera not drawn yet).
void sourceSize(obs_source_t *source, const Canvas &canvas, double &w, double &h);

} // namespace laycue
