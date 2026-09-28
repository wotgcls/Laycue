#include "obs/switcher.hpp"

#include <obs-frontend-api.h>
#include <obs.hpp>
#include <plugin-support.h>

#include <cstring>

namespace laycue {

namespace {

std::string uuidOf(obs_source_t *source)
{
	const char *uuid = source ? obs_source_get_uuid(source) : nullptr;
	return uuid ? std::string(uuid) : std::string();
}

// A new reference to OBS's built-in Cut transition, or null if it can't be found.
obs_source_t *findCutTransition()
{
	obs_frontend_source_list transitions = {};
	obs_frontend_get_transitions(&transitions);
	obs_source_t *cut = nullptr;
	for (size_t i = 0; i < transitions.sources.num && !cut; ++i) {
		obs_source_t *transition = transitions.sources.array[i];
		const char *id = transition ? obs_source_get_unversioned_id(transition) : nullptr;
		if (id && std::strcmp(id, "cut_transition") == 0)
			cut = obs_source_get_ref(transition);
	}
	obs_frontend_source_list_free(&transitions);
	return cut;
}

} // namespace

bool studioModeActive()
{
	return obs_frontend_preview_program_mode_active();
}

std::string programSceneUuid()
{
	OBSSourceAutoRelease scene = obs_frontend_get_current_scene();
	return uuidOf(scene);
}

std::string previewSceneUuid()
{
	if (!studioModeActive())
		return std::string();
	OBSSourceAutoRelease scene = obs_frontend_get_current_preview_scene();
	return uuidOf(scene);
}

bool showInPreview(const std::string &sceneUuid)
{
	if (!studioModeActive())
		return false;
	OBSSourceAutoRelease scene = obs_get_source_by_uuid(sceneUuid.c_str());
	if (!scene)
		return false;
	obs_frontend_set_current_preview_scene(scene);
	return true;
}

bool takeLive(const std::string &sceneUuid, bool instant, std::string &transitionToRestore)
{
	transitionToRestore.clear();
	OBSSourceAutoRelease scene = obs_get_source_by_uuid(sceneUuid.c_str());
	if (!scene)
		return false;

	// For Cut, switch to OBS's Cut transition just for this switch. The caller puts the
	// operator's transition back afterwards so nothing in OBS changes behind their back.
	if (instant) {
		OBSSourceAutoRelease cut = findCutTransition();
		OBSSourceAutoRelease previous = obs_frontend_get_current_transition();
		if (cut && previous && previous.Get() != cut.Get()) {
			const char *name = obs_source_get_name(previous);
			transitionToRestore = name ? name : "";
			obs_frontend_set_current_transition(cut);
		} else if (!cut) {
			obs_log(LOG_WARNING, "Cut transition not found; using the current transition");
		}
	}

	if (studioModeActive()) {
		obs_frontend_set_current_preview_scene(scene);
		obs_frontend_preview_program_trigger_transition();
	} else {
		obs_frontend_set_current_scene(scene);
	}

	return true;
}

void restoreTransition(const std::string &name)
{
	if (name.empty())
		return;
	obs_frontend_source_list transitions = {};
	obs_frontend_get_transitions(&transitions);
	for (size_t i = 0; i < transitions.sources.num; ++i) {
		obs_source_t *transition = transitions.sources.array[i];
		const char *transitionName = transition ? obs_source_get_name(transition) : nullptr;
		if (transitionName && name == transitionName) {
			obs_frontend_set_current_transition(transition);
			break;
		}
	}
	obs_frontend_source_list_free(&transitions);
}

TransitionInfo currentTransition()
{
	TransitionInfo info;
	OBSSourceAutoRelease transition = obs_frontend_get_current_transition();
	if (!transition)
		return info;
	const char *name = obs_source_get_name(transition);
	info.name = name ? name : "";
	if (!obs_transition_fixed(transition))
		info.durationMs = obs_frontend_get_transition_duration();
	return info;
}

} // namespace laycue
