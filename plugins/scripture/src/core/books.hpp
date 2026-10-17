// The 66 books, their usual short forms, and the typing help: "jn" -> John, "1 cor" -> 1 Corinthians.
// Pure C++17, tested.
#pragma once

#include <string>
#include <vector>

namespace laycue::scripture {

struct BookInfo {
	int number = 0; // 1 Genesis .. 66 Revelation
	std::string name;
	std::vector<std::string> aliases; // lower case, written without spaces or dots ("1cor", "jn")
};

const std::vector<BookInfo> &books();
const BookInfo *bookByNumber(int number);

// Lower case, no spaces, dots or dashes; a leading roman numeral (I, II, III) becomes a digit.
std::string normalizeBookText(const std::string &text);

// Books whose name or a short form starts with what was typed, best first: an exact short form, then the
// order of the Bible ("jo" gives Joshua, Joel, John, Job, ...; "job" gives Job). At most `limit`.
std::vector<int> suggestBooks(const std::string &typed, size_t limit = 5);

} // namespace laycue::scripture
