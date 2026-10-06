#include "obs/source-control.hpp"

#include "obs/title-source.hpp"

#include <obs-frontend-api.h>
#include <obs-module.h>
#include <obs.hpp>
#include <plugin-support.h>

namespace laycue::titles {

namespace {

std::string str(const char *s)
{
	return s ? std::string(s) : std::string();
}

uint32_t toObsColour(const Rgba &c)
{
	return 0xFF000000u | (uint32_t(c.b) << 16) | (uint32_t(c.g) << 8) | uint32_t(c.r);
}

bool collect(void *param, obs_source_t *source)
{
	if (str(obs_source_get_id(source)) == kTitleSourceId)
		static_cast<std::vector<std::string> *>(param)->push_back(str(obs_source_get_name(source)));
	return true;
}

OBSSourceAutoRelease openTitle(const std::string &name)
{
	if (name.empty())
		return OBSSourceAutoRelease(nullptr);
	OBSSourceAutoRelease source = obs_get_source_by_name(name.c_str());
	if (source && str(obs_source_get_id(source)) != kTitleSourceId)
		return OBSSourceAutoRelease(nullptr);
	return source;
}

// Puts the source in the scene (if it isn't there yet) at the top of the stack.
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

std::vector<std::string> listTitleSources()
{
	std::vector<std::string> names;
	obs_enum_sources(collect, &names);
	return names;
}

bool applyToSource(const std::string &sourceName, const Library &lib, const std::string &styleId,
		   const TitleContent &content)
{
	OBSSourceAutoRelease source = openTitle(sourceName);
	if (!source)
		return false;
	std::string baseId;
	resolveStyle(lib, styleId, &baseId);

	OBSDataAutoRelease settings = obs_data_create();
	obs_data_set_string(settings, key::templateId, baseId.c_str());
	obs_data_set_string(settings, key::line1, content.line1.c_str());
	obs_data_set_string(settings, key::line2, content.line2.c_str());
	obs_data_set_string(settings, key::tag, content.tag.c_str());
	obs_data_set_string(settings, key::note, content.note.c_str());
	obs_data_set_string(settings, key::logo, content.logo.c_str());
	obs_data_set_int(settings, key::logoCut, content.logoCut);
	obs_data_set_bool(settings, key::logoStacked, content.logoStacked);
	obs_data_set_bool(settings, key::logoMarkSecond, content.logoMarkSecond);
	{
		OBSDataArrayAutoRelease rows = obs_data_array_create();
		for (const Row &r : content.rows) {
			OBSDataAutoRelease item = obs_data_create();
			obs_data_set_string(item, "label", r.label.c_str());
			obs_data_set_string(item, "value", r.value.c_str());
			obs_data_array_push_back(rows, item);
		}
		obs_data_set_array(settings, key::rows, rows);
	}
	obs_data_set_bool(settings, key::accentOn, lib.useBrand);
	obs_data_set_int(settings, key::accent, toObsColour(lib.brand));
	obs_data_set_int(settings, key::hold, lib.holdOverride);
	obs_data_set_int(settings, key::speed, lib.speed);
	const Placement placement = placementFor(lib, styleId);
	obs_data_set_int(settings, key::align, placement.align);
	obs_data_set_int(settings, key::sizePercent, placement.percent);
	obs_data_set_int(settings, key::fit, placement.fit);
	obs_data_set_int(settings, key::fitWidth, placement.width);

	OBSDataAutoRelease custom = obs_data_create();
	if (isCustomId(styleId)) {
		if (const Overrides *o = findCustom(lib, customNameOf(styleId))) {
			for (const auto &kv : toMap(*o))
				obs_data_set_string(custom, kv.first.c_str(), kv.second.c_str());
		}
	}
	// Always written (empty when the style is a built-in one) so switching back clears old styling.
	obs_data_set_obj(settings, key::custom, custom);
	obs_source_update(source, settings);
	return true;
}

bool showOnAir(const std::string &sourceName)
{
	OBSSourceAutoRelease source = openTitle(sourceName);
	if (!source)
		return false;
	calldata_t cd;
	calldata_init(&cd);
	const bool ok = proc_handler_call(obs_source_get_proc_handler(source), "laycue_title_show", &cd);
	calldata_free(&cd);
	return ok;
}

bool hideFromAir(const std::string &sourceName)
{
	OBSSourceAutoRelease source = openTitle(sourceName);
	if (!source)
		return false;
	calldata_t cd;
	calldata_init(&cd);
	const bool ok = proc_handler_call(obs_source_get_proc_handler(source), "laycue_title_hide", &cd);
	calldata_free(&cd);
	return ok;
}

TitleStatus statusOf(const std::string &sourceName)
{
	TitleStatus status;
	OBSSourceAutoRelease source = openTitle(sourceName);
	if (!source)
		return status;
	status.found = true;
	calldata_t cd;
	calldata_init(&cd);
	if (proc_handler_call(obs_source_get_proc_handler(source), "laycue_title_state", &cd)) {
		const long long phase = calldata_int(&cd, "phase");
		status.phase = phase == 1   ? TitleStatus::Phase::In
			       : phase == 2 ? TitleStatus::Phase::Hold
			       : phase == 3 ? TitleStatus::Phase::Out
			       : phase == 4 ? TitleStatus::Phase::Cued
					    : TitleStatus::Phase::Hidden;
		status.holdRemaining = calldata_float(&cd, "hold_remaining");
	}
	calldata_free(&cd);
	return status;
}

std::string addToCurrentScene(const std::string &preferredName, std::string &error)
{
	OBSSourceAutoRelease current = obs_frontend_get_current_scene();
	obs_scene_t *scene = current ? obs_scene_from_source(current) : nullptr;
	if (!scene) {
		error = "There is no scene to add it to.";
		return std::string();
	}

	std::string name = preferredName.empty() ? str(obs_module_text("Laycue.Titles.SourceName")) : preferredName;
	OBSSourceAutoRelease existing = openTitle(name);
	if (!existing) {
		// A source with that name that is not ours: use the next free name.
		std::string candidate = name;
		for (int n = 2;; ++n) {
			OBSSourceAutoRelease other = obs_get_source_by_name(candidate.c_str());
			if (!other)
				break;
			candidate = name + " " + std::to_string(n);
		}
		name = candidate;
		existing = OBSSourceAutoRelease(obs_source_create(kTitleSourceId, name.c_str(), nullptr, nullptr));
		if (!existing) {
			error = "OBS could not make the title layer.";
			return std::string();
		}
	}
	addToScene(scene, existing);
	return name;
}

int addToAllScenes(const std::string &sourceName)
{
	OBSSourceAutoRelease source = openTitle(sourceName);
	if (!source)
		return 0;
	int added = 0;
	obs_frontend_source_list scenes = {};
	obs_frontend_get_scenes(&scenes);
	for (size_t i = 0; i < scenes.sources.num; ++i) {
		obs_source_t *sceneSource = scenes.sources.array[i];
		const std::string sceneName = str(obs_source_get_name(sceneSource));
		if (addToScene(obs_scene_from_source(sceneSource), source))
			++added;
	}
	obs_frontend_source_list_free(&scenes);
	return added;
}

} // namespace laycue::titles
