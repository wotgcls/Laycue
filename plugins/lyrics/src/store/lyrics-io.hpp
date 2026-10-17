// The song library and settings of Laycue Lyrics, kept in OBS's plugin config folder (so the same songs are there
// in every scene collection). The first start fills the library with the bundled public-domain hymns.
#pragma once

#include "core/song.hpp"

#include <string>
#include <vector>

namespace laycue::lyrics {

struct LyricsSettings {
	int look = 0;           // slides::Look
	std::string background; // picture for the full-screen look
	std::string lastSong;   // id
};

LyricsSettings loadSettings();
void saveSettings(const LyricsSettings &settings);

std::vector<Song> loadLibrary();
void saveLibrary(const std::vector<Song> &songs);

// A song from a file: OpenLyrics (.xml) or Laycue's text form (.txt). False with `error` set.
bool importSongFile(const std::string &path, Song &out, std::string &error);

} // namespace laycue::lyrics
