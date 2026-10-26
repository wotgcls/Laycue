#include "check.hpp"

#include "core/online.hpp"
#include "core/song.hpp"

#include <fstream>
#include <sstream>

using namespace laycue::lyrics;

TEST_CASE("lyrics: part names are recognised, lyrics are not")
{
	CHECK_EQ(sectionLabel("Verse 1"), std::string("Verse 1"));
	CHECK_EQ(sectionLabel("[Chorus]"), std::string("Chorus"));
	CHECK_EQ(sectionLabel("Bridge:"), std::string("Bridge"));
	CHECK_EQ(sectionLabel("Pre-Chorus 2"), std::string("Pre-Chorus 2"));
	CHECK_EQ(sectionLabel("V2"), std::string("Verse 2"));
	CHECK_EQ(sectionLabel("C2"), std::string("Chorus 2"));
	CHECK_EQ(sectionLabel("C"), std::string()); // a bare letter may be a lyric
	CHECK_EQ(sectionLabel("Come, thou Fount"), std::string());
	CHECK_EQ(sectionLabel("Chorus of angels sing"), std::string());
	CHECK_EQ(sectionLabel("Verses"), std::string());
	CHECK_EQ(sectionLabel("Bless the Lord"), std::string());
}

TEST_CASE("lyrics: plain text becomes parts, and back")
{
	const Song song = parseSongText(" Amazing Grace ", "John Newton",
					"Verse 1\nAmazing grace! How sweet the sound\nThat saved a wretch like me!\n"
					"I once was lost, but now am found;\nWas blind, but now I see.\n\n\n"
					"[Chorus]\nLine A\nLine B\nLine C\n\nNo label here\n");
	CHECK_EQ(song.title, std::string("Amazing Grace"));
	CHECK_EQ(song.sections.size(), size_t(3));
	CHECK_EQ(song.sections[0].label, std::string("Verse 1"));
	CHECK_EQ(song.sections[0].lines.size(), size_t(4));
	CHECK_EQ(song.sections[1].label, std::string("Chorus"));
	CHECK_EQ(song.sections[2].label, std::string());
	const Song again = parseSongText("Amazing Grace", "John Newton", songText(song));
	CHECK(again.sections == song.sections);
}

TEST_CASE("lyrics: two lines at a time")
{
	const Song song = parseSongText("S", "", "Verse 1\na\nb\nc\nd\n\nChorus\ne\nf\ng\n");
	const std::vector<LyricSlide> slides = slidesFor(song, 2);
	CHECK_EQ(slides.size(), size_t(4));
	CHECK_EQ(slides[0].text, std::string("a\nb"));
	CHECK_EQ(slides[0].label, std::string("Verse 1"));
	CHECK_EQ(slides[1].text, std::string("c\nd"));
	CHECK_EQ(slides[2].text, std::string("e\nf"));
	CHECK_EQ(slides[3].text, std::string("g")); // an odd last line gets its own slide
	CHECK_EQ(slides[3].label, std::string("Chorus"));
	CHECK_EQ(slidesFor(song, 4).size(), size_t(2));
}

TEST_CASE("lyrics: OpenLyrics files from OpenLP")
{
	Song song;
	std::string error;
	CHECK(parseOpenLyrics("<?xml version='1.0' encoding='UTF-8'?>"
			      "<song xmlns=\"http://openlyrics.info/namespace/2009/song\" version=\"0.8\">"
			      "<properties><titles><title>Holy, Holy, Holy</title></titles>"
			      "<authors><author>Reginald Heber</author></authors></properties><lyrics>"
			      "<verse name=\"v1\"><lines>Holy, holy, holy!<br/>Lord God Almighty!<comment>x</comment>"
			      "</lines></verse><verse name=\"c\"><lines>Line one\nLine two</lines></verse>"
			      "</lyrics></song>",
			      song, error));
	CHECK_EQ(song.title, std::string("Holy, Holy, Holy"));
	CHECK_EQ(song.author, std::string("Reginald Heber"));
	CHECK_EQ(song.sections.size(), size_t(2));
	CHECK_EQ(song.sections[0].label, std::string("Verse 1"));
	CHECK_EQ(song.sections[0].lines.size(), size_t(2));
	CHECK_EQ(song.sections[0].lines[1], std::string("Lord God Almighty!"));
	CHECK_EQ(song.sections[1].label, std::string("Chorus"));
	CHECK_EQ(song.sections[1].lines.size(), size_t(2));
	CHECK(!parseOpenLyrics("<song><properties/></song>", song, error));
}

