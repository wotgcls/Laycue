#include "slides/slide-control.hpp"

#include "slides/slide-source.hpp"

#include <obs-frontend-api.h>
#include <obs.hpp>

namespace laycue::slides {

namespace {

std::string str(const char *s)
{
	return s ? std::string(s) : std::string();
}

struct Finder {
	const char *id;
	std::vector<std::string> names;
};

} // namespace

std::vector<std::string> sourcesOf(const char *id)
{
	Finder finder{id, {}};
	obs_enum_sources(
		[](void *param, obs_source_t *source) {
			auto *f = static_cast<Finder *>(param);
			if (str(obs_source_get_unversioned_id(source)) == f->id)
				f->names.push_back(str(obs_source_get_name(source)));
			return true;
		},
		&finder);
	return finder.names;
}

bool addToLiveScene(const std::string &sourceName)
{
	OBSSourceAutoRelease current = obs_frontend_get_current_scene();
	obs_scene_t *scene = current ? obs_scene_from_source(current) : nullptr;
	OBSSourceAutoRelease source = obs_get_source_by_name(sourceName.c_str());
	if (!scene || !source)
		return false;
	if (obs_scene_find_source(scene, sourceName.c_str()))
		return true;
	obs_sceneitem_t *item = obs_scene_add(scene, source);
	if (item)
		obs_sceneitem_set_order(item, OBS_ORDER_MOVE_TOP);
	return item != nullptr;
}

std::string ensureSource(const char *id, const std::string &name, std::string &error)
{
	const std::vector<std::string> existing = sourcesOf(id);
	if (!existing.empty())
		return existing.front();
	std::string candidate = name;
	for (int n = 2;; ++n) {
		OBSSourceAutoRelease other = obs_get_source_by_name(candidate.c_str());
		if (!other)
			break;
		candidate = name + " " + std::to_string(n);
	}
	OBSSourceAutoRelease made = obs_source_create(id, candidate.c_str(), nullptr, nullptr);
	if (!made) {
		error = "OBS could not make the layer.";
		return std::string();
	}
	// Until a scene holds it, nothing else does: add it before the reference above is released.
	if (!addToLiveScene(candidate)) {
		error = "There is no scene to add it to.";
		return std::string();
	}
	return candidate;
}

bool isShown(const std::string &sourceName)
{
	OBSSourceAutoRelease source = obs_get_source_by_name(sourceName.c_str());
	if (!source)
		return false;
	OBSDataAutoRelease settings = obs_source_get_settings(source);
	return settings && obs_data_get_bool(settings, key::shown);
}

void hideOthers(const char *keepId)
{
	for (const char *id : {"laycue_scripture", "laycue_lyrics"}) {
		if (str(id) == str(keepId))
			continue;
		for (const std::string &name : sourcesOf(id)) {
			OBSSourceAutoRelease source = obs_get_source_by_name(name.c_str());
			if (!source)
				continue;
			OBSDataAutoRelease settings = obs_data_create();
			obs_data_set_bool(settings, key::shown, false);
			obs_source_update(source, settings); // only "shown" changes; the words stay
		}
	}
}

bool send(const std::string &sourceName, const Slide &slide)
{
	OBSSourceAutoRelease source = obs_get_source_by_name(sourceName.c_str());
	if (!source)
		return false;
	OBSDataAutoRelease settings = obs_data_create();
	obs_data_set_string(settings, key::text, slide.text.c_str());
	obs_data_set_string(settings, key::caption, slide.caption.c_str());
	obs_data_set_int(settings, key::look, slide.look);
	obs_data_set_string(settings, key::background, slide.background.c_str());
	obs_data_set_bool(settings, key::shown, slide.shown);
	obs_source_update(source, settings);
	return true;
}

} // namespace laycue::slides
