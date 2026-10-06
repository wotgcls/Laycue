// How the Clock panel talks to the "Laycue Clock" sources in OBS: find them, write their settings, start, pause and
// reset them, read what they show, and add them to scenes. UI thread only.
#pragma once

#include "core/clock-settings.hpp"

#include <string>
#include <vector>

namespace laycue::clock {

struct ClockStatus {
	bool found = false;
	bool running = false;
	double seconds = 0.0; // the number on screen
	int level = 0;        // 0 normal, 1 warning, 2 over
	bool finished = false;
	bool overtime = false;
	bool countdown = true;
};

std::vector<std::string> listClockSources();

// Writes every setting into the source (the clock starts counting again only if what it counts changed).
bool applyToSource(const std::string &sourceName, const ClockSettings &settings);

bool startClock(const std::string &sourceName);
bool pauseClock(const std::string &sourceName);
bool toggleClock(const std::string &sourceName);
bool resetClock(const std::string &sourceName);
bool adjustClock(const std::string &sourceName, int seconds);
ClockStatus statusOf(const std::string &sourceName);

// Makes the source if it doesn't exist and puts it in the scene that is live now (on top). Returns its name, or ""
// with `error` set.
std::string addToCurrentScene(const std::string &preferredName, std::string &error);
// Puts the source in every scene that doesn't have it yet. Returns how many scenes got it.
int addToAllScenes(const std::string &sourceName);

} // namespace laycue::clock
