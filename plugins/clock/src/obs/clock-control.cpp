#include "obs/clock-control.hpp"

#include "obs/clock-source.hpp"

#include <obs-frontend-api.h>
#include <obs-module.h>
#include <obs.hpp>
#include <plugin-support.h>

namespace laycue::clock {

namespace {

std::string str(const char *s)
{
	return s ? std::string(s) : std::string();
}

long long toObsColour(uint32_t rgb)
{
	return (long long)(0xFF000000u | ((rgb & 0xFF) << 16) | (rgb & 0xFF00) | ((rgb >> 16) & 0xFF));
}

bool collect(void *param, obs_source_t *source)
{
	if (str(obs_source_get_id(source)) == kClockSourceId)
		static_cast<std::vector<std::string> *>(param)->push_back(str(obs_source_get_name(source)));
	return true;
}

OBSSourceAutoRelease openClock(const std::string &name)
{
	if (name.empty())
		return OBSSourceAutoRelease(nullptr);
	OBSSourceAutoRelease source = obs_get_source_by_name(name.c_str());
	if (source && str(obs_source_get_id(source)) != kClockSourceId)
		return OBSSourceAutoRelease(nullptr);
	return source;
}

bool call(const std::string &name, const char *procedure, calldata_t *cd = nullptr)
{
	OBSSourceAutoRelease source = openClock(name);
	if (!source)
		return false;
	calldata_t local;
	calldata_init(&local);
	const bool ok = proc_handler_call(obs_source_get_proc_handler(source), procedure, cd ? cd : &local);
	calldata_free(&local);
	return ok;
}

bool addToScene(obs_scene_t *scene, obs_source_t *source)
{
	if (!scene || !source)
		return false;
	if (obs_scene_find_source(scene, obs_source_get_name(source)))
		return false;
	obs_sceneitem_t *item = obs_scene_add(scene, source);
	if (!item)
		return false;
	obs_sceneitem_set_order(item, OBS_ORDER_MOVE_TOP);
	return true;
}

} // namespace

std::vector<std::string> listClockSources()
{
	std::vector<std::string> names;
	obs_enum_sources(collect, &names);
	return names;
}

bool applyToSource(const std::string &sourceName, const ClockSettings &s)
{
	OBSSourceAutoRelease source = openClock(sourceName);
	if (!source)
		return false;
	OBSDataAutoRelease d = obs_data_create();
	obs_data_set_int(d, key::mode, s.mode);
	obs_data_set_string(d, key::duration, s.duration.c_str());
	obs_data_set_string(d, key::target, s.target.c_str());
	obs_data_set_string(d, key::label, s.label.c_str());
	obs_data_set_int(d, key::design, s.design);
	obs_data_set_int(d, key::background, s.background);
	obs_data_set_int(d, key::warn, s.warnSeconds);
	obs_data_set_bool(d, key::overtime, s.overtime);
	obs_data_set_int(d, key::text, toObsColour(s.text));
	obs_data_set_int(d, key::accent, toObsColour(s.accent));
	obs_data_set_int(d, key::warning, toObsColour(s.warning));
	obs_data_set_int(d, key::over, toObsColour(s.over));
	obs_data_set_int(d, key::panel, toObsColour(s.panel));
	obs_data_set_int(d, key::panelOpacity, s.panelOpacity);
	obs_data_set_int(d, key::align, s.align);
	obs_data_set_int(d, key::vertical, s.vertical);
	obs_data_set_int(d, key::sizePercent, s.sizePercent);
	obs_source_update(source, d);
	return true;
}

bool startClock(const std::string &n)
{
	return call(n, "laycue_clock_start");
}

bool pauseClock(const std::string &n)
{
	return call(n, "laycue_clock_pause");
}

bool toggleClock(const std::string &n)
{
	return call(n, "laycue_clock_toggle");
}

bool resetClock(const std::string &n)
{
	return call(n, "laycue_clock_reset");
}

bool adjustClock(const std::string &n, int seconds)
{
	calldata_t cd;
	calldata_init(&cd);
	calldata_set_int(&cd, "seconds", seconds);
	const bool ok = call(n, "laycue_clock_adjust", &cd);
	calldata_free(&cd);
	return ok;
}

ClockStatus statusOf(const std::string &sourceName)
{
	ClockStatus st;
	OBSSourceAutoRelease source = openClock(sourceName);
	if (!source)
		return st;
	st.found = true;
	calldata_t cd;
	calldata_init(&cd);
	if (proc_handler_call(obs_source_get_proc_handler(source), "laycue_clock_state", &cd)) {
		st.running = calldata_int(&cd, "running") != 0;
		st.seconds = calldata_float(&cd, "seconds");
		st.level = int(calldata_int(&cd, "level"));
		st.finished = calldata_int(&cd, "finished") != 0;
		st.overtime = calldata_int(&cd, "overtime") != 0;
		st.countdown = calldata_int(&cd, "countdown") != 0;
	}
	calldata_free(&cd);
	return st;
}

std::string addToCurrentScene(const std::string &preferredName, std::string &error)
{
	OBSSourceAutoRelease current = obs_frontend_get_current_scene();
	obs_scene_t *scene = current ? obs_scene_from_source(current) : nullptr;
	if (!scene) {
		error = "There is no scene to add it to.";
		return std::string();
	}
	std::string name = preferredName.empty() ? str(obs_module_text("Laycue.Clock.SourceName")) : preferredName;
	OBSSourceAutoRelease existing = openClock(name);
	if (!existing) {
		std::string candidate = name;
		for (int n = 2;; ++n) {
			OBSSourceAutoRelease other = obs_get_source_by_name(candidate.c_str());
			if (!other)
				break;
			candidate = name + " " + std::to_string(n);
		}
		name = candidate;
		existing = OBSSourceAutoRelease(obs_source_create(kClockSourceId, name.c_str(), nullptr, nullptr));
		if (!existing) {
			error = "OBS could not make the clock layer.";
			return std::string();
		}
	}
	addToScene(scene, existing);
	return name;
}

int addToAllScenes(const std::string &sourceName)
{
	OBSSourceAutoRelease source = openClock(sourceName);
	if (!source)
		return 0;
	int added = 0;
	obs_frontend_source_list scenes = {};
	obs_frontend_get_scenes(&scenes);
	for (size_t i = 0; i < scenes.sources.num; ++i)
		if (addToScene(obs_scene_from_source(scenes.sources.array[i]), source))
			++added;
	obs_frontend_source_list_free(&scenes);
	return added;
}

} // namespace laycue::clock
