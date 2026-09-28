// The one top-level object: it owns Laycue's Config and keeps it saved with the OBS scene
// collection. Everything else (the panel, the overlay engine, pushbacks) reads and changes
// the config through it, on the UI thread.
#pragma once

#include "core/config.hpp"

#include <obs-frontend-api.h>

#include <functional>
#include <vector>

namespace laycue {

class App {
public:
	App();
	~App();

	App(const App &) = delete;
	App &operator=(const App &) = delete;

	const Config &config() const { return config_; }

	// Change the config, then tell every listener. The only way to change it.
	void modify(const std::function<void(Config &)> &change);

	// Replace everything (import, load a profile).
	void replace(Config config);

	// Called after the config changed or was loaded from a scene collection (UI thread).
	// Returns an id for removeListener.
	int addListener(std::function<void()> listener);
	void removeListener(int id);

	// Called for every OBS frontend event, on the UI thread.
	void handleFrontendEvent(enum obs_frontend_event event);

private:
	static void saveCallback(obs_data_t *saveData, bool saving, void *param);
	void load(obs_data_t *saveData);
	void save(obs_data_t *saveData) const;
	void notify();

	Config config_;
	struct Listener {
		int id;
		std::function<void()> callback;
	};
	std::vector<Listener> listeners_;
	int nextListenerId_ = 1;
};

} // namespace laycue
