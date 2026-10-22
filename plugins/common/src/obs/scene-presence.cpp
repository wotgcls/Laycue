#include "obs/scene-presence.hpp"

#include <obs-frontend-api.h>
#include <obs.hpp>

namespace laycue {

ScenePresence presenceOf(const std::string &sourceName)
{
	ScenePresence result;
	try {
		OBSSourceAutoRelease program = obs_frontend_get_current_scene();
		if (program)
			result.programScene = obs_source_get_name(program);

		struct obs_frontend_source_list scenes = {};
		obs_frontend_get_scenes(&scenes);
		for (size_t i = 0; i < scenes.sources.num; ++i) {
			obs_source_t *sceneSource = scenes.sources.array[i];
			obs_scene_t *scene = obs_scene_from_source(sceneSource);
			if (!scene)
				continue;
			const char *sceneName = obs_source_get_name(sceneSource);
			obs_sceneitem_t *item = obs_scene_find_source_recursive(scene, sourceName.c_str());
			if (!item)
				continue;
			if (sceneName)
				result.scenes.push_back(sceneName);
			if (sceneSource == program.Get()) {
				result.inProgramScene = true;
				result.shownInProgram = obs_sceneitem_visible(item);
			}
		}
		obs_frontend_source_list_free(&scenes);
	} catch (...) {
	}
	return result;
}

} // namespace laycue
