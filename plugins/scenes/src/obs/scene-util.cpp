#include "obs/scene-util.hpp"

#include "obs/overlay-items.hpp"

#include <obs.hpp>

namespace laycue {

std::string str(const char *text)
{
	return text ? std::string(text) : std::string();
}

// An unused source name based on `wanted`.
std::string freeSourceName(const std::string &wanted)
{
	std::string name = wanted;
	for (int n = 2;; ++n) {
		OBSSourceAutoRelease existing = obs_get_source_by_name(name.c_str());
		if (!existing)
			return name;
		name = wanted + " " + std::to_string(n);
	}
}

namespace {

struct FindByUuid {
	std::string uuid;
	obs_sceneitem_t *found = nullptr;
};

bool matchItem(obs_scene_t *, obs_sceneitem_t *item, void *param)
{
	auto *find = static_cast<FindByUuid *>(param);
	obs_source_t *source = obs_sceneitem_get_source(item);
	if (source && str(obs_source_get_uuid(source)) == find->uuid) {
		find->found = item;
		return false;
	}
	return true;
}

} // namespace

// The item in `scene` that shows the source with this UUID (not a new reference), or null.
obs_sceneitem_t *itemForSource(obs_scene_t *scene, const std::string &uuid)
{
	FindByUuid find{uuid};
	obs_scene_enum_items(scene, matchItem, &find);
	return find.found;
}

Canvas readCanvas()
{
	Canvas canvas;
	double w = 0, h = 0;
	canvasSize(w, h);
	if (w > 0 && h > 0) {
		canvas.w = w;
		canvas.h = h;
	}
	return canvas;
}

// Places an item with a top-left position, a stretched box and a crop.
void placeItem(obs_sceneitem_t *item, const RectF &box, const Crop &crop)
{
	obs_sceneitem_defer_update_begin(item);
	obs_sceneitem_set_alignment(item, OBS_ALIGN_LEFT | OBS_ALIGN_TOP);
	obs_sceneitem_set_rot(item, 0.0f);
	vec2 scale;
	vec2_set(&scale, 1.0f, 1.0f);
	obs_sceneitem_set_scale(item, &scale);
	obs_sceneitem_crop c = {crop.left, crop.top, crop.right, crop.bottom};
	obs_sceneitem_set_crop(item, &c);
	obs_sceneitem_set_bounds_type(item, OBS_BOUNDS_STRETCH);
	obs_sceneitem_set_bounds_alignment(item, OBS_ALIGN_CENTER);
	vec2 pos, bounds;
	vec2_set(&pos, float(box.x), float(box.y));
	vec2_set(&bounds, float(box.w), float(box.h));
	obs_sceneitem_set_pos(item, &pos);
	obs_sceneitem_set_bounds(item, &bounds);
	obs_sceneitem_defer_update_end(item);
}

// Source size, or a 16:9 guess at canvas size if OBS doesn't know yet.
void sourceSize(obs_source_t *source, const Canvas &canvas, double &w, double &h)
{
	w = obs_source_get_width(source);
	h = obs_source_get_height(source);
	if (w <= 0 || h <= 0) {
		w = canvas.w;
		h = canvas.h;
	}
}

} // namespace laycue
