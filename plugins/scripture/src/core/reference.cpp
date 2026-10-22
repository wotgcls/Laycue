#include "core/reference.hpp"

#include "core/books.hpp"

#include <cctype>
#include <vector>

namespace laycue::scripture {

namespace {

bool isDigit(char c)
{
	return std::isdigit(static_cast<unsigned char>(c)) != 0;
}

bool isAlpha(char c)
{
	return std::isalpha(static_cast<unsigned char>(c)) != 0;
}

std::string trim(const std::string &s)
{
	const size_t a = s.find_first_not_of(" \t");
	if (a == std::string::npos)
		return std::string();
	const size_t b = s.find_last_not_of(" \t");
	return s.substr(a, b - a + 1);
}

// Where the book's name ends: the first digit that comes after a letter.
size_t bookEnd(const std::string &t)
{
	bool letter = false;
	for (size_t i = 0; i < t.size(); ++i) {
		if (isAlpha(t[i]))
			letter = true;
		else if (letter && isDigit(t[i]))
			return i;
	}
	return t.size();
}

} // namespace

std::string bookPart(const std::string &typed, bool *hasNumbers)
{
	const std::string t = trim(typed);
	const size_t end = bookEnd(t);
	if (hasNumbers)
		*hasNumbers = end < t.size();
	return trim(t.substr(0, end));
}

bool parseReference(const std::string &text, Reference &out)
{
	out = Reference();
	const std::string t = trim(text);
	const size_t end = bookEnd(t);
	const std::vector<int> found = suggestBooks(t.substr(0, end), 1);
	if (found.empty())
		return false;
	out.book = found.front();
	// The numbers after the book, and what separates them: "3:16-18".
	std::vector<int> numbers;
	std::vector<char> separators;
	char pending = ' ';
	for (size_t i = end; i < t.size();) {
		if (isDigit(t[i])) {
			int n = 0;
			while (i < t.size() && isDigit(t[i]) && n < 100000)
				n = n * 10 + (t[i++] - '0');
			numbers.push_back(n);
			separators.push_back(pending);
			pending = ' ';
		} else {
			const char c = t[i];
			// The UTF-8 en dash (E2 80 93) counts as a range dash.
			if (c == '-' || (static_cast<unsigned char>(c) == 0xE2 && i + 2 < t.size() &&
					 static_cast<unsigned char>(t[i + 2]) == 0x93))
				pending = '-';
			else if ((c == ':' || c == '.' || c == ',') && pending != '-')
				pending = ':';
			++i;
		}
	}
	if (!numbers.empty())
		out.chapter = numbers[0];
	if (numbers.size() > 1)
		out.verseFrom = numbers[1];
	if (numbers.size() > 2 && separators[2] == '-')
		out.verseTo = numbers[2];
	if (out.verseTo > 0 && out.verseTo <= out.verseFrom)
		out.verseTo = 0;
	return true;
}

std::string referenceLabel(const Reference &ref)
{
	const BookInfo *book = bookByNumber(ref.book);
	if (!book)
		return std::string();
	std::string label = book->name;
	if (ref.chapter > 0)
		label += " " + std::to_string(ref.chapter);
	if (ref.chapter > 0 && ref.verseFrom > 0)
		label += ":" + std::to_string(ref.verseFrom);
	if (ref.verseFrom > 0 && ref.verseTo > ref.verseFrom)
		label += "-" + std::to_string(ref.verseTo);
	return label;
}

std::string bookHint(const std::string &typed)
{
	bool numbers = false;
	const std::string book = bookPart(typed, &numbers);
	bool letter = false;
	for (char c : book)
		letter = letter || isAlpha(c);
	if (!letter || numbers) // "1" on its own is the start of "1 John": no hint yet
		return std::string();
	const std::vector<int> found = suggestBooks(book, 1);
	return found.empty() ? std::string() : bookByNumber(found.front())->name;
}

std::string inlineBookCompletion(const std::string &typed)
{
	const std::string hint = bookHint(typed);
	const std::string t = trim(typed);
	if (hint.size() <= t.size())
		return std::string();
	for (size_t i = 0; i < t.size(); ++i) {
		if (std::tolower(static_cast<unsigned char>(hint[i])) != std::tolower(static_cast<unsigned char>(t[i])))
			return std::string();
	}
	return hint;
}

std::string afterSpace(const std::string &typed)
{
	bool numbers = false;
	const std::string book = bookPart(typed, &numbers);
	if (!numbers) {
		const std::string hint = bookHint(typed);
		// "1" alone is the start of "1 John": keep typing.
		if (!hint.empty())
			return hint + " ";
		return typed + " ";
	}
	// "John 3" -> "John 3:" (a chapter with no verse yet).
	const std::string t = trim(typed);
	if (!t.empty() && isDigit(t.back()) && t.find(':', bookEnd(t)) == std::string::npos &&
	    t.find(' ', bookEnd(t)) == std::string::npos)
		return t + ":";
	return typed + " ";
}

} // namespace laycue::scripture
