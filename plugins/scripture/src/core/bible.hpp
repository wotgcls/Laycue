// A Bible version read from an XML file: Zefania (<XMLBIBLE><BIBLEBOOK><CHAPTER><VERS>), OpenSong
// (<bible><b><c><v>) and Beblia (<bible><testament><book><chapter><verse>). Pure C++17, tested.
#pragma once

#include "core/reference.hpp"

#include <string>
#include <utility>
#include <vector>

namespace laycue::scripture {

struct Bible {
	std::string name;   // "King James Version"
	std::string abbrev; // "KJV": shown after the reference on screen
	// books[book-1][chapter-1][verse-1]; a missing book or chapter is empty.
	std::vector<std::vector<std::vector<std::string>>> books;

	bool empty() const { return books.empty(); }
	int chapterCount(int book) const;
	int verseCount(int book, int chapter) const;
	std::string verse(int book, int chapter, int verse) const; // "" if it isn't there
};

// Reads the whole file. False with `error` set if it isn't a Bible Laycue knows.
bool parseBible(const std::string &xml, Bible &out, std::string &error);

// A short name made from a long one when the file has none: "World English Bible" -> "WEB".
std::string abbreviate(const std::string &name);

// The verse after or before this one, moving on to the next chapter (and book) at the end.
// False at the very start or end of the Bible.
bool nextVerse(const Bible &bible, Reference &ref);
bool previousVerse(const Bible &bible, Reference &ref);

} // namespace laycue::scripture
