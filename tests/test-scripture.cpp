#include "check.hpp"

#include "core/bible.hpp"
#include "core/books.hpp"
#include "core/reference.hpp"
#include "core/xml-lite.hpp"

#include <fstream>
#include <sstream>

using namespace laycue::scripture;

namespace {
std::string nameOf(const std::string &typed)
{
	const std::vector<int> found = suggestBooks(typed, 1);
	return found.empty() ? std::string() : bookByNumber(found.front())->name;
}
} // namespace

TEST_CASE("scripture: short forms find the right book")
{
	CHECK_EQ(books().size(), size_t(66));
	CHECK_EQ(nameOf("jn"), std::string("John"));
	CHECK_EQ(nameOf("joh"), std::string("John"));
	CHECK_EQ(nameOf("jo"), std::string("Joshua")); // Bible order when it could be several
	CHECK_EQ(nameOf("job"), std::string("Job"));   // an exact name wins
	CHECK_EQ(nameOf("1 cor"), std::string("1 Corinthians"));
	CHECK_EQ(nameOf("1cor"), std::string("1 Corinthians"));
	CHECK_EQ(nameOf("I John"), std::string("1 John"));
	CHECK_EQ(nameOf("ii kings"), std::string("2 Kings"));
	CHECK_EQ(nameOf("is"), std::string("Isaiah"));
	CHECK_EQ(nameOf("isa"), std::string("Isaiah"));
	CHECK_EQ(nameOf("ps"), std::string("Psalms"));
	CHECK_EQ(nameOf("psalm"), std::string("Psalms"));
	CHECK_EQ(nameOf("phil"), std::string("Philippians"));
	CHECK_EQ(nameOf("philem"), std::string("Philemon"));
	CHECK_EQ(nameOf("song"), std::string("Song of Solomon"));
	CHECK_EQ(nameOf("rev"), std::string("Revelation"));
	CHECK_EQ(nameOf("mt"), std::string("Matthew"));
	CHECK_EQ(nameOf("ge"), std::string("Genesis"));
	CHECK_EQ(nameOf("zzz"), std::string());
}

TEST_CASE("scripture: references in the ways people type them")
{
	Reference r;
	CHECK(parseReference("John 3:16", r));
	CHECK(r.book == 43 && r.chapter == 3 && r.verseFrom == 16 && r.verseTo == 0);
	CHECK(parseReference("jn 3 16", r));
	CHECK(r.book == 43 && r.chapter == 3 && r.verseFrom == 16);
	CHECK(parseReference("john 3:16-18", r));
	CHECK(r.verseFrom == 16 && r.verseTo == 18);
	CHECK(parseReference("1 cor 13:4-7", r));
	CHECK(r.book == 46 && r.chapter == 13 && r.verseFrom == 4 && r.verseTo == 7);
	CHECK(parseReference("Ps 23", r));
	CHECK(r.book == 19 && r.chapter == 23 && r.verseFrom == 0);
	CHECK(parseReference("Genesis 1.1", r));
	CHECK(r.book == 1 && r.chapter == 1 && r.verseFrom == 1);
	CHECK(parseReference("Rom 8:28\xE2\x80\x93"
			     "30",
			     r)); // en dash
	CHECK(r.verseFrom == 28 && r.verseTo == 30);
	CHECK(parseReference("rev", r));
	CHECK(r.book == 66 && r.chapter == 0);
	CHECK(!parseReference("hello 3:16", r));
	CHECK(!parseReference("", r));

	Reference label{43, 3, 16, 18};
	CHECK_EQ(referenceLabel(label), std::string("John 3:16-18"));
	CHECK_EQ(referenceLabel(Reference{19, 23, 0, 0}), std::string("Psalms 23"));
}

TEST_CASE("scripture: typing works like EasyWorship")
{
	CHECK_EQ(bookHint("jn"), std::string("John"));
	CHECK_EQ(bookHint("1"), std::string()); // not yet: it could be 1 John
	CHECK_EQ(bookHint("John 3"), std::string());
	CHECK_EQ(afterSpace("jn"), std::string("John "));
	CHECK_EQ(afterSpace("1 co"), std::string("1 Corinthians "));
	CHECK_EQ(afterSpace("1"), std::string("1 "));
	CHECK_EQ(afterSpace("John 3"), std::string("John 3:"));
	CHECK_EQ(afterSpace("John 3:16"), std::string("John 3:16 "));
}

TEST_CASE("scripture: the XML reader")
{
	std::vector<std::string> seen;
	laycue::xml::scan("<?xml version='1.0'?><!-- c --><A x='1 &amp; 2'><b/>t &lt;&#65;&#x42;<![CDATA[<raw>]]></A>",
			  [&](const laycue::xml::Event &e) {
				  using K = laycue::xml::Event::Kind;
				  if (e.kind == K::Start)
					  seen.push_back("<" + e.name +
							 (e.attrs.count("x") ? "[" + e.attrs.at("x") + "]" : "") + ">");
				  else if (e.kind == K::End)
					  seen.push_back("</" + e.name + ">");
				  else
					  seen.push_back(e.text);
			  });
	std::string joined;
	for (const auto &s : seen)
		joined += s + "|";
	CHECK_EQ(joined, std::string("<a[1 & 2]>|<b>|</b>|t <AB|<raw>|</a>|"));
	CHECK_EQ(laycue::xml::squeeze("  a \n\t b  "), std::string("a b"));
	CHECK_EQ(laycue::xml::decodeEntities("&#8220;x&#8221;"), std::string("\xE2\x80\x9Cx\xE2\x80\x9D"));
}

