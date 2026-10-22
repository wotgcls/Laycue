// The "Live" and "Up next" rules of the Shots view. Pure logic: no libobs, no Qt.
//
// Shots are identified by a string id (in Release 1, an OBS scene's UUID).
#pragma once

#include <string>

namespace laycue {

struct CueState {
	std::string live; // what is on the stream now
	std::string next; // what is ready to go live; empty = nothing cued
};

enum class PickResult {
	Cued,        // the shot is now Up next
	Uncued,      // the shot was already Up next; tapping it again clears Up next
	AlreadyLive, // the shot is live; nothing changes
};

// The operator tapped a shot.
PickResult pickShot(CueState &state, const std::string &shot);

// Can Go Live or Cut run? Only when something is cued that is not already live.
bool canTake(const CueState &state);

// Go Live or Cut finished: the cued shot is live and Up next clears, so pressing
// Go Live twice can never jump back to the previous shot.
void take(CueState &state);

// OBS changed the live scene (from Laycue, OBS itself, a hotkey or a Stream Deck).
// If the new live shot was cued, Up next clears.
void liveChanged(CueState &state, const std::string &live);

// A shot no longer exists (scene deleted, collection switched).
void shotRemoved(CueState &state, const std::string &shot);

} // namespace laycue
