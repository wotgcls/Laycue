#include "obs/scene-reader.hpp"

#include <obs-frontend-api.h>
#include <obs.hpp>

#include <algorithm>

namespace laycue {

namespace {

std::string safeString(const char *text)
{
	return text ? std::string(text) : std::string();
}

std::string sourceUuid(obs_source_t *source)
{
	return source ? safeString(obs_source_get_uuid(source)) : std::string();
}

ItemInfo readItem(obs_sceneitem_t *item);

bool collectItem(obs_scene_t *, obs_sceneitem_t *item, void *param)
{
	auto *items = static_cast<std::vector<ItemInfo> *>(param);
	if (item)
		items->push_back(readItem(item));
	return true; // keep enumerating
}

ItemInfo readItem(obs_sceneitem_t *item)
{
	ItemInfo info;
	info.id = obs_sceneitem_get_id(item);
	info.visible = obs_sceneitem_visible(item);

	obs_source_t *source = obs_sceneitem_get_source(item); // not a new reference
	if (source) {
		info.sourceName = safeString(obs_source_get_name(source));
		info.sourceUuid = sourceUuid(source);
		info.typeId = safeString(obs_source_get_id(source));
		info.kind = kindFromTypeId(info.typeId);
	}

	if (obs_sceneitem_is_group(item)) {
		obs_sceneitem_group_enum_items(item, collectItem, &info.children);
		// OBS enumerates bottom-to-top; show top first like the Sources list.
		std::reverse(info.children.begin(), info.children.end());
	}
	return info;
}

SceneInfo readScene(obs_source_t *sceneSource)
{
	SceneInfo scene;
	scene.name = safeString(obs_source_get_name(sceneSource));
	scene.uuid = sourceUuid(sceneSource);

	obs_scene_t *obsScene = obs_scene_from_source(sceneSource); // not a new reference
	if (obsScene) {
		obs_scene_enum_items(obsScene, collectItem, &scene.items);
		std::reverse(scene.items.begin(), scene.items.end());
	}
	return scene;
}

} // namespace

Snapshot readSnapshot()
{
	Snapshot snapshot;

	obs_video_info video = {};
	if (obs_get_video_info(&video)) {
		snapshot.canvasWidth = video.base_width;
		snapshot.canvasHeight = video.base_height;
	}

	{
		OBSSourceAutoRelease program = obs_frontend_get_current_scene();
		snapshot.programSceneUuid = sourceUuid(program);
	}

	snapshot.studioMode = obs_frontend_preview_program_mode_active();
	if (snapshot.studioMode) {
		OBSSourceAutoRelease preview = obs_frontend_get_current_preview_scene();
		snapshot.previewSceneUuid = sourceUuid(preview);
	}

	obs_frontend_source_list scenes = {};
	obs_frontend_get_scenes(&scenes);
	for (size_t i = 0; i < scenes.sources.num; ++i) {
		obs_source_t *sceneSource = scenes.sources.array[i];
		if (sceneSource)
			snapshot.scenes.push_back(readScene(sceneSource));
	}
	obs_frontend_source_list_free(&scenes);

	return snapshot;
}

} // namespace laycue
