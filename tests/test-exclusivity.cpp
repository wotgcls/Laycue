#include "check.hpp"

#include "core/autotag.hpp"
#include "core/exclusivity.hpp"

using laycue::ItemRef;
using laycue::Rule;
using laycue::Scope;
using laycue::TaggedItem;

namespace {

TaggedItem item(const char *scene, int id, const char *source, const char *tagName, laycue::RectF spot)
{
	return TaggedItem{{scene, id}, source, tagName, spot};
}

Rule textRule()
{
	Rule r;
	r.name = "Text";
	r.tags = {laycue::tag::scripture, laycue::tag::lyrics};
	return r;
}

const laycue::RectF bottomBig{100, 800, 1700, 200};
const laycue::RectF bottomSmall{100, 850, 500, 100};
const laycue::RectF topCorner{1700, 20, 150, 100};

} // namespace

TEST_CASE("rules: overlapping overlays in the same rule replace each other")
{
	const TaggedItem shown = item("A", 1, "scripture", laycue::tag::scripture, bottomBig);
	const std::vector<TaggedItem> others{item("A", 2, "lyrics", laycue::tag::lyrics, bottomBig)};
	const auto hide = laycue::itemsToHide(shown, {textRule()}, others);
	CHECK_EQ(hide.size(), size_t(1));
	CHECK(hide[0] == (ItemRef{"A", 2}));
}

TEST_CASE("rules: overlays in different spots stay together")
{
	const TaggedItem shown = item("A", 1, "scripture", laycue::tag::scripture, bottomBig);
	const std::vector<TaggedItem> others{item("A", 2, "lyrics", laycue::tag::lyrics, topCorner)};
	CHECK(laycue::itemsToHide(shown, {textRule()}, others).empty());
}

TEST_CASE("rules: strict hides every other member wherever it is")
{
	Rule r = textRule();
	r.strict = true;
	const TaggedItem shown = item("A", 1, "scripture", laycue::tag::scripture, bottomBig);
	const std::vector<TaggedItem> others{item("A", 2, "lyrics", laycue::tag::lyrics, topCorner)};
	CHECK_EQ(laycue::itemsToHide(shown, {r}, others).size(), size_t(1));
}

TEST_CASE("rules: paused rules do nothing")
{
	Rule r = textRule();
	r.paused = true;
	const TaggedItem shown = item("A", 1, "scripture", laycue::tag::scripture, bottomBig);
	const std::vector<TaggedItem> others{item("A", 2, "lyrics", laycue::tag::lyrics, bottomBig)};
	CHECK(laycue::itemsToHide(shown, {r}, others).empty());
}

TEST_CASE("rules: a tag outside the rule is never touched")
{
	const TaggedItem shown = item("A", 1, "scripture", laycue::tag::scripture, bottomBig);
	const std::vector<TaggedItem> others{item("A", 2, "logo", laycue::tag::logo, bottomBig)};
	CHECK(laycue::itemsToHide(shown, {textRule()}, others).empty());
	// And showing a logo triggers nothing, because logo isn't in the rule.
	const TaggedItem logo = item("A", 3, "logo", laycue::tag::logo, bottomBig);
	const std::vector<TaggedItem> texts{item("A", 4, "lyrics", laycue::tag::lyrics, bottomBig)};
	CHECK(laycue::itemsToHide(logo, {textRule()}, texts).empty());
}

TEST_CASE("rules: same-scene scope ignores other scenes")
{
	const TaggedItem shown = item("A", 1, "scripture", laycue::tag::scripture, bottomBig);
	const std::vector<TaggedItem> others{item("B", 2, "lyrics", laycue::tag::lyrics, bottomBig)};
	CHECK(laycue::itemsToHide(shown, {textRule()}, others).empty());
}

TEST_CASE("rules: all-scenes scope reaches other scenes")
{
	Rule r = textRule();
	r.scope = Scope::AllScenes;
	const TaggedItem shown = item("A", 1, "scripture", laycue::tag::scripture, bottomBig);
	const std::vector<TaggedItem> others{item("B", 2, "lyrics", laycue::tag::lyrics, bottomBig)};
	CHECK_EQ(laycue::itemsToHide(shown, {r}, others).size(), size_t(1));
}

