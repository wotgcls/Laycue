#include "core/books.hpp"

#include <algorithm>
#include <cctype>

namespace laycue::scripture {

namespace {

std::vector<BookInfo> makeBooks()
{
	// Name, then short forms (written as normalizeBookText leaves them).
	const std::vector<std::pair<const char *, std::vector<std::string>>> table = {
		{"Genesis", {"gen", "ge", "gn"}},
		{"Exodus", {"ex", "exo", "exod"}},
		{"Leviticus", {"lev", "le", "lv"}},
		{"Numbers", {"num", "nu", "nm", "nb"}},
		{"Deuteronomy", {"deut", "deu", "dt"}},
		{"Joshua", {"josh", "jos", "jsh"}},
		{"Judges", {"judg", "jdg", "jg", "jdgs"}},
		{"Ruth", {"rth", "ru"}},
		{"1 Samuel", {"1sam", "1sa", "1sm", "1s"}},
		{"2 Samuel", {"2sam", "2sa", "2sm", "2s"}},
		{"1 Kings", {"1kgs", "1ki", "1kin", "1k"}},
		{"2 Kings", {"2kgs", "2ki", "2kin", "2k"}},
		{"1 Chronicles", {"1chr", "1ch", "1chron"}},
		{"2 Chronicles", {"2chr", "2ch", "2chron"}},
		{"Ezra", {"ezr"}},
		{"Nehemiah", {"neh", "ne"}},
		{"Esther", {"est", "esth", "es"}},
		{"Job", {"jb"}},
		{"Psalms", {"ps", "psa", "psalm", "pss", "psm"}},
		{"Proverbs", {"prov", "pro", "prv", "pr"}},
		{"Ecclesiastes", {"eccl", "ecc", "ec", "qoh"}},
		{"Song of Solomon", {"song", "sos", "so", "songofsongs", "canticles", "sng"}},
		{"Isaiah", {"isa", "is"}},
		{"Jeremiah", {"jer", "je", "jr"}},
		{"Lamentations", {"lam", "la"}},
		{"Ezekiel", {"ezek", "eze", "ezk"}},
		{"Daniel", {"dan", "da", "dn"}},
		{"Hosea", {"hos", "ho"}},
		{"Joel", {"jl"}},
		{"Amos", {"am"}},
		{"Obadiah", {"obad", "ob"}},
		{"Jonah", {"jnh", "jon"}},
		{"Micah", {"mic", "mc"}},
		{"Nahum", {"nah", "na"}},
		{"Habakkuk", {"hab", "hb"}},
		{"Zephaniah", {"zeph", "zep", "zp"}},
		{"Haggai", {"hag", "hg"}},
		{"Zechariah", {"zech", "zec", "zc"}},
		{"Malachi", {"mal", "ml"}},
		{"Matthew", {"matt", "mt", "mat"}},
		{"Mark", {"mrk", "mk", "mr"}},
		{"Luke", {"luk", "lk"}},
		{"John", {"jn", "jhn", "joh"}},
		{"Acts", {"act", "ac"}},
		{"Romans", {"rom", "ro", "rm"}},
		{"1 Corinthians", {"1cor", "1co"}},
		{"2 Corinthians", {"2cor", "2co"}},
		{"Galatians", {"gal", "ga"}},
		{"Ephesians", {"eph", "ephes"}},
		{"Philippians", {"phil", "php", "pp"}},
		{"Colossians", {"col", "co"}},
		{"1 Thessalonians", {"1thess", "1th", "1thes"}},
		{"2 Thessalonians", {"2thess", "2th", "2thes"}},
		{"1 Timothy", {"1tim", "1ti", "1tm"}},
		{"2 Timothy", {"2tim", "2ti", "2tm"}},
		{"Titus", {"tit", "ti"}},
		{"Philemon", {"philem", "phm", "pm"}},
		{"Hebrews", {"heb"}},
		{"James", {"jas", "jm"}},
		{"1 Peter", {"1pet", "1pe", "1pt", "1p"}},
		{"2 Peter", {"2pet", "2pe", "2pt", "2p"}},
		{"1 John", {"1jn", "1jhn", "1jo", "1joh"}},
		{"2 John", {"2jn", "2jhn", "2jo", "2joh"}},
		{"3 John", {"3jn", "3jhn", "3jo", "3joh"}},
		{"Jude", {"jud", "jd"}},
		{"Revelation", {"rev", "re", "rv", "revelations"}},
	};
	std::vector<BookInfo> out;
	int number = 1;
	for (const auto &row : table)
		out.push_back({number++, row.first, row.second});
	return out;
}

} // namespace

const std::vector<BookInfo> &books()
{
	static const std::vector<BookInfo> all = makeBooks();
	return all;
}

const BookInfo *bookByNumber(int number)
{
	if (number < 1 || number > int(books().size()))
		return nullptr;
	return &books()[size_t(number - 1)];
}

std::string normalizeBookText(const std::string &text)
{
	std::string lowered;
	for (char c : text)
		lowered += char(std::tolower(static_cast<unsigned char>(c)));
	const size_t first = lowered.find_first_not_of(" \t");
	lowered = first == std::string::npos ? std::string() : lowered.substr(first);
	// "I John", "II Kings", "III John": a roman numeral as its own word in front is the book's number.
	std::string prefix;
	for (const auto &roman : {std::make_pair(std::string("iii"), '3'), std::make_pair(std::string("ii"), '2'),
				  std::make_pair(std::string("i"), '1')}) {
		const std::string &r = roman.first;
		if (lowered.size() > r.size() && lowered.compare(0, r.size(), r) == 0 &&
		    (lowered[r.size()] == ' ' || lowered[r.size()] == '.')) {
			prefix = std::string(1, roman.second);
			lowered = lowered.substr(r.size());
			break;
		}
	}
	std::string s = prefix;
	for (char c : lowered) {
		if (std::isspace(static_cast<unsigned char>(c)) || c == '.' || c == '-' || c == '_')
			continue;
		s += c;
	}
	return s;
}

std::vector<int> suggestBooks(const std::string &typed, size_t limit)
{
	const std::string t = normalizeBookText(typed);
	std::vector<int> out;
	if (t.empty())
		return out;
	// 1. An exact short form or name ("job", "jn", "phil").
	for (const BookInfo &b : books()) {
		if (normalizeBookText(b.name) == t ||
		    std::find(b.aliases.begin(), b.aliases.end(), t) != b.aliases.end())
			out.push_back(b.number);
	}
	// 2. Names that start with it, in Bible order; then short forms that start with it.
	for (const BookInfo &b : books()) {
		if (normalizeBookText(b.name).rfind(t, 0) == 0 &&
		    std::find(out.begin(), out.end(), b.number) == out.end())
			out.push_back(b.number);
	}
	for (const BookInfo &b : books()) {
		for (const std::string &alias : b.aliases) {
			if (alias.rfind(t, 0) == 0 && std::find(out.begin(), out.end(), b.number) == out.end())
				out.push_back(b.number);
		}
	}
	if (out.size() > limit)
		out.resize(limit);
	return out;
}

} // namespace laycue::scripture