TEST_CASE("lyrics: typing a song name finds it")
{
	std::vector<Song> songs;
	for (const char *title :
	     {"Amazing Grace", "Holy, Holy, Holy", "It Is Well with My Soul", "Rock of Ages", "Grace Alone"})
		songs.push_back(parseSongText(title, "", "Verse 1\nfirst line of " + std::string(title) + "\n"));
	songs[3].sections[0].lines[0] = "Cleft for me";
	songs[4].sections.push_back({"Bridge", {"shelter me from the cold"}}); // a line no name or first line has
	const auto names = [&](const std::string &q) {
		std::string out;
		for (size_t i : searchSongs(songs, q))
			out += songs[i].title + "|";
		return out;
	};
	CHECK_EQ(names("amaz"), std::string("Amazing Grace|"));
	CHECK_EQ(names("grace"), std::string("Grace Alone|Amazing Grace|")); // starts with it first
	CHECK_EQ(names("well"), std::string("It Is Well with My Soul|"));
	CHECK_EQ(names("holy holy"), std::string("Holy, Holy, Holy|"));
	CHECK_EQ(names("cleft"), std::string("Rock of Ages|")); // by its first line
	CHECK_EQ(names("shelter"), std::string("Grace Alone|")); // by a line deep in the song
	CHECK_EQ(names("zzz"), std::string());
	CHECK_EQ(searchSongs(songs, "", 3).size(), size_t(3));

	CHECK_EQ(makeSongId("It Is Well (With My Soul)", {}), std::string("it-is-well-with-my-soul"));
	CHECK_EQ(makeSongId("Amazing Grace", {"amazing-grace"}), std::string("amazing-grace-2"));
}

TEST_CASE("lyrics: song files with a Title and Author header")
{
	const Song song =
		parseSongFile("Title: Rock of Ages\r\nAuthor: Augustus Toplady\r\n\r\nVerse 1\r\nRock of Ages, "
			      "cleft for me,\r\nLet me hide myself in Thee;\r\n",
			      "rock-of-ages");
	CHECK_EQ(song.title, std::string("Rock of Ages"));
	CHECK_EQ(song.author, std::string("Augustus Toplady"));
	CHECK_EQ(song.sections.size(), size_t(1));
	CHECK_EQ(song.sections[0].lines[1], std::string("Let me hide myself in Thee;"));
	CHECK_EQ(parseSongFile("Just a line\n", "My Song").title, std::string("My Song"));
}

TEST_CASE("lyrics: every bundled hymn reads cleanly")
{
	for (const char *file :
	     {"amazing-grace", "holy-holy-holy", "it-is-well-with-my-soul", "blessed-assurance",
	      "what-a-friend-we-have-in-jesus", "rock-of-ages", "when-i-survey-the-wondrous-cross", "come-thou-fount",
	      "to-god-be-the-glory", "nothing-but-the-blood", "all-hail-the-power", "joyful-joyful-we-adore-thee",
	      "i-surrender-all", "doxology", "jesus-loves-me"}) {
		std::ifstream in(std::string(LAYCUE_PLUGINS_DIR) + "/lyrics/data/songs/" + file + ".txt");
		CHECK(in.good());
		std::stringstream buffer;
		buffer << in.rdbuf();
		const Song song = parseSongFile(buffer.str(), file);
		CHECK(!song.title.empty() && song.title != file);
		CHECK(!song.author.empty());
		CHECK(!song.sections.empty());
		for (const Section &s : song.sections) {
			CHECK(!s.label.empty()); // every part is named
			CHECK(!s.lines.empty());
		}
	}
}

