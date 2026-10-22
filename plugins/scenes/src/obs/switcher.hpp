// Switching scenes in OBS for the Shots view: cue a scene, take it live with the OBS
// transition (Go Live) or instantly (Cut). UI thread only.
#pragma once

#include <string>
#include <vector>

namespace laycue {

bool studioModeActive();

// UUIDs of the scene on air and (in studio mode) the preview scene. Empty if none.
std::string programSceneUuid();
std::string previewSceneUuid();

// Studio mode only: show the scene in OBS's Preview. Returns false if the scene is gone.
bool showInPreview(const std::string &sceneUuid);

// Put the scene on air. `instant` = Cut (no transition); otherwise the OBS transition
// currently selected is used. Returns false if the scene no longer exists.
//
// For Cut, OBS's Cut transition is selected just for this switch. OBS finishes the switch
// on a later frame, so the operator's own transition can't be put back straight away:
// `transitionToRestore` gets its name, and the caller passes it to restoreTransition()
// once OBS reports the transition has stopped.
bool takeLive(const std::string &sceneUuid, bool instant, std::string &transitionToRestore);

// Selects the OBS transition with this name again (does nothing if it no longer exists).
void restoreTransition(const std::string &name);

// The transition Go Live will use, e.g. "Fade" and its duration in milliseconds.
// durationMs is 0 for transitions without a duration (Cut, stingers).
struct TransitionInfo {
	std::string name;
	int durationMs = 0;
};
TransitionInfo currentTransition();

// The transitions in OBS's Scene Transitions list, in order; `laycue` marks Laycue's own (Move, Push...).
struct TransitionChoice {
	std::string name;
	bool laycue = false;
};
std::vector<TransitionChoice> transitionChoices();

// Laycue's transition kinds that are not in OBS's Scene Transitions list yet, as {id, display name}.
struct MissingTransition {
	std::string id;
	std::string name;
};
std::vector<MissingTransition> missingLaycueTransitions();

} // namespace laycue
