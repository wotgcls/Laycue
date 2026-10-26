#include "check.hpp"

#include "../plugins/lyrics/src/core/templates.hpp"
#include "../plugins/scripture/src/core/templates.hpp"
#include "slides/slide-style.hpp"

#include <fstream>
#include <set>
#include <sstream>
#include <string>

using namespace laycue::slides;

namespace {

std::string readLocale(const char *plugin)
{
	std::ifstream in(std::string(LAYCUE_PLUGINS_DIR) + "/" + plugin + "/data/locale/en-US.ini");
	std::stringstream all;
	all << in.rdbuf();
	return all.str();
}

bool hasKey(const std::string &ini, const std::string &key)
{
	return ini.find("\n" + key + "=") != std::string::npos || ini.rfind(key + "=", 0) == 0;
}

} // namespace

TEST_CASE("slides: a style is always brought back into range")
{
	SlideStyle s;
	s.layout = 99;
	s.align = -4;
	s.font = 77;
	s.decor = 9;
	s.transition = 5;
	s.sizePercent = 5000;
	sanitize(s);
	CHECK_EQ(s.layout, kLayoutCount - 1);
	CHECK_EQ(s.align, 0);
	CHECK_EQ(s.font, kFontCount - 1);
	CHECK_EQ(s.decor, kDecorCount - 1);
	CHECK_EQ(s.transition, int(Transition::Cut));
	CHECK_EQ(s.sizePercent, kMaxSize);
	// The new choices are part of what makes two looks the same.
	SlideStyle a, b;
	CHECK(a == b);
	b.font = int(Font::Classic);
	CHECK(a != b);
	b = a;
	b.decor = int(Decor::OpenBible);
	CHECK(a != b);
	b = a;
	b.transition = int(Transition::Cut);
	CHECK(a != b);
}

TEST_CASE("slides: Scripture has 17 templates in four packs and keeps the first three where they were")
{
	using namespace laycue::scripture;
	const auto &all = scriptureTemplates();
	CHECK_EQ(int(all.size()), 17);
	CHECK_EQ(all[0].style.layout, int(Layout::LowerThird));
	CHECK_EQ(all[1].style.layout, int(Layout::Card));
	CHECK_EQ(all[2].style.layout, int(Layout::FullScreen));
	std::set<std::string> names;
	int withDecor = 0;
	for (const auto &t : all) {
		names.insert(t.nameKey);
		SlideStyle copy = t.style;
		sanitize(copy);
		CHECK(copy == t.style); // a built-in look is already in range
		CHECK(t.pack >= 0 && t.pack < kScripturePackCount);
		if (t.style.decor != int(Decor::None))
			++withDecor;
	}
	CHECK_EQ(int(names.size()), 17);
	CHECK(withDecor >= 2); // some of the verses have the looping Bible beside them
	int total = 0;
	for (int pack = 0; pack < kScripturePackCount; ++pack) {
		const std::vector<int> members = templatesOfPack(pack);
		CHECK_EQ(int(members.size()), pack == 3 ? 5 : 4);
		total += int(members.size());
		for (int index : members)
			CHECK_EQ(all[size_t(index)].pack, pack);
	}
	CHECK_EQ(total, 17);
	// Every name the panel shows exists in the language file.
	const std::string ini = readLocale("scripture");
	for (const auto &t : all)
		CHECK(hasKey(ini, t.nameKey));
	for (int pack = 0; pack < kScripturePackCount; ++pack)
		CHECK(hasKey(ini, scripturePackKey(pack)));
}

TEST_CASE("slides: Lyrics has 12 templates in four packs")
{
	using namespace laycue::lyrics;
	const auto &all = lyricsTemplates();
	CHECK_EQ(int(all.size()), 12);
	CHECK_EQ(all[0].style.fill, int(Fill::Solid)); // Black, White, Faded and None keep their places
	CHECK_EQ(all[3].style.fill, int(Fill::None));
	const std::string ini = readLocale("lyrics");
	std::set<std::string> names;
	for (const auto &t : all) {
		names.insert(t.nameKey);
		SlideStyle copy = t.style;
		sanitize(copy);
		CHECK(copy == t.style);
		CHECK(hasKey(ini, t.nameKey));
	}
	CHECK_EQ(int(names.size()), 12);
	for (int pack = 0; pack < kLyricsPackCount; ++pack) {
		CHECK_EQ(int(templatesOfPack(pack).size()), pack == 0 || pack == 3 ? 4 : 2);
		CHECK(hasKey(ini, lyricsPackKey(pack)));
	}
}

TEST_CASE("slides: every word the look editor shows is in both language files")
{
	const char *keys[] = {"Tab",
			      "Rule",
			      "Quote",
			      "Align",
			      "AlignLeft",
			      "AlignCentre",
			      "AlignRight",
			      "Font",
			      "FontModern",
			      "FontClean",
			      "FontCompact",
			      "FontClassic",
			      "FontDisplay",
			      "Between",
			      "BetweenFade",
			      "BetweenCut",
			      "Line",
			      "LineFlat",
			      "LineGradient",
			      "LineSweep",
			      "LineNone",
			      "Animation",
			      "AnimationNone",
			      "AnimationOpenBible",
			      "AnimationGlowBible",
			      "Split",
			      "Torn",
			      "FillMoving",
			      "ColourThird",
			      "AnimationScroll",
			      "AnimationDove",
			      "AnimationCross",
			      "AnimationSunrise",
			      "AnimationNotes",
			      "AnimationEqualizer",
			      "AnimationVinyl"};
	for (const char *plugin : {"scripture", "lyrics"}) {
		const std::string ini = readLocale(plugin);
		for (const char *key : keys)
			CHECK(hasKey(ini, std::string("Laycue.Slides.") + key));
	}
}

TEST_CASE("slides: moving looks are known to move, still ones are not")
{
	SlideStyle still;
	CHECK(!isAnimated(still));
	SlideStyle moving = still;
	moving.fill = int(Fill::Moving);
	CHECK(isAnimated(moving));
	SlideStyle pictured = still;
	pictured.decor = int(Decor::Notes);
	CHECK(isAnimated(pictured));
	SlideStyle sweeping = still;
	sweeping.line = int(AccentLine::Sweep);
	CHECK(isAnimated(sweeping));
	// Lyrics has music pictures, Scripture has Bible ones.
	int music = 0;
	for (const auto &t : laycue::lyrics::lyricsTemplates())
		music += t.style.decor >= int(Decor::Notes) ? 1 : 0;
	CHECK(music >= 3);
	for (const auto &t : laycue::scripture::scriptureTemplates())
		CHECK(t.style.decor < int(Decor::Notes));
}

TEST_CASE("slides: the accent line is a choice, and only its sweep moves")
{
	SlideStyle still;
	CHECK(!isAnimated(still));
	SlideStyle sweeping = still;
	sweeping.line = int(AccentLine::Sweep);
	CHECK(isAnimated(sweeping));
	sweeping.line = int(AccentLine::Gradient);
	CHECK(!isAnimated(sweeping));
	sweeping.line = int(AccentLine::None);
	CHECK(!isAnimated(sweeping));
	// Whatever a saved file or older copy holds, the line mode stays in range.
	SlideStyle stale = still;
	stale.line = 9;
	sanitize(stale);
	CHECK_EQ(stale.line, kAccentLineCount - 1);
	stale.line = -3;
	sanitize(stale);
	CHECK_EQ(stale.line, 0);
}
