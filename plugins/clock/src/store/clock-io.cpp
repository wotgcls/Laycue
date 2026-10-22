#include "store/clock-io.hpp"

#include <obs-module.h>
#include <obs.hpp>
#include <plugin-support.h>
#include <util/platform.h>

namespace laycue::clock {

namespace {

std::string str(const char *s)
{
	return s ? std::string(s) : std::string();
}

std::string filePath()
{
	char *path = obs_module_config_path("clock.json");
	std::string result = str(path);
	bfree(path);
	return result;
}

} // namespace

PanelState loadPanelState()
{
	PanelState state;
	try {
		const std::string path = filePath();
		if (path.empty())
			return state;
		OBSDataAutoRelease root = obs_data_create_from_json_file(path.c_str());
		if (!root)
			return state;
		ClockSettings &s = state.settings;
		s.mode = int(obs_data_get_int(root, "mode"));
		s.duration = str(obs_data_get_string(root, "duration"));
		s.target = str(obs_data_get_string(root, "target"));
		if (obs_data_has_user_value(root, "label"))
			s.label = str(obs_data_get_string(root, "label"));
		s.design = int(obs_data_get_int(root, "design"));
		s.background = int(obs_data_get_int(root, "background"));
		if (obs_data_has_user_value(root, "warn"))
			s.warnSeconds = int(obs_data_get_int(root, "warn"));
		if (obs_data_has_user_value(root, "overtime"))
			s.overtime = obs_data_get_bool(root, "overtime");
		if (obs_data_has_user_value(root, "text"))
			s.text = uint32_t(obs_data_get_int(root, "text"));
		if (obs_data_has_user_value(root, "accent"))
			s.accent = uint32_t(obs_data_get_int(root, "accent"));
		if (obs_data_has_user_value(root, "panel"))
			s.panel = uint32_t(obs_data_get_int(root, "panel"));
		if (obs_data_has_user_value(root, "panelOpacity"))
			s.panelOpacity = int(obs_data_get_int(root, "panelOpacity"));
		if (obs_data_has_user_value(root, "align"))
			s.align = int(obs_data_get_int(root, "align"));
		if (obs_data_has_user_value(root, "vertical"))
			s.vertical = int(obs_data_get_int(root, "vertical"));
		if (obs_data_has_user_value(root, "size"))
			s.sizePercent = int(obs_data_get_int(root, "size"));
		sanitize(s);
		state.targetSource = str(obs_data_get_string(root, "targetSource"));
	} catch (...) {
		obs_log(LOG_WARNING, "Laycue Clock: its saved settings could not be read; using the defaults");
		state = PanelState();
	}
	return state;
}

bool savePanelState(const PanelState &state)
{
	try {
		const std::string path = filePath();
		if (path.empty())
			return false;
		const ClockSettings &s = state.settings;
		OBSDataAutoRelease root = obs_data_create();
		obs_data_set_int(root, "mode", s.mode);
		obs_data_set_string(root, "duration", s.duration.c_str());
		obs_data_set_string(root, "target", s.target.c_str());
		obs_data_set_string(root, "label", s.label.c_str());
		obs_data_set_int(root, "design", s.design);
		obs_data_set_int(root, "background", s.background);
		obs_data_set_int(root, "warn", s.warnSeconds);
		obs_data_set_bool(root, "overtime", s.overtime);
		obs_data_set_int(root, "text", s.text);
		obs_data_set_int(root, "accent", s.accent);
		obs_data_set_int(root, "panel", s.panel);
		obs_data_set_int(root, "panelOpacity", s.panelOpacity);
		obs_data_set_int(root, "align", s.align);
		obs_data_set_int(root, "vertical", s.vertical);
		obs_data_set_int(root, "size", s.sizePercent);
		obs_data_set_string(root, "targetSource", state.targetSource.c_str());
		char *dir = obs_module_config_path("");
		if (dir) {
			os_mkdirs(dir);
			bfree(dir);
		}
		return obs_data_save_json_safe(root, path.c_str(), "tmp", "bak");
	} catch (...) {
		return false;
	}
}

} // namespace laycue::clock
