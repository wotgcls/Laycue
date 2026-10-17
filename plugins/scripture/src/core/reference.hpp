// A Bible reference and the EasyWorship-style typing: "jn" + Space -> "John ", "John 3" + Space -> "John 3:".
// Pure C++17, tested.
#pragma once

#include <string>

namespace laycue::scripture {

struct Reference {
	int book = 0;      // 1..66
	int chapter = 0;   // 0 = not typed yet
	int verseFrom = 0; // 0 = the whole chapter
	int verseTo = 0;   // 0 = just verseFrom
	bool valid() const { return book > 0 && chapter > 0; }
	bool operator==(const Reference &o) const
	{
		return book == o.book && chapter == o.chapter && verseFrom == o.verseFrom && verseTo == o.verseTo;
	}
};

// "John 3:16", "jn 3 16", "john 3:16-18", "1 cor 13:4-7", "Ps 23", "Genesis 1.1". False if no book matches.
// A reference without a chapter still gives the book (chapter 0).
bool parseReference(const std::string &text, Reference &out);

// "John 3:16", "John 3:16-18", "Psalms 23". Empty for an unknown book.
std::string referenceLabel(const Reference &ref);

// The book part as typed so far ("jn" in "jn 3"), and whether a number has been typed after it.
std::string bookPart(const std::string &typed, bool *hasNumbers = nullptr);

// What the box holds after pressing Space: the book is completed ("jn" -> "John "), after a chapter a colon
// is added ("John 3" -> "John 3:"), otherwise a plain space.
std::string afterSpace(const std::string &typed);

// The grey hint shown while typing a book: the full name it will become, or "".
std::string bookHint(const std::string &typed);

} // namespace laycue::scripture
