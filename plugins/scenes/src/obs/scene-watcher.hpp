// Listens to OBS signals that change what the dock shows (items added, removed, shown,
// hidden, reordered, renamed) and reports "something changed" through a callback.
//
// The callbacks can be called on ANY thread. They must only schedule work, never touch Qt
// widgets directly.
#pragma once

#include <obs.hpp>

#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <vector>

namespace laycue {

class SceneWatcher {
public:
	// Called with (top-level scene uuid, item id as OverlayItem::itemId, now visible) whenever an
	// item is shown or hidden.
	using ItemVisibleCallback = std::function<void(const std::string &, int64_t, bool)>;

	explicit SceneWatcher(std::function<void()> onChange, ItemVisibleCallback onItemVisible = nullptr);
	~SceneWatcher();

	SceneWatcher(const SceneWatcher &) = delete;
	SceneWatcher &operator=(const SceneWatcher &) = delete;

	// Disconnects everything, then connects to every current scene and group.
	// Call from the UI thread whenever the scene list may have changed.
	void attachAll();

	// Disconnects everything. Safe to call more than once.
	void detachAll();

private:
	// Which top-level scene (and group, if any) a watched source belongs to.
	struct Context {
		SceneWatcher *owner = nullptr;
		std::string sceneUuid;
		int64_t groupItemId = 0;
	};

	static void handleSignal(void *data, calldata_t *params);
	static void handleItemVisible(void *data, calldata_t *params);
	void connectSceneSource(obs_source_t *source, const std::string &sceneUuid, int64_t groupItemId);

	std::function<void()> onChange_;
	ItemVisibleCallback onItemVisible_;
	std::vector<std::unique_ptr<Context>> contexts_;
	// Weak references, so a scene deleted by OBS never leaves us holding a dangling pointer.
	std::vector<OBSWeakSourceAutoRelease> watched_;
	bool globalConnected_ = false;
};

} // namespace laycue
