#include "obs/overlay-items.hpp"

#include "obs/pixel-reader.hpp"

#include <obs-frontend-api.h>
#include <graphics/matrix4.h>
#include <obs.hpp>
#include <plugin-support.h>

#include <algorithm>
#include <cmath>

namespace laycue {

namespace {

constexpr int kSpotAlphaThreshold = 16; // a pixel counts as visible at or above this alpha

std::string str(const char *text)
{
	return text ? std::string(text) : std::string();
}

RectF boxBounds(obs_sceneitem_t *item)
{
	matrix4 m;
	obs_sceneitem_get_box_transform(item, &m);
	const double xs[4] = {m.t.x, m.t.x + m.x.x, m.t.x + m.y.x, m.t.x + m.x.x + m.y.x};
	const double ys[4] = {m.t.y, m.t.y + m.x.y, m.t.y + m.y.y, m.t.y + m.x.y + m.y.y};
	const double minX = *std::min_element(xs, xs + 4);
	const double maxX = *std::max_element(xs, xs + 4);
	const double minY = *std::min_element(ys, ys + 4);
	const double maxY = *std::max_element(ys, ys + 4);
	return {minX, minY, maxX - minX, maxY - minY};
}

struct Collector {
	std::string sceneUuid;
	std::string sceneName;
	int64_t groupId = 0;
	std::vector<OverlayItem> *out = nullptr;
};

bool collect(obs_scene_t *, obs_sceneitem_t *item, void *param);

void addItem(Collector &c, obs_sceneitem_t *item)
{
	obs_source_t *source = obs_sceneitem_get_source(item); // not a new reference
	if (!source)
		return;
	if (obs_sceneitem_is_group(item)) {
		Collector inner = c;
		inner.groupId = obs_sceneitem_get_id(item);
		obs_sceneitem_group_enum_items(item, collect, &inner);
		return;
	}
	if (obs_source_get_type(source) == OBS_SOURCE_TYPE_SCENE)
		return; // a scene used as a source is not an overlay

	OverlayItem o;
	o.sceneUuid = c.sceneUuid;
	o.sceneName = c.sceneName;
	o.itemId = c.groupId * kGroupIdStep + obs_sceneitem_get_id(item);
	o.sourceUuid = str(obs_source_get_uuid(source));
	o.sourceName = str(obs_source_get_name(source));
	o.kind = kindFromTypeId(str(obs_source_get_id(source)));
	o.visible = obs_sceneitem_visible(item);
	o.box = boxBounds(item);
	c.out->push_back(std::move(o));
}

bool collect(obs_scene_t *, obs_sceneitem_t *item, void *param)
{
	if (item)
		addItem(*static_cast<Collector *>(param), item);
	return true;
}

void collectScene(obs_source_t *sceneSource, std::vector<OverlayItem> &out)
{
	obs_scene_t *scene = obs_scene_from_source(sceneSource);
	if (!scene)
		return;
	Collector c;
	c.sceneUuid = str(obs_source_get_uuid(sceneSource));
	c.sceneName = str(obs_source_get_name(sceneSource));
	c.out = &out;
	obs_scene_enum_items(scene, collect, &c);
}

// Finds the item again. The returned pointer is not a new reference; use it right away.
obs_sceneitem_t *findItem(obs_source_t *sceneSource, int64_t itemId)
{
	obs_scene_t *scene = obs_scene_from_source(sceneSource);
	if (!scene)
		return nullptr;
	const int64_t groupId = itemId / kGroupIdStep;
	const int64_t innerId = itemId % kGroupIdStep;
	if (groupId == 0)
		return obs_scene_find_sceneitem_by_id(scene, innerId);

	obs_sceneitem_t *group = obs_scene_find_sceneitem_by_id(scene, groupId);
	if (!group || !obs_sceneitem_is_group(group))
		return nullptr;
	obs_scene_t *groupScene = obs_sceneitem_group_get_scene(group);
	return groupScene ? obs_scene_find_sceneitem_by_id(groupScene, innerId) : nullptr;
}

} // namespace

std::vector<OverlayItem> readAllItems()
{
	std::vector<OverlayItem> items;
	obs_frontend_source_list scenes = {};
	obs_frontend_get_scenes(&scenes);
	for (size_t i = 0; i < scenes.sources.num; ++i) {
		if (obs_source_t *scene = scenes.sources.array[i])
			collectScene(scene, items);
	}
	obs_frontend_source_list_free(&scenes);
	return items;
}

std::vector<OverlayItem> readSceneItems(const std::string &sceneUuid)
{
	std::vector<OverlayItem> items;
	OBSSourceAutoRelease scene = obs_get_source_by_uuid(sceneUuid.c_str());
	if (scene)
		collectScene(scene, items);
	return items;
}

bool setItemVisible(const std::string &sceneUuid, int64_t itemId, bool visible)
{
	OBSSourceAutoRelease scene = obs_get_source_by_uuid(sceneUuid.c_str());
	if (!scene)
		return false;
	obs_sceneitem_t *item = findItem(scene, itemId);
	if (!item)
		return false;
	obs_sceneitem_set_visible(item, visible);
	return true;
}

RectF measureSpot(const std::string &sceneUuid, int64_t itemId)
{
	OBSSourceAutoRelease scene = obs_get_source_by_uuid(sceneUuid.c_str());
	if (!scene)
		return {};
	obs_sceneitem_t *item = findItem(scene, itemId);
	if (!item)
		return {};

	const RectF fallback = boxBounds(item);
	obs_source_t *source = obs_sceneitem_get_source(item);
	if (!source)
		return fallback;

	// Draw the source and find where its pixels are, in the source's own coordinates.
	const std::string uuid = str(obs_source_get_uuid(source));
	const RgbaImage picture = captureSource(uuid);
	if (picture.empty())
		return fallback;

	int minX = picture.width, minY = picture.height, maxX = -1, maxY = -1;
	for (int y = 0; y < picture.height; ++y) {
		const uint8_t *row = picture.pixels.data() + static_cast<size_t>(y) * picture.width * 4;
		for (int x = 0; x < picture.width; ++x) {
			if (row[x * 4 + 3] >= kSpotAlphaThreshold) {
				minX = std::min(minX, x);
				maxX = std::max(maxX, x);
				minY = std::min(minY, y);
				maxY = std::max(maxY, y);
			}
		}
	}
	if (maxX < minX || maxY < minY)
		return fallback; // nothing visible: a text source with no text, say

	obs_sceneitem_crop crop = {};
	obs_sceneitem_get_crop(item, &crop);
	matrix4 m;
	obs_sceneitem_get_box_transform(item, &m);
	const Affine box{m.x.x, m.x.y, m.y.x, m.y.y, m.t.x, m.t.y};

	const RectF part{double(minX), double(minY), double(maxX - minX + 1), double(maxY - minY + 1)};
	const RectF mapped = sourceRectToCanvas(picture.width, picture.height,
						Crop{crop.left, crop.top, crop.right, crop.bottom}, box, part);
	return mapped.empty() ? fallback : mapped;
}

void canvasSize(double &width, double &height)
{
	obs_video_info video = {};
	if (obs_get_video_info(&video)) {
		width = video.base_width;
		height = video.base_height;
	} else {
		width = height = 0;
	}
}

} // namespace laycue
