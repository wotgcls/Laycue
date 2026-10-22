#include "core/xml-lite.hpp"

#include <cctype>
#include <cstdlib>

namespace laycue::xml {

namespace {

void appendUtf8(std::string &out, unsigned long cp)
{
	if (cp < 0x80) {
		out += char(cp);
	} else if (cp < 0x800) {
		out += char(0xC0 | (cp >> 6));
		out += char(0x80 | (cp & 0x3F));
	} else if (cp < 0x10000) {
		out += char(0xE0 | (cp >> 12));
		out += char(0x80 | ((cp >> 6) & 0x3F));
		out += char(0x80 | (cp & 0x3F));
	} else if (cp < 0x110000) {
		out += char(0xF0 | (cp >> 18));
		out += char(0x80 | ((cp >> 12) & 0x3F));
		out += char(0x80 | ((cp >> 6) & 0x3F));
		out += char(0x80 | (cp & 0x3F));
	}
}

std::string lower(std::string s)
{
	for (char &c : s)
		c = char(std::tolower(static_cast<unsigned char>(c)));
	return s;
}

// "zef:VERS" -> "vers"
std::string localName(const std::string &raw)
{
	const size_t colon = raw.find(':');
	return lower(colon == std::string::npos ? raw : raw.substr(colon + 1));
}

bool isNameChar(char c)
{
	return std::isalnum(static_cast<unsigned char>(c)) || c == '_' || c == '-' || c == ':' || c == '.' ||
	       static_cast<unsigned char>(c) >= 0x80;
}

} // namespace

std::string decodeEntities(const std::string &text)
{
	if (text.find('&') == std::string::npos)
		return text;
	std::string out;
	out.reserve(text.size());
	for (size_t i = 0; i < text.size(); ++i) {
		if (text[i] != '&') {
			out += text[i];
			continue;
		}
		const size_t semi = text.find(';', i);
		if (semi == std::string::npos || semi - i > 10) {
			out += '&';
			continue;
		}
		const std::string name = text.substr(i + 1, semi - i - 1);
		if (name == "amp")
			out += '&';
		else if (name == "lt")
			out += '<';
		else if (name == "gt")
			out += '>';
		else if (name == "quot")
			out += '"';
		else if (name == "apos")
			out += '\'';
		else if (name == "nbsp")
			out += ' ';
		else if (!name.empty() && name[0] == '#') {
			const bool hex = name.size() > 1 && (name[1] == 'x' || name[1] == 'X');
			const unsigned long cp = std::strtoul(name.c_str() + (hex ? 2 : 1), nullptr, hex ? 16 : 10);
			appendUtf8(out, cp);
		} else {
			out += text.substr(i, semi - i + 1); // unknown: keep as written
		}
		i = semi;
	}
	return out;
}

std::string squeeze(const std::string &text)
{
	std::string out;
	bool space = false;
	for (char c : text) {
		if (std::isspace(static_cast<unsigned char>(c))) {
			space = true;
			continue;
		}
		if (space && !out.empty())
			out += ' ';
		space = false;
		out += c;
	}
	return out;
}

bool scan(const std::string &xml, const std::function<void(const Event &)> &visit)
{
	size_t i = 0;
	const size_t n = xml.size();
	// A UTF-8 byte order mark is not text.
	if (n >= 3 && static_cast<unsigned char>(xml[0]) == 0xEF && static_cast<unsigned char>(xml[1]) == 0xBB &&
	    static_cast<unsigned char>(xml[2]) == 0xBF)
		i = 3;
	while (i < n) {
		if (xml[i] != '<') {
			const size_t next = xml.find('<', i);
			const size_t end = next == std::string::npos ? n : next;
			Event e;
			e.kind = Event::Kind::Text;
			e.text = decodeEntities(xml.substr(i, end - i));
			visit(e);
			i = end;
			continue;
		}
		if (xml.compare(i, 4, "<!--") == 0) {
			const size_t end = xml.find("-->", i + 4);
			if (end == std::string::npos)
				return false;
			i = end + 3;
			continue;
		}
		if (xml.compare(i, 9, "<![CDATA[") == 0) {
			const size_t end = xml.find("]]>", i + 9);
			if (end == std::string::npos)
				return false;
			Event e;
			e.kind = Event::Kind::Text;
			e.text = xml.substr(i + 9, end - i - 9);
			visit(e);
			i = end + 3;
			continue;
		}
		if (i + 1 < n && (xml[i + 1] == '?' || xml[i + 1] == '!')) {
			const size_t end = xml.find('>', i);
			if (end == std::string::npos)
				return false;
			i = end + 1;
			continue;
		}
		const size_t close = xml.find('>', i);
		if (close == std::string::npos)
			return false;
		std::string tag = xml.substr(i + 1, close - i - 1);
		i = close + 1;
		if (!tag.empty() && tag[0] == '/') {
			size_t k = 1;
			while (k < tag.size() && isNameChar(tag[k]))
				++k;
			Event e;
			e.kind = Event::Kind::End;
			e.name = localName(tag.substr(1, k - 1));
			visit(e);
			continue;
		}
		bool selfClosing = false;
		if (!tag.empty() && tag.back() == '/') {
			selfClosing = true;
			tag.pop_back();
		}
		size_t k = 0;
		while (k < tag.size() && isNameChar(tag[k]))
			++k;
		Event start;
		start.kind = Event::Kind::Start;
		start.name = localName(tag.substr(0, k));
		// Attributes: name="value" or name='value'.
		while (k < tag.size()) {
			while (k < tag.size() && std::isspace(static_cast<unsigned char>(tag[k])))
				++k;
			const size_t nameStart = k;
			while (k < tag.size() && isNameChar(tag[k]))
				++k;
			if (k == nameStart) {
				++k;
				continue;
			}
			const std::string attr = localName(tag.substr(nameStart, k - nameStart));
			while (k < tag.size() && std::isspace(static_cast<unsigned char>(tag[k])))
				++k;
			if (k >= tag.size() || tag[k] != '=') {
				start.attrs[attr] = std::string();
				continue;
			}
			++k;
			while (k < tag.size() && std::isspace(static_cast<unsigned char>(tag[k])))
				++k;
			if (k < tag.size() && (tag[k] == '"' || tag[k] == '\'')) {
				const char quote = tag[k];
				const size_t endQuote = tag.find(quote, k + 1);
				const size_t stop = endQuote == std::string::npos ? tag.size() : endQuote;
				start.attrs[attr] = decodeEntities(tag.substr(k + 1, stop - k - 1));
				k = stop + 1;
			} else {
				const size_t valueStart = k;
				while (k < tag.size() && !std::isspace(static_cast<unsigned char>(tag[k])))
					++k;
				start.attrs[attr] = decodeEntities(tag.substr(valueStart, k - valueStart));
			}
		}
		visit(start);
		if (selfClosing) {
			Event end;
			end.kind = Event::Kind::End;
			end.name = start.name;
			visit(end);
		}
	}
	return true;
}

} // namespace laycue::xml
