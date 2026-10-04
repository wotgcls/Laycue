#include "obs/scene-watcher.hpp"

#include "obs/overlay-items.hpp"

#include <obs-frontend-api.h>
#include <plugin-support.h>

namespace laycue {

namespace {

// Signals on each scene (and group) source that change what the dock shows.
constexpr const char *kSceneSignals[] = {"item_add", "item_remove", "item_visible", "reorder", "refresh"};

// Signals on OBS's global signal handler.
constexpr const char *kGlobalSignals[] = {"source_rename"};

struct GroupRef {
	obs_source_t *source; // not a new reference
	int64_t itemId;
};

bool connectGroups(obs_scene_t *, obs_sceneitem_t *item, void *param)
{
	if (item && obs_sceneitem_is_group(item)) {
		auto *groups = static_cast<std::vector<GroupRef> *>(param);
		groups->push_back({obs_sceneitem_get_source(item), obs_sceneitem_get_id(item)});
	}
	return true;
}

} // namespace

SceneWatcher::SceneWatcher(std::function<void()> onChange, ItemVisibleCallback onItemVisible)
	: onChange_(std::move(onChange)),
	  onItemVisible_(std::move(onItemVisible))
{
}

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

void SceneWatcher::handleItemVisible(void *data, calldata_t *params)
{
	try {
		auto *context = static_cast<Context *>(data);
		if (!context || !context->owner || !context->owner->onItemVisible_)
			return;
		auto *item = static_cast<obs_sceneitem_t *>(calldata_ptr(params, "item"));
		if (!item)
			return;
		const int64_t id = context->groupItemId * kGroupIdStep + obs_sceneitem_get_id(item);
		context->owner->onItemVisible_(context->sceneUuid, id, calldata_bool(params, "visible"));
	} catch (...) {
		obs_log(LOG_ERROR, "SceneWatcher: error while handling an item_visible signal");
	}
}

void SceneWatcher::connectSceneSource(obs_source_t *source, const std::string &sceneUuid, int64_t groupItemId)
{
	signal_handler_t *handler = obs_source_get_signal_handler(source);
	if (!handler)
		return;

	for (const char *signal : kSceneSignals)
		signal_handler_connect(handler, signal, handleSignal, this);

	auto context = std::make_unique<Context>();
	context->owner = this;
	context->sceneUuid = sceneUuid;
	context->groupItemId = groupItemId;
	signal_handler_connect(handler, "item_visible", handleItemVisible, context.get());
	contexts_.push_back(std::move(context));

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
		const char *uuidText = obs_source_get_uuid(sceneSource);
		const std::string sceneUuid = uuidText ? uuidText : "";
		connectSceneSource(sceneSource, sceneUuid, 0);

		// Groups have their own signal handler, so watch them too.
		std::vector<GroupRef> groups;
		if (obs_scene_t *scene = obs_scene_from_source(sceneSource))
			obs_scene_enum_items(scene, connectGroups, &groups);
		for (const GroupRef &group : groups) {
			if (group.source)
				connectSceneSource(group.source, sceneUuid, group.itemId);
		}
	}
	obs_frontend_source_list_free(&scenes);
}

void SceneWatcher::detachAll()
{
	// watched_ and contexts_ were filled together, one entry each per source.
	for (size_t i = 0; i < watched_.size(); ++i) {
		OBSSourceAutoRelease source = obs_weak_source_get_source(watched_[i]);
		if (!source)
			continue; // already destroyed by OBS, nothing to disconnect
		if (signal_handler_t *handler = obs_source_get_signal_handler(source)) {
			for (const char *signal : kSceneSignals)
				signal_handler_disconnect(handler, signal, handleSignal, this);
			if (i < contexts_.size())
				signal_handler_disconnect(handler, "item_visible", handleItemVisible,
							  contexts_[i].get());
		}
	}
	watched_.clear();
	contexts_.clear();

	if (globalConnected_) {
		if (signal_handler_t *global = obs_get_signal_handler()) {
			for (const char *signal : kGlobalSignals)
				signal_handler_disconnect(global, signal, handleSignal, this);
		}
		globalConnected_ = false;
	}
}

} // namespace laycue
