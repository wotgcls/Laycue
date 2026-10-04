#include "obs/layout-scene.hpp"

#include "core/geometry.hpp"
#include "core/layouts.hpp"
#include "obs/scene-util.hpp"

#include <obs-frontend-api.h>
#include <obs.hpp>
#include <plugin-support.h>

#include <algorithm>

namespace laycue {

namespace {

constexpr const char *kPrefix = "Laycue · ";

// The layout's scene, or null. The returned reference is released by the caller (RAII).
OBSSourceAutoRelease openScene(const Layout &layout)
{
	OBSSourceAutoRelease scene = obs_get_source_by_uuid(layout.sceneUuid.c_str());
	if (!scene && !layout.sceneName.empty())
		scene = obs_get_source_by_name(layout.sceneName.c_str());
	if (scene && !obs_scene_from_source(scene))
		return OBSSourceAutoRelease(nullptr);
	return scene;
}

bool isSlotSource(const Layout &layout, const std::string &uuid)
{
	return !uuid.empty() && std::any_of(layout.sources.begin(), layout.sources.end(),
					    [&](const SlotSource &slot) { return slot.uuid == uuid; });
}

struct StaleCollector {
	const Layout *layout;
	std::vector<obs_sceneitem_t *> stale;
};

bool collectStale(obs_scene_t *, obs_sceneitem_t *item, void *param)
{
	auto *collector = static_cast<StaleCollector *>(param);
	obs_source_t *source = obs_sceneitem_get_source(item);
	if (!isSlotSource(*collector->layout, str(source ? obs_source_get_uuid(source) : nullptr)))
		collector->stale.push_back(item);
	return true;
}

} // namespace

bool fitLayout(const Layout &layout)
{
	OBSSourceAutoRelease sceneSource = openScene(layout);
	const LayoutTemplate *shape = findTemplate(layout.templateId);
	if (!sceneSource || !shape)
		return false;
	obs_scene_t *scene = obs_scene_from_source(sceneSource);
	const std::string ownUuid = str(obs_source_get_uuid(sceneSource));
	const Canvas canvas = readCanvas();
	const std::vector<RectF> rects = slotRects(*shape, canvas.w, canvas.h, 0);

	// Slots are stacked in order, so a later slot (the small picture) sits above an earlier one.
	for (size_t i = 0; i < rects.size() && i < layout.sources.size(); ++i) {
		const SlotSource &slot = layout.sources[i];
		if (slot.uuid.empty() || slot.uuid == ownUuid)
			continue; // an empty slot, or a scene that would contain itself
		OBSSourceAutoRelease source = obs_get_source_by_uuid(slot.uuid.c_str());
		if (!source)
			continue;
		obs_sceneitem_t *item = itemForSource(scene, slot.uuid);
		if (!item)
			item = obs_scene_add(scene, source);
		if (!item)
			continue;
		double w = 0, h = 0;
		sourceSize(source, canvas, w, h);
		const Placement placement = layout.fit == FitMode::Whole ? fitInsideWindow(w, h, rects[i])
									 : fillWindow(w, h, rects[i]);
		placeItem(item, placement.box, placement.crop);
		obs_sceneitem_set_visible(item, true);
		obs_sceneitem_set_order(item, OBS_ORDER_MOVE_TOP);
	}

	StaleCollector stale{&layout, {}};
	obs_scene_enum_items(scene, collectStale, &stale);
	for (obs_sceneitem_t *item : stale.stale)
		obs_sceneitem_remove(item);
	return true;
}

LayoutBuildResult buildLayoutScene(const Layout &input)
{
	LayoutBuildResult result;
	result.layout = input;
	Layout &layout = result.layout;

	if (!findTemplate(layout.templateId)) {
		result.error = "Laycue doesn't know this layout shape.";
		return result;
	}

	OBSSourceAutoRelease sceneSource = openScene(layout);
	if (!sceneSource) {
		const std::string name = freeSourceName(std::string(kPrefix) + layout.name);
		obs_scene_t *created = obs_scene_create(name.c_str());
		if (!created) {
			result.error = "OBS couldn't make a scene for this layout.";
			return result;
		}
		sceneSource = OBSSourceAutoRelease(obs_source_get_ref(obs_scene_get_source(created)));
		obs_scene_release(created);
	}
	layout.sceneUuid = str(obs_source_get_uuid(sceneSource));
	layout.sceneName = str(obs_source_get_name(sceneSource));

	result.ok = fitLayout(layout);
	if (!result.ok)
		result.error = "OBS couldn't set up the layout scene.";
	return result;
}

bool layoutSceneExists(const Layout &layout)
{
	OBSSourceAutoRelease sceneSource = openScene(layout);
	return sceneSource != nullptr;
}

bool renameLayoutScene(const Layout &layout, const std::string &newName)
{
	OBSSourceAutoRelease sceneSource = openScene(layout);
	if (!sceneSource || str(obs_source_get_name(sceneSource)).rfind(kPrefix, 0) != 0)
		return false;
	const std::string wanted = std::string(kPrefix) + newName;
	if (str(obs_source_get_name(sceneSource)) == wanted)
		return true;
	obs_source_set_name(sceneSource, freeSourceName(wanted).c_str());
	return true;
}

bool removeLayoutScene(const Layout &layout)
{
	OBSSourceAutoRelease sceneSource = openScene(layout);
	if (!sceneSource)
		return false;
	// Only ever a scene Laycue made: its name starts with "Laycue · ".
	if (str(obs_source_get_name(sceneSource)).rfind(kPrefix, 0) != 0)
		return false;
	obs_source_remove(sceneSource);
	return true;
}

} // namespace laycue
