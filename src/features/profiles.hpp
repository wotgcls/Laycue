// Profiles: a whole Laycue setup (labels, overlay rules, preferences) saved under a name and kept
// for every scene collection. Matching is by name, so a setup made for one service works in
// another scene collection that has sources and scenes with the same names. Pushbacks are not
// part of a profile: they depend on that collection's cameras.
#pragma once

#include "core/config.hpp"

#include <string>
#include <vector>

namespace laycue {

class App;

// Saved profile names, sorted.
std::vector<std::string> listProfiles();

// Saves the current setup as a profile. False if the name can't be used or the file can't be written.
bool saveProfile(const App &app, const std::string &name);

// Applies a saved profile. `summary` says what happened in plain words (or what went wrong).
bool loadProfile(App &app, const std::string &name, std::string &summary);

bool deleteProfile(const std::string &name);

// The same, but to and from any file, for moving a setup to another computer.
bool exportSetup(const App &app, const std::string &path);
bool importSetup(App &app, const std::string &path, std::string &summary);

} // namespace laycue
