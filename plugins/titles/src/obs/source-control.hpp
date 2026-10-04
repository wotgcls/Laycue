// How the Titles panel talks to the "Laycue Title" sources in OBS: find them, write their settings,
// show and hide them, and add them to scenes. UI thread only.
#pragma once

#include "core/library.hpp"

#include <string>
#include <vector>

namespace laycue::titles {

struct TitleStatus {
	enum class Phase { Hidden, In, Hold, Out };
	bool found = false;
	Phase phase = Phase::Hidden;
	double holdRemaining = -1.0; // seconds before it hides itself; negative = never
	bool onAir() const { return phase != Phase::Hidden; }
};

// Names of every Laycue Title source in this scene collection.
std::vector<std::string> listTitleSources();

// Writes what the panel shows (template, text, brand colour, hold) into the source. Does not show it.
bool applyToSource(const std::string &sourceName, const Library &library, const std::string &styleId,
		   const TitleContent &content);

bool showOnAir(const std::string &sourceName);
bool hideFromAir(const std::string &sourceName);
TitleStatus statusOf(const std::string &sourceName);

// Makes the source if it doesn't exist and puts it in the scene that is live now (on top). Returns the
// source's name, or "" with `error` set.
std::string addToCurrentScene(const std::string &preferredName, std::string &error);

// Puts the source in every scene that doesn't have it yet. Returns how many scenes got it.
int addToAllScenes(const std::string &sourceName);

} // namespace laycue::titles
