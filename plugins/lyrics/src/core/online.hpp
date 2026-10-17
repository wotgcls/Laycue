// Songs found online (LRCLIB, a free community lyrics site the owner chose): the address to ask, and turning an
// answer into a Laycue song. The network and JSON parts live in store/online-search. Pure C++17, tested.
#pragma once

#include "core/song.hpp"

#include <string>
#include <vector>

namespace laycue::lyrics {

enum class OnlineSource { Lrclib = 0, LyricsOvh = 1 };

struct OnlineSong {
	OnlineSource source = OnlineSource::Lrclib;
	bool needsWords = false; // lyrics.ovh lists songs first; the words are fetched when one is picked
	long long id = 0;
	std::string title;
	std::string artist;
	std::string album;
	int durationSeconds = 0;
	bool instrumental = false;
	std::string plainLyrics;
	std::string syncedLyrics; // "[00:12.34] line" form, used when there are no plain lyrics
};

// Text written safely into a web address ("&" -> "%26").
std::string percentEncode(const std::string &text);

// https://lrclib.net/api/search?q=... with the query written safely into the address.
std::string onlineSearchUrl(const std::string &query);

// The second source, lyrics.ovh: its song list for a name, and the words of one song.
std::string ovhSuggestUrl(const std::string &query);
std::string ovhLyricsUrl(const std::string &artist, const std::string &title);

// lyrics.ovh words without its French heading line ("Paroles de la chanson ... par ..."), Windows line ends,
// or runs of blank lines.
std::string cleanOvhLyrics(const std::string &text);

// "[00:12.34] Amazing grace" -> "Amazing grace"; lines that were only a time mark become blank.
std::string plainFromSynced(const std::string &synced);

// The words as Laycue parts (blank lines between verses become parts). Title and artist come along.
Song songFromOnline(const OnlineSong &found);

// Everything a search found, none left out: a song listed without words is marked so its words are looked for in
// the other source when it is picked.
std::vector<OnlineSong> listResults(const std::vector<OnlineSong> &all);

} // namespace laycue::lyrics
