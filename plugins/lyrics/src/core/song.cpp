#include "core/song.hpp"

#include "core/xml-lite.hpp"

#include <algorithm>
#include <cctype>
#include <sstream>

namespace laycue::lyrics {

namespace {

std::string trim(const std::string &s)
{
	const size_t a = s.find_first_not_of(" \t\r");
	if (a == std::string::npos)
		return std::string();
	const size_t b = s.find_last_not_of(" \t\r");
	return s.substr(a, b - a + 1);
}

std::string lower(const std::string &s)
{
	std::string out;
	for (char c : s)
		out += char(std::tolower(static_cast<unsigned char>(c)));
	return out;
}

// Letters and digits only, lower case, single spaces: "It Is Well (With My Soul)" -> "it is well with my soul".
std::string simple(const std::string &s)
{
	std::string out;
	bool space = false;
	for (char c : s) {
		const unsigned char u = static_cast<unsigned char>(c);
		if (std::isalnum(u) || u >= 0x80) {
			if (space && !out.empty())
				out += ' ';
			space = false;
			out += char(std::tolower(u));
		} else if (c != '\'') {
			space = true;
		}
	}
	return out;
}

std::vector<std::string> splitLines(const std::string &text)
{
	std::vector<std::string> lines;
	std::string line;
	std::istringstream in(text);
	while (std::getline(in, line))
		lines.push_back(line);
	return lines;
}

// OpenLyrics part names: v1 -> Verse 1, c -> Chorus, c2 -> Chorus 2, b -> Bridge, p -> Pre-Chorus.
std::string openLyricsLabel(const std::string &name)
{
	const std::string n = lower(trim(name));
	if (n.empty())
		return std::string();
	size_t digits = n.size();
	while (digits > 0 && std::isdigit(static_cast<unsigned char>(n[digits - 1])))
		--digits;
	const std::string kind = n.substr(0, digits);
	const std::string number = n.substr(digits);
	std::string word;
	if (kind == "v")
		word = "Verse";
	else if (kind == "c")
		word = "Chorus";
	else if (kind == "b")
		word = "Bridge";
	else if (kind == "p")
		word = "Pre-Chorus";
	else if (kind == "e")
		word = "Ending";
	else if (kind == "i")
		word = "Intro";
	else if (kind == "o")
		word = "Outro";
	else
		return trim(name);
	return number.empty() ? word : word + " " + number;
}

} // namespace

std::string sectionLabel(const std::string &raw)
{
	std::string line = trim(raw);
	if (line.size() >= 2 && line.front() == '[' && line.back() == ']')
		line = trim(line.substr(1, line.size() - 2));
	if (!line.empty() && line.back() == ':')
		line = trim(line.substr(0, line.size() - 1));
	if (line.empty() || line.size() > 24)
		return std::string();
	const std::string l = lower(line);
	static const char *words[] = {"verse", "chorus", "pre-chorus", "prechorus", "pre chorus", "bridge", "refrain",
				      "tag",   "ending", "intro",      "outro",     "interlude",  "vamp",   "coda"};
	for (const char *w : words) {
		const std::string word = w;
		if (l == word)
			return line;
		if (l.size() > word.size() && l.compare(0, word.size(), word) == 0) {
			const std::string rest = trim(l.substr(word.size()));
			if (!rest.empty() && std::all_of(rest.begin(), rest.end(), [](char c) {
				    return std::isdigit(static_cast<unsigned char>(c));
			    }))
				return line;
		}
	}
	// Short forms with a number: V1, C2, B1, PC1. A bare letter could be a lyric, so it is not a name.
	static const char *shorts[] = {"v", "c", "b", "pc", "t"};
	for (const char *w : shorts) {
		const std::string word = w;
		if (l.compare(0, word.size(), word) == 0) {
			const std::string rest = l.substr(word.size());
			if (std::all_of(rest.begin(), rest.end(),
					[](char c) { return std::isdigit(static_cast<unsigned char>(c)); }) &&
			    !rest.empty())
				return openLyricsLabel(word == "pc" ? "p" + rest : (word == "t" ? "tag" : l));
		}
	}
	return std::string();
}

Song parseSongText(const std::string &title, const std::string &author, const std::string &text)
{
	Song song;
	song.title = trim(title);
	song.author = trim(author);
	Section current;
	const auto flush = [&]() {
		if (!current.lines.empty())
			song.sections.push_back(current);
		current = Section();
	};
	for (const std::string &raw : splitLines(text)) {
		const std::string line = trim(raw);
		if (line.empty()) {
			flush();
			continue;
		}
		const std::string label = sectionLabel(line);
		if (!label.empty()) {
			flush();
			current.label = label;
			continue;
		}
		current.lines.push_back(line);
	}
	flush();
	return song;
}

std::string songText(const Song &song)
{
	std::string out;
	for (size_t i = 0; i < song.sections.size(); ++i) {
		if (i > 0)
			out += "\n";
		const Section &s = song.sections[i];
		if (!s.label.empty())
			out += s.label + "\n";
		for (const std::string &line : s.lines)
			out += line + "\n";
	}
	return out;
}

Song parseSongFile(const std::string &text, const std::string &fallbackTitle)
{
	std::string title = fallbackTitle, author, body;
	bool header = true;
	for (const std::string &raw : splitLines(text)) {
		const std::string line = trim(raw);
		if (header) {
			const std::string l = lower(line);
			if (l.rfind("title:", 0) == 0) {
				title = trim(line.substr(6));
				continue;
			}
			if (l.rfind("author:", 0) == 0) {
				author = trim(line.substr(7));
				continue;
			}
			if (line.empty())
				continue;
			header = false;
		}
		body += raw + "\n";
	}
	return parseSongText(title, author, body);
}

std::vector<LyricSlide> slidesFor(const Song &song, int linesPerSlide)
{
	std::vector<LyricSlide> out;
	const size_t per = size_t(std::max(1, linesPerSlide));
	for (const Section &s : song.sections) {
		for (size_t i = 0; i < s.lines.size(); i += per) {
			LyricSlide slide;
			slide.label = s.label;
			for (size_t k = i; k < std::min(s.lines.size(), i + per); ++k) {
				if (!slide.text.empty())
					slide.text += "\n";
				slide.text += s.lines[k];
			}
			out.push_back(slide);
		}
	}
	return out;
}

bool parseOpenLyrics(const std::string &xmlText, Song &out, std::string &error)
{
	out = Song();
	int commentDepth = 0;
	Section current;
	std::string line;
	bool inLines = false, inTitle = false, inAuthor = false;
	const auto endLine = [&]() {
		const std::string t = xml::squeeze(line);
		if (!t.empty())
			current.lines.push_back(t);
		line.clear();
	};
	const bool complete = xml::scan(xmlText, [&](const xml::Event &e) {
		using K = xml::Event::Kind;
		if (e.kind == K::Start) {
			if (e.name == "comment")
				++commentDepth;
			else if (e.name == "title" && out.title.empty())
				inTitle = true;
			else if (e.name == "author" && out.author.empty())
				inAuthor = true;
			else if (e.name == "verse") {
				current = Section();
				const auto name = e.attrs.find("name");
				current.label = name == e.attrs.end() ? std::string() : openLyricsLabel(name->second);
			} else if (e.name == "lines") {
				inLines = true;
				line.clear();
			} else if (e.name == "br" && inLines) {
				endLine();
			}
		} else if (e.kind == K::End) {
			if (e.name == "comment" && commentDepth > 0)
				--commentDepth;
			else if (e.name == "title")
				inTitle = false;
			else if (e.name == "author")
				inAuthor = false;
			else if (e.name == "lines" && inLines) {
				endLine();
				inLines = false;
			} else if (e.name == "verse") {
				if (!current.lines.empty())
					out.sections.push_back(current);
				current = Section();
			}
		} else if (commentDepth == 0) {
			if (inTitle)
				out.title += e.text;
			else if (inAuthor)
				out.author += e.text;
			else if (inLines) {
				// Some files put each line on its own line instead of <br/>.
				const std::vector<std::string> parts = splitLines(e.text);
				for (size_t i = 0; i < parts.size(); ++i) {
					if (i > 0)
						endLine();
					line += parts[i];
				}
			}
		}
	});
	out.title = xml::squeeze(out.title);
	out.author = xml::squeeze(out.author);
	if (out.sections.empty() || out.title.empty()) {
		error = complete ? "This file has no song Laycue can read (OpenLyrics XML)."
				 : "This file is not complete XML.";
		out = Song();
		return false;
	}
	return true;
}

namespace {

// Whether a lyric line of any part (not just the first) contains the query, so a song can be found by the
// words it sings. Case and punctuation don't matter (the query arrives as `simple()` already).
bool inLyrics(const Song &song, const std::string &q)
{
	if (q.empty())
		return false;
	for (const Section &section : song.sections) {
		for (const std::string &line : section.lines) {
			if (simple(line).find(q) != std::string::npos)
				return true;
		}
	}
	return false;
}

} // namespace

std::vector<size_t> searchSongs(const std::vector<Song> &songs, const std::string &query, size_t limit)
{
	const std::string q = simple(query);
	std::vector<std::pair<int, size_t>> scored;
	for (size_t i = 0; i < songs.size(); ++i) {
		const std::string title = simple(songs[i].title);
		int score = 0;
		if (q.empty())
			score = 1;
		else if (title.rfind(q, 0) == 0)
			score = 4;
		else if (title.find(" " + q) != std::string::npos)
			score = 3;
		else if (title.find(q) != std::string::npos)
			score = 2;
		else if (inLyrics(songs[i], q))
			score = 1;
		if (score > 0)
			scored.push_back({score, i});
	}
	std::stable_sort(scored.begin(), scored.end(), [&](const auto &a, const auto &b) {
		if (a.first != b.first)
			return a.first > b.first;
		return simple(songs[a.second].title) < simple(songs[b.second].title);
	});
	std::vector<size_t> out;
	for (const auto &s : scored) {
		if (out.size() >= limit)
			break;
		out.push_back(s.second);
	}
	return out;
}

std::string makeSongId(const std::string &title, const std::vector<std::string> &taken)
{
	std::string base;
	for (char c : simple(title))
		base += c == ' ' ? '-' : c;
	if (base.empty())
		base = "song";
	std::string id = base;
	for (int n = 2; std::find(taken.begin(), taken.end(), id) != taken.end(); ++n)
		id = base + "-" + std::to_string(n);
	return id;
}

} // namespace laycue::lyrics
