#include "features/app.hpp"

#include "store/config-io.hpp"

#include <obs.hpp>
#include <plugin-support.h>

#include <algorithm>

namespace laycue {

namespace {
constexpr const char *kSaveKey = "laycue";
}

App::App()
{
	// Start with the starter rule, so a new operator has something sensible.
	config_.rules = defaultRules();
	config_.rulesInitialised = true;
	obs_frontend_add_save_callback(saveCallback, this);
}

App::~App()
{
	obs_frontend_remove_save_callback(saveCallback, this);
}

void App::saveCallback(obs_data_t *saveData, bool saving, void *param)
{
	auto *app = static_cast<App *>(param);
	try {
		if (saving)
			app->save(saveData);
		else
			app->load(saveData);
	} catch (const std::exception &error) {
		obs_log(LOG_ERROR, "Could not %s Laycue's setup: %s", saving ? "save" : "load", error.what());
	} catch (...) {
		obs_log(LOG_ERROR, "Could not %s Laycue's setup", saving ? "save" : "load");
	}
}

void App::save(obs_data_t *saveData) const
{
	OBSData data = configToData(config_);
	obs_data_set_obj(saveData, kSaveKey, data);
}

void App::load(obs_data_t *saveData)
{
	OBSDataAutoRelease data = obs_data_get_obj(saveData, kSaveKey);
	if (data) {
		config_ = configFromData(data);
		if (!config_.rulesInitialised) {
			config_.rules = defaultRules();
			config_.rulesInitialised = true;
		}
	} else {
		// A scene collection Laycue has never seen: start fresh.
		config_ = Config();
		config_.rules = defaultRules();
		config_.rulesInitialised = true;
	}
	notify();
}

void App::modify(const std::function<void(Config &)> &change)
{
	change(config_);
	sanitize(config_);
	notify();
}

void App::replace(Config config)
{
	sanitize(config);
	config_ = std::move(config);
	notify();
}

int App::addListener(std::function<void()> listener)
{
	const int id = nextListenerId_++;
	listeners_.push_back({id, std::move(listener)});
	return id;
}

void App::removeListener(int id)
{
	listeners_.erase(std::remove_if(listeners_.begin(), listeners_.end(),
					[id](const Listener &l) { return l.id == id; }),
			 listeners_.end());
}

void App::notify()
{
	// A listener may add or remove listeners; work on a copy.
	const std::vector<Listener> copy = listeners_;
	for (const Listener &listener : copy) {
		try {
			listener.callback();
		} catch (...) {
			obs_log(LOG_ERROR, "A Laycue listener failed");
		}
	}
}

void App::handleFrontendEvent(enum obs_frontend_event event)
{
	// Loading and saving happen in the save callback. Nothing else to do yet.
	(void)event;
}

} // namespace laycue
