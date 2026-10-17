// What Laycue Scripture remembers between sessions (in OBS's plugin config folder, not the scene collection,
// so saved scriptures stay when you switch collections), and the Bible versions it can find.
#pragma once

#include "core/bible.hpp"

#include <memory>
#include <string>
#include <vector>

namespace laycue::scripture {

struct ScriptureSettings {
	std::string version;            // file name of the chosen version ("kjv.xml")
	int look = 0;                   // slides::Look
	std::string background;         // picture for the full-screen look
	std::vector<std::string> saved; // saved scriptures, as typed references ("John 3:16")
	std::string lastTyped;
};

ScriptureSettings loadSettings();
void saveSettings(const ScriptureSettings &settings);

struct VersionFile {
	std::string file; // file name, unique across both folders
	std::string path; // full path
};

// The bundled versions (data/bibles) and the ones the operator added (config folder), by file name.
std::vector<VersionFile> listVersions();

// Reads and keeps a version; the same file is only read once. Null with `error` set if it can't be read.
std::shared_ptr<const Bible> loadVersion(const VersionFile &version, std::string &error);

// Checks the file is a Bible Laycue can read, then copies it into the config folder. Returns the new entry,
// or an empty one with `error` set.
VersionFile addVersion(const std::string &sourcePath, std::string &error);

} // namespace laycue::scripture
