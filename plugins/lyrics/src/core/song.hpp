// Songs for Laycue Lyrics: written as plain text (a blank line starts a new part, a line like "Chorus" names
// it), read from OpenLyrics XML (what OpenLP and others export), found by name as you type, and cut into
// two-line slides. Pure C++17, tested.
#pragma once

#include <string>
#include <vector>

namespace laycue::lyrics {

struct Section {
	std::string label; // "Verse 1", "Chorus"; may be empty
	std::vector<std::string> lines;
	bool operator==(const Section &o) const { return label == o.label && lines == o.lines; }
};

struct Song {
	std::string id;
	std::string title;
	std::string author;
	std::vector<Section> sections;
	bool operator==(const Song &o) const
	{
		return id == o.id && title == o.title && author == o.author && sections == o.sections;
	}
};

struct LyricSlide {
	std::string label; // the part it comes from
	std::string text;  // its lines, joined with "\n"
};

// "Verse 1", "Chorus", "[Bridge]", "Pre-Chorus 2", "Refrain", "V1", "C:"... as a part's name, or "" if the line
// is a lyric.
std::string sectionLabel(const std::string &line);

// The text the editor shows and saves. A part's name is its first line when it has one.
Song parseSongText(const std::string &title, const std::string &author, const std::string &text);
std::string songText(const Song &song);

// Every part cut into slides of `linesPerSlide` lines (a last odd line gets its own slide).
std::vector<LyricSlide> slidesFor(const Song &song, int linesPerSlide = 2);

// A song file in Laycue's text form: optional "Title:" and "Author:" lines at the top, then the song.
// `fallbackTitle` (the file's name) is used when there is no Title line.
Song parseSongFile(const std::string &text, const std::string &fallbackTitle);

// An OpenLyrics file (<song><properties><titles>... <lyrics><verse name="v1"><lines>a<br/>b</lines>).
bool parseOpenLyrics(const std::string &xml, Song &out, std::string &error);

// The best matches for what was typed: title starts with it, then a word of the title starts with it, then
// it appears in the title, then it appears in any lyric line (so a song can be found by the words it sings).
// Case and punctuation don't matter. Indexes into `songs`.
std::vector<size_t> searchSongs(const std::vector<Song> &songs, const std::string &query, size_t limit = 6);

// "amazing-grace", made unique against `taken`.
std::string makeSongId(const std::string &title, const std::vector<std::string> &taken);

} // namespace laycue::lyrics
