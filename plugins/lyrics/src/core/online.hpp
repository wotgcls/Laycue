// Songs found online (LRCLIB, a free community lyrics site the owner chose): the address to ask, and turning an
// answer into a Laycue song. The network and JSON parts live in store/online-search. Pure C++17, tested.
#pragma once

#include "core/song.hpp"

#include <string>
#include <vector>

namespace laycue::lyrics {

struct OnlineSong {
	long long id = 0;
	std::string title;
	std::string artist;
	std::string album;
	int durationSeconds = 0;
	bool instrumental = false;
	std::string plainLyrics;
	std::string syncedLyrics; // "[00:12.34] line" form, used when there are no plain lyrics
};

// https://lrclib.net/api/search?q=... with the query written safely into the address.
std::string onlineSearchUrl(const std::string &query);

// "[00:12.34] Amazing grace" -> "Amazing grace"; lines that were only a time mark become blank.
std::string plainFromSynced(const std::string &synced);

// The words as Laycue parts (blank lines between verses become parts). Title and artist come along.
Song songFromOnline(const OnlineSong &found);

// What is worth showing from a search: songs with words, one of each title and artist, at most `limit`.
std::vector<OnlineSong> usefulResults(const std::vector<OnlineSong> &all, size_t limit = 12);

} // namespace laycue::lyrics
