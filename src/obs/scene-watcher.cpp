#include "obs/scene-watcher.hpp"

#include <obs-frontend-api.h>
#include <plugin-support.h>

namespace laycue {

namespace {

// Signals on each scene (and group) source that change what the dock shows.
constexpr const char *kSceneSignals[] = {"item_add", "item_remove", "item_visible", "reorder", "refresh"};

// Signals on OBS's global signal handler.
constexpr const char *kGlobalSignals[] = {"source_rename"};

bool connectGroups(obs_scene_t *, obs_sceneitem_t *item, void *param)
{
	if (item && obs_sceneitem_is_group(item)) {
		auto *groups = static_cast<std::vector<obs_source_t *> *>(param);
		groups->push_back(obs_sceneitem_get_source(item)); // not a new reference
	}
	return true;
}

} // namespace

SceneWatcher::SceneWatcher(std::function<void()> onChange) : onChange_(std::move(onChange)) {}

SceneWatcher::~SceneWatcher()
{
	detachAll();
}

void SceneWatcher::handleSignal(void *data, calldata_t *)
{
	try {
		auto *self = static_cast<SceneWatcher *>(data);
		if (self && self->onChange_)
			self->onChange_();
	} catch (...) {
		obs_log(LOG_ERROR, "SceneWatcher: error while handling a scene signal");
	}
}

void SceneWatcher::connectSceneSource(obs_source_t *source)
{
	signal_handler_t *handler = obs_source_get_signal_handler(source);
	if (!handler)
		return;

	for (const char *signal : kSceneSignals)
		signal_handler_connect(handler, signal, handleSignal, this);
	watched_.emplace_back(obs_source_get_weak_source(source));
}

void SceneWatcher::attachAll()
{
	detachAll();

	if (signal_handler_t *global = obs_get_signal_handler()) {
		for (const char *signal : kGlobalSignals)
			signal_handler_connect(global, signal, handleSignal, this);
		globalConnected_ = true;
	}

	obs_frontend_source_list scenes = {};
	obs_frontend_get_scenes(&scenes);
	for (size_t i = 0; i < scenes.sources.num; ++i) {
		obs_source_t *sceneSource = scenes.sources.array[i];
		if (!sceneSource)
			continue;
		connectSceneSource(sceneSource);

		// Groups have their own signal handler, so watch them too.
		std::vector<obs_source_t *> groups;
		if (obs_scene_t *scene = obs_scene_from_source(sceneSource))
			obs_scene_enum_items(scene, connectGroups, &groups);
		for (obs_source_t *group : groups) {
			if (group)
				connectSceneSource(group);
		}
	}
	obs_frontend_source_list_free(&scenes);
}

void SceneWatcher::detachAll()
{
	for (const OBSWeakSourceAutoRelease &weak : watched_) {
		OBSSourceAutoRelease source = obs_weak_source_get_source(weak);
		if (!source)
			continue; // already destroyed by OBS, nothing to disconnect
		if (signal_handler_t *handler = obs_source_get_signal_handler(source)) {
			for (const char *signal : kSceneSignals)
				signal_handler_disconnect(handler, signal, handleSignal, this);
		}
	}
	watched_.clear();

	if (globalConnected_) {
		if (signal_handler_t *global = obs_get_signal_handler()) {
			for (const char *signal : kGlobalSignals)
				signal_handler_disconnect(global, signal, handleSignal, this);
		}
		globalConnected_ = false;
	}
}

} // namespace laycue