TEST_CASE("lyrics: songs found online")
{
	CHECK_EQ(onlineSearchUrl("way maker & co"),
		 std::string("https://lrclib.net/api/search?q=way%20maker%20%26%20co"));
	CHECK_EQ(onlineSearchUrl("\xC3\xA9"), std::string("https://lrclib.net/api/search?q=%C3%A9"));

	CHECK_EQ(plainFromSynced(
			 "[00:12.34] Amazing grace\n[00:15.00][01:20.00] How sweet\n[00:20.00]\n[00:21.00] Next"),
		 std::string("Amazing grace\nHow sweet\n\nNext\n"));
	CHECK_EQ(plainFromSynced("[Chorus]\nline"), std::string("[Chorus]\nline\n")); // a part name is not a time

	OnlineSong found;
	found.title = "Great Song";
	found.artist = "Someone";
	found.plainLyrics = "Line one\nLine two\n\n[Chorus]\nChorus one\nChorus two\n";
	const Song song = songFromOnline(found);
	CHECK_EQ(song.title, std::string("Great Song"));
	CHECK_EQ(song.author, std::string("Someone"));
	CHECK_EQ(song.sections.size(), size_t(2));
	CHECK_EQ(song.sections[1].label, std::string("Chorus"));

	OnlineSong syncedOnly = found;
	syncedOnly.plainLyrics.clear();
	syncedOnly.syncedLyrics = "[00:01.00] Only synced\n[00:02.00] Second";
	CHECK_EQ(songFromOnline(syncedOnly).sections.front().lines.size(), size_t(2));

	OnlineSong instrumental = found;
	instrumental.instrumental = true;
	OnlineSong empty = found;
	empty.title = "Empty";
	empty.plainLyrics = "  \n ";
	OnlineSong duplicate = found; // same title and artist, other spelling of case
	duplicate.title = "great song";
	OnlineSong other = found;
	other.artist = "Another Singer";
	const std::vector<OnlineSong> useful = listResults({found, instrumental, empty, duplicate, other});
	CHECK_EQ(useful.size(), size_t(5)); // nothing is left out
	CHECK(!useful[0].needsWords);
	CHECK(useful[1].needsWords); // listed without words: looked for in the other source when picked
	CHECK(useful[2].needsWords);
	CHECK_EQ(useful[4].artist, std::string("Another Singer"));
}

TEST_CASE("lyrics: the second online source")
{
	CHECK_EQ(ovhSuggestUrl("mercy chinwo"), std::string("https://api.lyrics.ovh/suggest/mercy%20chinwo"));
	CHECK_EQ(ovhLyricsUrl("Nathaniel Bassey", "Onise Iyanu"),
		 std::string("https://api.lyrics.ovh/v1/Nathaniel%20Bassey/Onise%20Iyanu"));
	CHECK_EQ(cleanOvhLyrics(
			 "Paroles de la chanson Excess Love par Mercy Chinwo\r\nLine one\r\nLine two\r\n\r\n\r\n\r\n"
			 "Chorus line\r\n"),
		 std::string("Line one\nLine two\n\nChorus line\n"));
	OnlineSong listed;
	listed.source = OnlineSource::LyricsOvh;
	listed.needsWords = true; // no words yet: still worth showing
	listed.title = "Excess Love";
	listed.artist = "Mercy Chinwo";
	CHECK_EQ(listResults({listed}).size(), size_t(1));
}

TEST_CASE("lyrics: song titles lose the parts sites spell differently")
{
	CHECK_EQ(cleanSongTitle("Way Maker (Live) [feat. Someone]"), std::string("Way Maker"));
	CHECK_EQ(cleanSongTitle("Excess Love feat. Mercy Chinwo"), std::string("Excess Love"));
	CHECK_EQ(cleanSongTitle("Jehovah Nissi - Live at the Cathedral"), std::string("Jehovah Nissi"));
	CHECK_EQ(cleanSongTitle("Plain Title"), std::string("Plain Title"));
	CHECK_EQ(cleanSongTitle("(Intro)"), std::string("(Intro)")); // nothing left: keep it as it was
}