TEST_CASE("rules: chosen scenes only apply inside the chosen scenes")
{
	Rule r = textRule();
	r.scope = Scope::ChosenScenes;
	r.scenes = {"A", "B"};
	const std::vector<TaggedItem> others{item("B", 2, "lyrics", laycue::tag::lyrics, bottomBig),
					     item("C", 3, "lyrics", laycue::tag::lyrics, bottomBig)};
	const auto hide =
		laycue::itemsToHide(item("A", 1, "scripture", laycue::tag::scripture, bottomBig), {r}, others);
	CHECK_EQ(hide.size(), size_t(1));
	CHECK(hide[0] == (ItemRef{"B", 2}));
	// A change in a scene that isn't chosen triggers nothing at all.
	CHECK(laycue::itemsToHide(item("C", 9, "scripture", laycue::tag::scripture, bottomBig), {r}, others).empty());
}

TEST_CASE("rules: the same source in another scene is not a rival")
{
	Rule r = textRule();
	r.scope = Scope::AllScenes;
	const TaggedItem shown = item("A", 1, "scripture", laycue::tag::scripture, bottomBig);
	const std::vector<TaggedItem> others{item("B", 2, "scripture", laycue::tag::scripture, bottomBig)};
	CHECK(laycue::itemsToHide(shown, {r}, others).empty());
}

TEST_CASE("rules: an item never hides itself, and results are not repeated")
{
	Rule a = textRule();
	Rule b = textRule();
	const TaggedItem shown = item("A", 1, "scripture", laycue::tag::scripture, bottomBig);
	const std::vector<TaggedItem> others{item("A", 1, "scripture", laycue::tag::scripture, bottomBig),
					     item("A", 2, "lyrics", laycue::tag::lyrics, bottomBig)};
	CHECK_EQ(laycue::itemsToHide(shown, {a, b}, others).size(), size_t(1));
}

TEST_CASE("rules: default rule covers text overlays but not the logo")
{
	const auto rules = laycue::defaultRules();
	CHECK_EQ(rules.size(), size_t(1));
	const TaggedItem shown = item("A", 1, "name", laycue::tag::lowerThird, bottomSmall);
	const std::vector<TaggedItem> others{item("A", 2, "lyrics", laycue::tag::lyrics, bottomBig),
					     item("A", 3, "logo", laycue::tag::logo, topCorner)};
	const auto hide = laycue::itemsToHide(shown, rules, others);
	CHECK_EQ(hide.size(), size_t(1));
	CHECK(hide[0] == (ItemRef{"A", 2}));
}

TEST_CASE("autotag: names")
{
	using laycue::suggestTag;
	CHECK_EQ(suggestTag("Scripture", {}, 1920, 1080)->tag, std::string(laycue::tag::scripture));
	CHECK_EQ(suggestTag("Bible verse", {}, 1920, 1080)->tag, std::string(laycue::tag::scripture));
	CHECK_EQ(suggestTag("Song Lyrics", {}, 1920, 1080)->tag, std::string(laycue::tag::lyrics));
	CHECK_EQ(suggestTag("Lower Third", {}, 1920, 1080)->tag, std::string(laycue::tag::lowerThird));
	CHECK_EQ(suggestTag("Church LOGO", {}, 1920, 1080)->tag, std::string(laycue::tag::logo));
	CHECK(suggestTag("Camera 1", {}, 1920, 1080) == std::nullopt);
}

TEST_CASE("autotag: position")
{
	using laycue::suggestTag;
	// A wide, low, mid-sized strip looks like a lower third.
	const auto lt = suggestTag("Text 3", {100, 850, 900, 150}, 1920, 1080);
	CHECK(lt && lt->tag == laycue::tag::lowerThird && !lt->fromName);
	// A small box in a corner looks like a logo.
	const auto logo = suggestTag("Image 2", {1750, 30, 120, 80}, 1920, 1080);
	CHECK(logo && logo->tag == laycue::tag::logo);
	// A full-screen source is nothing in particular.
	CHECK(suggestTag("Slides", {0, 0, 1920, 1080}, 1920, 1080) == std::nullopt);
}
