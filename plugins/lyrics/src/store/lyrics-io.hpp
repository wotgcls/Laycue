// The song library and settings of Laycue Lyrics, kept in OBS's plugin config folder (so the same songs are there
// in every scene collection). The first start fills the library with the bundled public-domain hymns.
#pragma once

#include "core/song.hpp"
#include "slides/slide-style.hpp"

#include <string>
#include <vector>

namespace laycue::lyrics {

struct LyricsSettings {
	int templateIndex = 0;                  // which of lyricsTemplates() is in use
	std::vector<slides::SlideStyle> styles; // one per template, with the operator's changes
	std::string lastSong;                   // id
	bool showSongName = true;               // the song's name rides with the lyrics unless switched off
};

LyricsSettings loadSettings();
void saveSettings(const LyricsSettings &settings);

std::vector<Song> loadLibrary();
void saveLibrary(const std::vector<Song> &songs);

// The background Laycue Scripture uses now (its chosen template), so songs can match it. False if Scripture
// isn't installed or has never been set up.
bool scriptureBackground(slides::SlideStyle &out);

// A song from a file: OpenLyrics (.xml) or Laycue's text form (.txt). False with `error` set.
bool importSongFile(const std::string &path, Song &out, std::string &error);

} // namespace laycue::lyrics