TEST_CASE("scripture: Zefania, OpenSong and Beblia files")
{
	Bible zef;
	std::string error;
	CHECK(parseBible("<XMLBIBLE biblename=\"Test Version\"><INFORMATION><identifier>TV</identifier></INFORMATION>"
			 "<BIBLEBOOK bnumber=\"43\" bname=\"John\"><CHAPTER cnumber=\"3\">"
			 "<VERS vnumber=\"16\">For God <STYLE>so</STYLE> loved<NOTE>a note</NOTE> the world</VERS>"
			 "<VERS vnumber=\"17\">For God sent not</VERS></CHAPTER></BIBLEBOOK></XMLBIBLE>",
			 zef, error));
	CHECK_EQ(zef.name, std::string("Test Version"));
	CHECK_EQ(zef.abbrev, std::string("TV"));
	CHECK_EQ(zef.verse(43, 3, 16), std::string("For God so loved the world"));
	CHECK_EQ(zef.verseCount(43, 3), 17);
	CHECK_EQ(zef.verse(43, 3, 1), std::string()); // missing verses are empty, not a crash

	Bible open;
	CHECK(parseBible("<bible><b n=\"Genesis\"><c n=\"1\"><v n=\"1\">In the beginning</v></c></b>"
			 "<b n=\"Psalms\"><c n=\"23\"><v n=\"1\">The <b>LORD</b> is my shepherd</v></c></b></bible>",
			 open, error));
	CHECK_EQ(open.verse(1, 1, 1), std::string("In the beginning"));
	CHECK_EQ(open.verse(19, 23, 1), std::string("The LORD is my shepherd"));
	CHECK_EQ(open.abbrev, std::string("B"));

	Bible beblia;
	CHECK(parseBible(
		"<bible translation=\"World English Bible\"><testament name=\"New\"><book number=\"66\">"
		"<chapter number=\"22\"><verse number=\"21\">The grace</verse></chapter></book></testament></bible>",
		beblia, error));
	CHECK_EQ(beblia.verse(66, 22, 21), std::string("The grace"));
	CHECK_EQ(beblia.abbrev, std::string("WEB"));

	Bible none;
	CHECK(!parseBible("<songs><song/></songs>", none, error));
	CHECK(!error.empty());
}

TEST_CASE("scripture: next and previous verse cross chapters and books")
{
	Bible b;
	std::string error;
	CHECK(parseBible(
		"<XMLBIBLE><BIBLEBOOK bnumber=\"1\"><CHAPTER cnumber=\"1\"><VERS vnumber=\"1\">a</VERS>"
		"<VERS vnumber=\"2\">b</VERS></CHAPTER><CHAPTER cnumber=\"2\"><VERS vnumber=\"1\">c</VERS>"
		"</CHAPTER></BIBLEBOOK><BIBLEBOOK bnumber=\"2\"><CHAPTER cnumber=\"1\"><VERS vnumber=\"1\">d</VERS>"
		"</CHAPTER></BIBLEBOOK></XMLBIBLE>",
		b, error));
	Reference r{1, 1, 2, 0};
	CHECK(nextVerse(b, r));
	CHECK(r.book == 1 && r.chapter == 2 && r.verseFrom == 1);
	CHECK(nextVerse(b, r));
	CHECK(r.book == 2 && r.chapter == 1 && r.verseFrom == 1);
	CHECK(previousVerse(b, r));
	CHECK(r.book == 1 && r.chapter == 2 && r.verseFrom == 1);
	CHECK(previousVerse(b, r));
	CHECK(r.book == 1 && r.chapter == 1 && r.verseFrom == 2);
	Reference first{1, 1, 1, 0};
	CHECK(!previousVerse(b, first));
}

TEST_CASE("scripture: the bundled King James Version loads")
{
	std::ifstream file(std::string(LAYCUE_PLUGINS_DIR) + "/scripture/data/bibles/kjv.xml", std::ios::binary);
	CHECK(file.good());
	if (!file.good())
		return;
	std::stringstream buffer;
	buffer << file.rdbuf();
	Bible kjv;
	std::string error;
	CHECK(parseBible(buffer.str(), kjv, error));
	CHECK_EQ(kjv.abbrev, std::string("KJV"));
	CHECK_EQ(kjv.verse(43, 3, 16),
		 std::string("For God so loved the world, that he gave his only begotten Son, that whosoever "
			     "believeth in him should not perish, but have everlasting life."));
	CHECK_EQ(kjv.verse(19, 23, 1), std::string("The LORD is my shepherd; I shall not want."));
	size_t verses = 0;
	for (int book = 1; book <= 66; ++book)
		for (int c = 1; c <= kjv.chapterCount(book); ++c)
			verses += size_t(kjv.verseCount(book, c));
	CHECK_EQ(verses, size_t(31102));
}
