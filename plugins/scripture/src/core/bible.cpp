#include "core/bible.hpp"

#include "core/books.hpp"
#include "core/xml-lite.hpp"

#include <cctype>
#include <cstdlib>
#include <initializer_list>
#include <map>

namespace laycue::scripture {

namespace {

int numberOf(const std::string &s)
{
	if (s.empty() || !std::isdigit(static_cast<unsigned char>(s[0])))
		return 0;
	return std::atoi(s.c_str());
}

std::string attr(const xml::Event &e, std::initializer_list<const char *> names)
{
	for (const char *n : names) {
		const auto it = e.attrs.find(n);
		if (it != e.attrs.end())
			return it->second;
	}
	return std::string();
}

} // namespace

int Bible::chapterCount(int book) const
{
	return book >= 1 && book <= int(books.size()) ? int(books[size_t(book - 1)].size()) : 0;
}

int Bible::verseCount(int book, int chapter) const
{
	if (chapter < 1 || chapter > chapterCount(book))
		return 0;
	return int(books[size_t(book - 1)][size_t(chapter - 1)].size());
}

std::string Bible::verse(int book, int chapter, int v) const
{
	if (v < 1 || v > verseCount(book, chapter))
		return std::string();
	return books[size_t(book - 1)][size_t(chapter - 1)][size_t(v - 1)];
}

std::string abbreviate(const std::string &name)
{
	std::string out;
	bool start = true;
	for (char c : name) {
		if (std::isalnum(static_cast<unsigned char>(c))) {
			if (start)
				out += char(std::toupper(static_cast<unsigned char>(c)));
			start = false;
		} else {
			start = true;
		}
	}
	return out.size() > 8 ? out.substr(0, 8) : out;
}

bool parseBible(const std::string &xmlText, Bible &out, std::string &error)
{
	out = Bible();
	int book = 0, chapter = 0, verse = 0;
	int bookOrder = 0;
	int noteDepth = 0; // text inside <note> is a footnote, not the verse
	bool inVerse = false;
	bool inTitle = false, inIdentifier = false;
	std::string verseText, title, identifier;
	std::map<std::string, std::string> rootAttrs;
	bool sawRoot = false;

	const auto store = [&]() {
		if (book < 1 || book > 66 || chapter < 1 || verse < 1 || verse > 999 || chapter > 200)
			return;
		if (out.books.size() < size_t(book))
			out.books.resize(size_t(book));
		auto &chapters = out.books[size_t(book - 1)];
		if (chapters.size() < size_t(chapter))
			chapters.resize(size_t(chapter));
		auto &verses = chapters[size_t(chapter - 1)];
		if (verses.size() < size_t(verse))
			verses.resize(size_t(verse));
		verses[size_t(verse - 1)] = xml::squeeze(verseText);
	};

	const bool complete = xml::scan(xmlText, [&](const xml::Event &e) {
		using K = xml::Event::Kind;
		if (e.kind == K::Start) {
			if (!sawRoot) {
				sawRoot = true;
				rootAttrs = e.attrs;
			}
			if (e.name == "note" || e.name == "footnote") {
				++noteDepth;
			} else if (e.name == "title") {
				inTitle = true;
			} else if (e.name == "identifier") {
				inIdentifier = true;
			} else if (!inVerse && (e.name == "biblebook" || e.name == "b" || e.name == "book")) {
				++bookOrder;
				const std::string number = attr(e, {"bnumber", "number", "n", "id"});
				book = numberOf(number);
				if (book == 0) {
					// OpenSong names the book; otherwise books come in Bible order.
					const std::string bookName = attr(e, {"bname", "n", "name"});
					const std::vector<int> found = suggestBooks(bookName, 1);
					book = !bookName.empty() && !found.empty() ? found.front() : bookOrder;
				}
				chapter = 0;
			} else if (!inVerse && (e.name == "chapter" || e.name == "c")) {
				chapter = numberOf(attr(e, {"cnumber", "number", "n"}));
			} else if (!inVerse && (e.name == "vers" || e.name == "v" || e.name == "verse")) {
				verse = numberOf(attr(e, {"vnumber", "number", "n"}));
				inVerse = true;
				verseText.clear();
			}
		} else if (e.kind == K::End) {
			if (e.name == "note" || e.name == "footnote") {
				if (noteDepth > 0)
					--noteDepth;
			} else if (e.name == "title") {
				inTitle = false;
			} else if (e.name == "identifier") {
				inIdentifier = false;
			} else if (inVerse && (e.name == "vers" || e.name == "v" || e.name == "verse")) {
				store();
				inVerse = false;
			}
		} else if (noteDepth == 0) {
			if (inVerse)
				verseText += e.text;
			else if (inTitle && title.empty())
				title = xml::squeeze(e.text);
			else if (inIdentifier && identifier.empty())
				identifier = xml::squeeze(e.text);
		}
	});

	size_t verses = 0;
	for (const auto &b : out.books)
		for (const auto &c : b)
			verses += c.size();
	if (verses == 0) {
		out = Bible();
		error = complete ? "This file has no Bible verses Laycue can read (Zefania, OpenSong or Beblia XML)."
				 : "This file is not complete XML.";
		return false;
	}
	for (const char *key : {"biblename", "translation", "name"}) {
		const auto it = rootAttrs.find(key);
		if (out.name.empty() && it != rootAttrs.end())
			out.name = xml::squeeze(it->second);
	}
	if (out.name.empty())
		out.name = title;
	if (out.name.empty())
		out.name = "Bible";
	out.abbrev = identifier.empty() ? abbreviate(out.name) : identifier;
	if (out.books.size() < 66)
		out.books.resize(66);
	return true;
}

bool nextVerse(const Bible &bible, Reference &ref)
{
	Reference r = ref;
	if (r.verseFrom < 1)
		r.verseFrom = 1;
	else
		++r.verseFrom;
	r.verseTo = 0;
	while (r.book <= 66) {
		if (r.verseFrom <= bible.verseCount(r.book, r.chapter)) {
			ref = r;
			return true;
		}
		r.verseFrom = 1;
		++r.chapter;
		if (r.chapter > bible.chapterCount(r.book)) {
			++r.book;
			r.chapter = 1;
		}
	}
	return false;
}

bool previousVerse(const Bible &bible, Reference &ref)
{
	Reference r = ref;
	r.verseTo = 0;
	--r.verseFrom;
	while (r.book >= 1) {
		if (r.verseFrom >= 1 && r.verseFrom <= bible.verseCount(r.book, r.chapter)) {
			ref = r;
			return true;
		}
		--r.chapter;
		if (r.chapter < 1) {
			--r.book;
			if (r.book < 1)
				return false;
			r.chapter = bible.chapterCount(r.book);
		}
		r.verseFrom = bible.verseCount(r.book, r.chapter);
	}
	return false;
}

namespace {

// Lowercase, letter by letter (the verses hold text such as accented names, so the unsigned cast matters).
std::string lowered(const std::string &text)
{
	std::string out;
	out.reserve(text.size());
	for (char c : text)
		out += char(std::tolower(static_cast<unsigned char>(c)));
	return out;
}

} // namespace

std::vector<Reference> searchBible(const Bible &bible, const std::string &phrase, size_t limit)
{
	std::vector<Reference> out;
	const std::string needle = lowered(phrase);
	if (needle.size() < 2)
		return out;
	for (int book = 1; book <= int(bible.books.size()) && out.size() < limit; ++book) {
		const auto &chapters = bible.books[size_t(book - 1)];
		for (int chapter = 1; chapter <= int(chapters.size()) && out.size() < limit; ++chapter) {
			const auto &verses = chapters[size_t(chapter - 1)];
			for (int verse = 1; verse <= int(verses.size()) && out.size() < limit; ++verse) {
				if (lowered(verses[size_t(verse - 1)]).find(needle) != std::string::npos) {
					Reference hit;
					hit.book = book;
					hit.chapter = chapter;
					hit.verseFrom = verse;
					out.push_back(hit);
				}
			}
		}
	}
	return out;
}

} // namespace laycue::scripture
