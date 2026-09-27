// Listens to OBS signals that change what the dock shows (items added, removed, shown,
// hidden, reordered, renamed) and reports "something changed" through a callback.
//
// The callback can be called on ANY thread. It must only schedule work, never touch Qt
// widgets directly.
#pragma once

#include <obs.hpp>

#include <functional>
#include <vector>

namespace laycue {

class SceneWatcher {
public:
	explicit SceneWatcher(std::function<void()> onChange);
	~SceneWatcher();

	SceneWatcher(const SceneWatcher &) = delete;
	SceneWatcher &operator=(const SceneWatcher &) = delete;

	// Disconnects everything, then connects to every current scene and group.
	// Call from the UI thread whenever the scene list may have changed.
	void attachAll();

	// Disconnects everything. Safe to call more than once.
	void detachAll();

private:
	static void handleSignal(void *data, calldata_t *params);
	void connectSceneSource(obs_source_t *source);

	std::function<void()> onChange_;
	// Weak references, so a scene deleted by OBS never leaves us holding a dangling pointer.
	std::vector<OBSWeakSourceAutoRelease> watched_;
	bool globalConnected_ = false;
};

} // namespace laycue
