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

TEST_CASE("autotag v2: what it is, not just what it is called")
{
	using namespace laycue;
	const auto tagOf = [](SourceFacts f) {
		f.canvasWidth = 1920;
		f.canvasHeight = 1080;
		return classifySource(f);
	};
	SourceFacts base;
	base.name = "Image 7";
	base.kind = SourceKind::Image;
	base.typeId = "image_source";
	base.spot = {0, 0, 1920, 1080};

	// A PNG with no background is an overlay, wherever it sits and however big it is.
	SourceFacts png = base;
	png.seeThrough = 0.62;
	const auto a = tagOf(png);
	CHECK(a && a->why == "seethrough");
	// A full opaque picture is not.
	SourceFacts photo = base;
	photo.seeThrough = 0.0;
	photo.green = 0.0;
	CHECK(tagOf(photo) == std::nullopt);
	// Anything with a green screen is an overlay.
	SourceFacts green = base;
	green.green = 0.7;
	const auto g = tagOf(green);
	CHECK(g && g->why == "green");
	// A Chroma Key filter says the same.
	SourceFacts keyed = base;
	keyed.kind = SourceKind::Browser;
	keyed.keyFilter = true;
	const auto k = tagOf(keyed);
	CHECK(k && k->why == "key");
	// A see-through bar at the bottom is a lower third; a see-through logo in a corner is a logo.
	SourceFacts bar = png;
	bar.spot = {100, 850, 900, 150};
	CHECK_EQ(tagOf(bar)->tag, std::string(tag::lowerThird));
	SourceFacts corner = png;
	corner.spot = {1750, 30, 120, 80};
	CHECK_EQ(tagOf(corner)->tag, std::string(tag::logo));
	// Anywhere else it is a plain overlay.
	SourceFacts middle = png;
	middle.spot = {600, 300, 700, 400};
	CHECK_EQ(tagOf(middle)->tag, std::string(tag::overlay));

	// Text sources are overlays.
	SourceFacts text;
	text.name = "Text 4";
	text.kind = SourceKind::Text;
	text.spot = {300, 400, 800, 120};
	const auto t = tagOf(text);
	CHECK(t && t->why == "text");
	// Laycue's own sources are overlays by design.
	SourceFacts title;
	title.name = "Laycue Title";
	title.typeId = "laycue_title";
	CHECK_EQ(tagOf(title)->tag, std::string(tag::lowerThird));
	CHECK_EQ(tagOf(title)->why, std::string("laycue"));
	SourceFacts clock;
	clock.name = "Laycue Clock";
	clock.typeId = "laycue_clock";
	CHECK_EQ(tagOf(clock)->tag, std::string(tag::clock));
	CHECK_EQ(tagOf(clock)->why, std::string("laycue")); // not "its name", though it says clock

	// A camera is never an overlay, not even with a green screen behind the speaker or "logo" in its name.
	SourceFacts cam;
	cam.name = "Logo cam";
	cam.kind = SourceKind::Camera;
	cam.green = 0.9;
	cam.spot = {1750, 30, 120, 80};
	CHECK(tagOf(cam) == std::nullopt);
	SourceFacts bellevue;
	bellevue.name = "Bellevue Camera";
	bellevue.kind = SourceKind::Camera;
	CHECK(tagOf(bellevue) == std::nullopt);

	// A colour that fills the screen is a background, even a green one; a small colour block is not.
	SourceFacts backdrop;
	backdrop.name = "Colour Source";
	backdrop.kind = SourceKind::Color;
	backdrop.green = 1.0;
	backdrop.spot = {0, 0, 1920, 1080};
	CHECK(tagOf(backdrop) == std::nullopt);

	// A browser source that is big and opaque (a slides page) is not; a small one is.
	SourceFacts page;
	page.name = "Browser";
	page.kind = SourceKind::Browser;
	page.seeThrough = 0.0;
	page.green = 0.0;
	page.spot = {0, 0, 1920, 1080};
	CHECK(tagOf(page) == std::nullopt);
	page.spot = {1400, 100, 400, 300};
	CHECK(tagOf(page) && tagOf(page)->why == "browser");

	// More words in the name count: countdown, timer, giving, subtitles.
	SourceFacts named;
	named.name = "Countdown 5 min";
	CHECK_EQ(tagOf(named)->tag, std::string(tag::clock));
	named.name = "Giving QR";
	CHECK_EQ(tagOf(named)->tag, std::string(tag::overlay));
	named.name = "Subtitles";
	CHECK_EQ(tagOf(named)->tag, std::string(tag::lowerThird));
	named.name = "Doorbell"; // "bell" inside a word is not the bell icon
	CHECK(tagOf(named) == std::nullopt);
}
