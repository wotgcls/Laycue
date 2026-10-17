#include "core/online.hpp"

#include <algorithm>
#include <cctype>
#include <set>
#include <sstream>

namespace laycue::lyrics {

namespace {

std::string lowerTrim(const std::string &s)
{
	std::string out;
	for (char c : s)
		out += char(std::tolower(static_cast<unsigned char>(c)));
	const size_t a = out.find_first_not_of(" \t");
	if (a == std::string::npos)
		return std::string();
	return out.substr(a, out.find_last_not_of(" \t") - a + 1);
}

bool hasWords(const OnlineSong &s)
{
	const auto blank = [](const std::string &t) {
		return std::all_of(t.begin(), t.end(),
				   [](char c) { return std::isspace(static_cast<unsigned char>(c)); });
	};
	return !s.instrumental && (s.needsWords || !blank(s.plainLyrics) || !blank(s.syncedLyrics));
}

} // namespace

std::string percentEncode(const std::string &text)
{
	static const char *hex = "0123456789ABCDEF";
	std::string encoded;
	for (unsigned char c : text) {
		if (std::isalnum(c) || c == '-' || c == '_' || c == '.' || c == '~') {
			encoded += char(c);
		} else {
			encoded += '%';
			encoded += hex[c >> 4];
			encoded += hex[c & 15];
		}
	}
	return encoded;
}

std::string onlineSearchUrl(const std::string &query)
{
	return "https://lrclib.net/api/search?q=" + percentEncode(query);
}

std::string ovhSuggestUrl(const std::string &query)
{
	return "https://api.lyrics.ovh/suggest/" + percentEncode(query);
}

std::string ovhLyricsUrl(const std::string &artist, const std::string &title)
{
	return "https://api.lyrics.ovh/v1/" + percentEncode(artist) + "/" + percentEncode(title);
}

std::string cleanOvhLyrics(const std::string &text)
{
	std::string out;
	std::istringstream in(text);
	std::string line;
	int blanks = 0;
	bool first = true;
	while (std::getline(in, line)) {
		if (!line.empty() && line.back() == '\r')
			line.pop_back();
		if (first && line.rfind("Paroles de la chanson", 0) == 0) {
			first = false;
			continue;
		}
		first = false;
		const bool blank = line.find_first_not_of(" \t") == std::string::npos;
		if (blank) {
			++blanks;
			if (blanks > 1 || out.empty())
				continue;
			out += "\n";
			continue;
		}
		blanks = 0;
		out += line + "\n";
	}
	return out;
}

std::string plainFromSynced(const std::string &synced)
{
	std::string out;
	std::istringstream in(synced);
	std::string line;
	while (std::getline(in, line)) {
		// Drop every leading "[mm:ss.xx]" mark (a line can carry several).
		size_t pos = 0;
		while (pos < line.size() && line[pos] == '[') {
			const size_t close = line.find(']', pos);
			if (close == std::string::npos)
				break;
			const std::string mark = line.substr(pos + 1, close - pos - 1);
			const bool time = !mark.empty() && std::isdigit(static_cast<unsigned char>(mark[0])) &&
					  mark.find(':') != std::string::npos;
			if (!time)
				break;
			pos = close + 1;
		}
		std::string rest = line.substr(pos);
		const size_t a = rest.find_first_not_of(" \t\r");
		rest = a == std::string::npos ? std::string() : rest.substr(a);
		out += rest + "\n";
	}
	return out;
}

Song songFromOnline(const OnlineSong &found)
{
	const bool plain = !found.plainLyrics.empty();
	return parseSongText(found.title, found.artist,
			     plain ? found.plainLyrics : plainFromSynced(found.syncedLyrics));
}

std::vector<OnlineSong> usefulResults(const std::vector<OnlineSong> &all, size_t limit)
{
	std::vector<OnlineSong> out;
	std::set<std::string> seen;
	for (const OnlineSong &s : all) {
		if (!hasWords(s) || s.title.empty())
			continue;
		const std::string key = lowerTrim(s.title) + "\n" + lowerTrim(s.artist);
		if (!seen.insert(key).second)
			continue;
		out.push_back(s);
		if (out.size() >= limit)
			break;
	}
	return out;
}

} // namespace laycue::lyrics
