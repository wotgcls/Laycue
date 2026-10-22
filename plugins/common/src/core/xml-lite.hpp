// A small, forgiving XML reader for the files churches already have: Bible versions (Zefania, OpenSong,
// Beblia) and songs (OpenLyrics). It walks the file and reports each element start, end and text.
// Pure C++17: no Qt, no libobs, tested.
#pragma once

#include <functional>
#include <map>
#include <string>

namespace laycue::xml {

struct Event {
	enum class Kind { Start, End, Text };
	Kind kind = Kind::Text;
	std::string name;                         // element name, lower case, without a namespace prefix
	std::map<std::string, std::string> attrs; // attribute names in lower case; values with entities decoded
	std::string text;                         // for Text: entities decoded, whitespace kept
};

// Calls `visit` for every element and text in order. A self-closing element gives a Start then an End.
// Comments, processing instructions, doctypes are skipped; CDATA is reported as text. Returns false if the
// file is cut off in the middle of a tag (what was read before is still reported).
bool scan(const std::string &xml, const std::function<void(const Event &)> &visit);

// &amp; &lt; &gt; &quot; &apos; and numeric references (&#8220; &#x201C;), written out as UTF-8.
std::string decodeEntities(const std::string &text);

// Collapses runs of whitespace to single spaces and trims both ends.
std::string squeeze(const std::string &text);

} // namespace laycue::xml
