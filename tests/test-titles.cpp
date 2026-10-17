#include "check.hpp"

#include "core/easing.hpp"
#include "core/ticker.hpp"
#include "core/title-clock.hpp"
#include "core/title-templates.hpp"

#include <map>
#include <set>

using namespace laycue::titles;

TEST_CASE("titles: easing curves start at 0, end at 1 and clamp")
{
	for (double (*f)(double) : {easeOutCubic, easeInCubic, easeInOutCubic, easeOutExpo, easeInExpo, easeOutBack}) {
		CHECK(f(0.0) < 0.001);
		CHECK(f(1.0) > 0.999 && f(1.0) < 1.001);
		CHECK(f(-5.0) < 0.001);
		CHECK(f(9.0) > 0.999 && f(9.0) < 1.001);
	}
	CHECK(easeOutExpo(0.1) > 0.4); // arrives fast
	CHECK(easeInExpo(0.5) < 0.05); // leaves slowly at first
	double peak = 0;
	for (int i = 0; i <= 100; ++i)
		peak = std::max(peak, easeOutBack(i / 100.0));
	CHECK(peak > 1.0 && peak < 1.12); // a small overshoot
}

TEST_CASE("titles: stage maps part of a timeline to its own 0..1")
{
	CHECK(stage(0.0, 0.25, 0.5) == 0.0);
	CHECK(stage(0.5, 0.25, 0.5) == 0.5);
	CHECK(stage(1.0, 0.25, 0.5) == 1.0);
	CHECK(stage(0.3, 0.3, 0.0) == 1.0);
}

TEST_CASE("titles: the clock goes hidden, in, hold, out, hidden")
{
	TitleClock clock;
	clock.configure(1.0, 0.5, 0.0);
	CHECK(clock.phase() == TitleClock::Phase::Hidden);
	clock.show();
	CHECK(clock.phase() == TitleClock::Phase::In);
	clock.advance(0.5);
	CHECK(clock.in() > 0.49 && clock.in() < 0.51);
	clock.advance(0.6);
	CHECK(clock.phase() == TitleClock::Phase::Hold);
	CHECK(clock.in() == 1.0);
	clock.advance(100.0); // no auto-hide: stays
	CHECK(clock.phase() == TitleClock::Phase::Hold);
	CHECK(clock.holdRemaining() < 0.0);
	clock.hide();
	CHECK(clock.phase() == TitleClock::Phase::Out);
	clock.advance(0.25);
	CHECK(clock.out() > 0.49 && clock.out() < 0.51);
	clock.advance(0.3);
	CHECK(clock.phase() == TitleClock::Phase::Hidden);
	CHECK(!clock.visible());
}

TEST_CASE("titles: a title hides itself after its hold time")
{
	TitleClock clock;
	clock.configure(0.5, 0.5, 4.0);
	clock.show();
	clock.advance(0.5);
	CHECK(clock.phase() == TitleClock::Phase::Hold);
	CHECK(clock.holdRemaining() > 3.9);
	clock.advance(3.9);
	CHECK(clock.phase() == TitleClock::Phase::Hold);
	clock.advance(0.2);
	CHECK(clock.phase() == TitleClock::Phase::Out);
}

TEST_CASE("titles: hiding while arriving leaves from where it is, and showing while leaving turns around")
{
	TitleClock clock;
	clock.configure(1.0, 1.0, 0.0);
	clock.show();
	clock.advance(0.4);
	const double reached = clock.in();
	clock.hide();
	CHECK(clock.phase() == TitleClock::Phase::Out);
	CHECK(clock.in() == reached); // frozen, so nothing jumps
	clock.advance(0.5);
	clock.show(); // turn around halfway out
	CHECK(clock.phase() == TitleClock::Phase::In);
	CHECK(clock.out() == 0.0);
	CHECK(clock.in() > 0.15 && clock.in() < reached); // it was less visible than before
}

TEST_CASE("titles: zero-length animations jump straight to the end state")
{
	TitleClock clock;
	clock.configure(0.0, 0.0, 0.0);
	clock.show();
	CHECK(clock.phase() == TitleClock::Phase::Hold);
	clock.hide();
	CHECK(clock.phase() == TitleClock::Phase::Hidden);
}

TEST_CASE("titles: hide on a hidden title and show on a shown one are harmless")
{
	TitleClock clock;
	clock.configure(1.0, 1.0, 0.0);
	clock.hide();
	CHECK(clock.phase() == TitleClock::Phase::Hidden);
	clock.show();
	clock.show();
	CHECK(clock.phase() == TitleClock::Phase::In);
	clock.advance(-1.0); // time never runs backwards
	CHECK(clock.in() == 0.0);
}

TEST_CASE("titles: the ticker wraps and says how many copies fill the view")
{
	TickerState ticker;
	ticker.configure(1000.0, 100.0);
	ticker.advance(5.0);
	CHECK(ticker.offset() > 499.9 && ticker.offset() < 500.1);
	ticker.advance(7.0); // 1200 px later: wrapped
	CHECK(ticker.offset() > 199.9 && ticker.offset() < 200.1);
	CHECK(ticker.copiesNeeded(1920.0) == 3);
	CHECK(ticker.copiesNeeded(500.0) == 2);
	CHECK(ticker.copyX(0) < 0.0);
	CHECK(ticker.copyX(1) > 0.0);
	ticker.configure(0.0, 100.0); // a zero-width strip must not divide by zero
	ticker.advance(1.0);
	CHECK(ticker.offset() >= 0.0);
}

TEST_CASE("titles: there are 68 built-in templates with unique ids and sensible values")
{
	const auto &all = builtInTemplates();
	CHECK_EQ(int(all.size()), 68);
	std::set<std::string> ids;
	for (const auto &t : all) {
		ids.insert(t.id);
		CHECK(findTemplate(t.id) == &t);
		CHECK(t.inSeconds > 0.0 && t.inSeconds < 3.0);
		CHECK(t.outSeconds > 0.0 && t.outSeconds <= t.inSeconds); // exits are never slower than entrances
		if (t.tickerSpeed > 0.0)
			CHECK(t.category == Category::Ticker);
	}
	CHECK_EQ(int(ids.size()), 68);
	CHECK(findTemplate("nope") == nullptr);
}

TEST_CASE("titles: every category has templates, the Live and Social ones are where they belong, no scripture")
{
	std::map<Category, int> perCategory;
	for (const auto &t : builtInTemplates())
		++perCategory[t.category];
	for (Category c : {Category::People, Category::Worship, Category::Announce, Category::Ticker, Category::Live,
			   Category::Social, Category::News, Category::Notes})
		CHECK(perCategory[c] >= 2);
	CHECK_EQ(perCategory[Category::Notes], 4);
	for (const char *id :
	     {"logo-sweep", "logo-spin", "logo-pulse", "logo-float", "logo-flip", "logo-ripple", "logo-hop",
	      "logo-reveal", "logo-orbit", "logo-water", "logo-glass-cube", "logo-glass-disc"}) {
		const TitleTemplate *t = findTemplate(id);
		CHECK(t != nullptr);
		if (t) {
			CHECK(t->usesLogo);
			CHECK(t->pulseSeconds > 0.0); // the logo effect repeats while the title is up
		}
	}
	const TitleTemplate *crawl = findTemplate("desk-crawl");
	CHECK(crawl != nullptr);
	if (crawl)
		CHECK(!supportsPlacement(*crawl));
	CHECK_EQ(perCategory[Category::Social],
		 8); // the generic handle, the five platforms, Subscribe + Bell and Hashtag
	for (const char *id :
	     {"social-youtube", "social-instagram", "social-tiktok", "social-facebook", "social-mixlr"}) {
		const TitleTemplate *t = findTemplate(id);
		CHECK(t != nullptr);
		if (t)
			CHECK(t->category == Category::Social && t->usesTag);
	}
	for (const char *id : {"live-badge", "on-air"}) {
		const TitleTemplate *t = findTemplate(id);
		CHECK(t != nullptr);
		if (t) {
			CHECK(t->category == Category::Live);
			CHECK(isContinuous(*t)); // the light pulses, so it is redrawn every frame
		}
	}
	CHECK(findTemplate("scripture-bar") == nullptr);
	CHECK(findTemplate("verse-card") == nullptr);
}

TEST_CASE("titles: colours parse and print")
{
	Rgba c;
	CHECK(parseColour("#FFB31A", c));
	CHECK(c.r == 255 && c.g == 0xB3 && c.b == 0x1A && c.a == 255);
	CHECK(parseColour("#11111080", c));
	CHECK(c.a == 0x80);
	CHECK(!parseColour("FFB31A", c));
	CHECK(!parseColour("#GGGGGG", c));
	CHECK(!parseColour("#12345", c));
	CHECK_EQ(colourToString(Rgba{1, 2, 3, 255}), std::string("#010203FF"));
}

TEST_CASE("titles: custom templates keep only what was changed and survive a save and load")
{
	Overrides o;
	o.baseId = "clean-bar";
	o.name = "Sunday";
	o.hasAccent = true;
	o.palette.accent = Rgba{0x2E, 0x8B, 0xFF, 255};
	o.hasMotionIn = true;
	o.motionIn = MotionKind::Pop;
	o.hasHold = true;
	o.holdSeconds = 6.5;
	const Overrides back = fromMap(toMap(o));
	CHECK_EQ(back.baseId, std::string("clean-bar"));
	CHECK_EQ(back.name, std::string("Sunday"));
	CHECK(back.hasAccent && back.palette.accent == o.palette.accent);
	CHECK(!back.hasPanel);
	CHECK(back.hasMotionIn && back.motionIn == MotionKind::Pop);
	CHECK(back.hasHold && back.holdSeconds > 6.4 && back.holdSeconds < 6.6);

	const TitleTemplate base = *findTemplate("clean-bar");
	const TitleTemplate custom = resolve(base, back);
	CHECK(custom.palette.accent == o.palette.accent);
	CHECK(custom.palette.panel == base.palette.panel); // untouched parts follow the built-in design
	CHECK(custom.motionIn == MotionKind::Pop);
	CHECK(custom.holdSeconds > 6.4);
}

TEST_CASE("titles: broken custom data is repaired, never trusted")
{
	std::map<std::string, std::string> broken = {{"base", "gone"},   {"accent", "red"}, {"in", "spin"},
						     {"radius", "9999"}, {"hold", "-4"},    {"speed", "nan"}};
	const Overrides o = fromMap(broken);
	CHECK(findTemplate(o.baseId) != nullptr);
	CHECK(!o.hasAccent);
	CHECK(!o.hasMotionIn);
	CHECK(o.cornerRadius <= 60.0);
	CHECK(o.holdSeconds >= 0.0);
}

#include "core/library.hpp"

namespace {
laycue::titles::TitleContent contentOf(const std::string &a, const std::string &b, const std::string &c)
{
	laycue::titles::TitleContent t;
	t.line1 = a;
	t.line2 = b;
	t.tag = c;
	return t;
}
} // namespace

TEST_CASE("titles library: custom templates are saved, replaced by name, and removed safely")
{
	Library lib;
	Overrides o;
	o.baseId = "pop-card";
	o.name = "Sunday";
	CHECK(saveCustom(lib, o));
	CHECK(saveCustom(lib, o)); // same name replaces, doesn't duplicate
	CHECK_EQ(int(lib.customs.size()), 1);
	CHECK_EQ(uniqueCustomName(lib, "Sunday"), std::string("Sunday 2"));
	CHECK_EQ(uniqueCustomName(lib, "Easter"), std::string("Easter"));

	lib.selected = customId("Sunday");
	lib.queue.push_back({customId("Sunday"), contentOf("A", "B", "")});
	std::string base;
	const auto resolved = resolveStyle(lib, lib.selected, &base);
	CHECK_EQ(base, std::string("pop-card"));
	CHECK_EQ(resolved.id, std::string("pop-card"));

	CHECK(removeCustom(lib, "Sunday"));
	CHECK_EQ(lib.selected, std::string("pop-card")); // falls back to the base design, never breaks
	CHECK_EQ(lib.queue[0].styleId, std::string("pop-card"));
	CHECK(!removeCustom(lib, "Sunday"));
}

TEST_CASE("titles library: an unknown style id resolves to the first built-in template")
{
	Library lib;
	std::string base;
	const auto t = resolveStyle(lib, "custom:gone", &base);
	CHECK_EQ(base, builtInTemplates().front().id);
	CHECK_EQ(t.id, builtInTemplates().front().id);
	resolveStyle(lib, "no-such-built-in", &base);
	CHECK_EQ(base, builtInTemplates().front().id);
}

TEST_CASE("titles library: the queue keeps its order and has a limit")
{
	Library lib;
	for (int i = 0; i < 3; ++i)
		CHECK(enqueue(lib, {"clean-bar", contentOf(std::to_string(i), "", "")}));
	CHECK(moveInQueue(lib, 2, 0));
	CHECK_EQ(lib.queue[0].content.line1, std::string("2"));
	CHECK(!moveInQueue(lib, 5, 0));
	QueueItem out;
	CHECK(popQueue(lib, out));
	CHECK_EQ(out.content.line1, std::string("2"));
	CHECK(removeFromQueue(lib, 0));
	CHECK_EQ(int(lib.queue.size()), 1);
	CHECK(!removeFromQueue(lib, 9));
	lib.queue.clear();
	for (size_t i = 0; i < kMaxQueue; ++i)
		CHECK(enqueue(lib, {"clean-bar", {}}));
	CHECK(!enqueue(lib, {"clean-bar", {}}));
	QueueItem none;
	Library empty;
	CHECK(!popQueue(empty, none));
}

TEST_CASE("titles library: sanitize repairs a damaged file")
{
	Library lib;
	lib.selected = "custom:ghost";
	lib.holdOverride = 99999;
	lib.queue.push_back({"nope", contentOf(std::string(5000, 'x'), "", "")});
	Overrides nameless;
	nameless.baseId = "clean-bar";
	lib.customs.push_back(nameless);
	sanitize(lib);
	CHECK(lib.customs.empty());
	CHECK(findTemplate(lib.selected) != nullptr);
	CHECK(findTemplate(lib.queue[0].styleId) != nullptr);
	CHECK(lib.queue[0].content.line1.size() <= 600);
	CHECK_EQ(lib.holdOverride, -1);
}

TEST_CASE("titles: placement is remembered per style, kept in range and only for titles that do not span the picture")
{
	Library lib;
	CHECK(placementFor(lib, "clean-bar") == Placement());
	setPlacement(lib, "clean-bar", {1, 130});
	CHECK_EQ(placementFor(lib, "clean-bar").align, 1);
	CHECK_EQ(placementFor(lib, "clean-bar").percent, 130);
	CHECK(placementFor(lib, "social-youtube") == Placement()); // other styles keep their own
	setPlacement(lib, "clean-bar", {7, 999});                  // out of range: clamped
	CHECK_EQ(placementFor(lib, "clean-bar").align, 2);
	CHECK_EQ(placementFor(lib, "clean-bar").percent, kMaxSizePercent);
	setPlacement(lib, "clean-bar", {0, 100}); // back to the default: nothing is stored
	CHECK(lib.placements.empty());

	lib.placements["no-such-style"] = {1, 80};
	lib.placements["clean-bar"] = {5, 10};
	sanitize(lib);
	CHECK(lib.placements.count("no-such-style") == 0);
	CHECK_EQ(placementFor(lib, "clean-bar").align, 2);
	CHECK_EQ(placementFor(lib, "clean-bar").percent, kMinSizePercent);

	CHECK(supportsPlacement(*findTemplate("clean-bar")));
	CHECK(supportsPlacement(*findTemplate("social-tiktok")));
	CHECK(!supportsPlacement(*findTemplate("crawl-ticker")));
	CHECK(!supportsPlacement(*findTemplate("headline-ticker")));
	CHECK(!supportsPlacement(*findTemplate("thank-you")));
}

TEST_CASE("titles: the giving card takes rows, a note and a logo; extra colours can be overridden and saved")
{
	const TitleTemplate *g = findTemplate("giving");
	CHECK(g != nullptr);
	if (g) {
		CHECK(g->maxRows >= 2 && g->usesNote && g->usesLogo && g->usesShape2 && g->usesShape3 && g->usesInk);
		CHECK(g->category == Category::Announce);
	}
	CHECK(findTemplate("guest-minister") != nullptr);

	Overrides o;
	o.baseId = "giving";
	o.name = "Mine";
	o.hasShape2 = o.hasShape3 = o.hasInk = true;
	o.palette.shape2 = Rgba{1, 2, 3, 255};
	o.palette.shape3 = Rgba{4, 5, 6, 255};
	o.palette.ink = Rgba{7, 8, 9, 255};
	const Overrides back = fromMap(toMap(o));
	CHECK(back.hasShape2 && back.hasShape3 && back.hasInk);
	CHECK(back.palette.shape3 == o.palette.shape3);
	const TitleTemplate resolved = resolve(*g, back);
	CHECK(resolved.palette.ink == o.palette.ink);
	CHECK(resolved.palette.shape2 == o.palette.shape2);

	Library lib;
	for (size_t i = 0; i < kMaxRows + 4; ++i)
		lib.draft.rows.push_back({"Label", std::string(2000, '7')});
	lib.draft.note = std::string(5000, 'n');
	sanitize(lib);
	CHECK_EQ(lib.draft.rows.size(), kMaxRows);
	CHECK(lib.draft.rows[0].value.size() <= 600);
	CHECK(lib.draft.note.size() <= 600);
}

TEST_CASE("titles: transition speed steps are a small range and never leave it")
{
	CHECK(transitionScale(kDefaultSpeed) == 1.0);
	CHECK(transitionScale(0) > transitionScale(1)); // slower means a bigger multiplier on the times
	CHECK(transitionScale(1) > 1.0 && transitionScale(kSpeedSteps - 1) < 1.0);
	CHECK(transitionScale(-5) == transitionScale(0));
	CHECK(transitionScale(99) == transitionScale(kSpeedSteps - 1));
	CHECK(transitionScale(0) <= 2.0 && transitionScale(kSpeedSteps - 1) >= 0.4); // tight on purpose
	Library lib;
	lib.speed = 42;
	sanitize(lib);
	CHECK_EQ(lib.speed, kSpeedSteps - 1);
}

TEST_CASE("titles: shape width choices apply to name templates only and are kept in range")
{
	for (const char *id : {"clean-bar", "accent-edge", "split-blocks", "pop-card", "corner-tag", "stacked-panels",
			       "guest-minister", "modern-pill", "modern-card"}) {
		const TitleTemplate *t = findTemplate(id);
		CHECK(t != nullptr);
		if (t)
			CHECK(supportsFit(*t));
	}
	for (const char *id : {"news-bar", "social-youtube", "desk-crawl", "logo-sweep", "notes-card"}) {
		const TitleTemplate *t = findTemplate(id);
		CHECK(t != nullptr);
		if (t)
			CHECK(!supportsFit(*t));
	}
	Library lib;
	setPlacement(lib, "clean-bar", {0, 100, 2, 99999});
	CHECK_EQ(placementFor(lib, "clean-bar").fit, 2);
	CHECK_EQ(placementFor(lib, "clean-bar").width, kMaxFitWidth);
	setPlacement(lib, "clean-bar", {0, 100, 7, 10});
	CHECK_EQ(placementFor(lib, "clean-bar").fit, 2);
	CHECK_EQ(placementFor(lib, "clean-bar").width, kMinFitWidth);
	setPlacement(lib, "clean-bar", {0, 100, 0, 700}); // back to the default: nothing is stored
	CHECK(lib.placements.empty());
	// The width only matters in manual mode, so a stale width does not make a "default" placement look changed.
	CHECK((Placement{0, 100, 1, 500} == Placement{0, 100, 1, 900}));
}

TEST_CASE("titles: each layer remembers its own template and text")
{
	Library lib;
	lib.selected = "clean-bar";
	lib.draft.line1 = "Live";
	rememberLayer(lib, "Laycue Title");

	lib.selected = "news-bar";
	lib.draft.line1 = "Crawl text";
	rememberLayer(lib, "Laycue Title 2");

	recallLayer(lib, "Laycue Title");
	CHECK_EQ(lib.selected, std::string("clean-bar"));
	CHECK_EQ(lib.draft.line1, std::string("Live"));
	recallLayer(lib, "Laycue Title 2");
	CHECK_EQ(lib.draft.line1, std::string("Crawl text"));

	// A layer never held before keeps what the panel shows; an empty name is ignored.
	recallLayer(lib, "Laycue Title 3");
	CHECK_EQ(lib.draft.line1, std::string("Crawl text"));
	rememberLayer(lib, "");
	CHECK_EQ(lib.layers.size(), size_t(2));
}

TEST_CASE("titles queue: a service run order keeps its items and walks forward and back")
{
	Library lib;
	for (int i = 0; i < 4; ++i)
		CHECK(enqueue(lib, {"clean-bar", contentOf(std::to_string(i), "", ""), ""}));
	CHECK_EQ(nextQueueIndex(lib), 0); // nothing played yet: Next plays the first
	CHECK_EQ(previousQueueIndex(lib), -1);
	lib.queueCurrent = 1;
	CHECK_EQ(nextQueueIndex(lib), 2);
	CHECK_EQ(previousQueueIndex(lib), 0);
	lib.queueCurrent = 3; // the last one: there is no next
	CHECK_EQ(nextQueueIndex(lib), -1);

	// The marker stays on the title it points to when rows move or go.
	lib.queueCurrent = 1;
	CHECK(moveInQueue(lib, 1, 3));
	CHECK_EQ(lib.queueCurrent, 3);
	CHECK_EQ(lib.queue[3].content.line1, std::string("1"));
	CHECK(moveInQueue(lib, 0, 3)); // a row from before it moves past it
	CHECK_EQ(lib.queueCurrent, 2);
	CHECK_EQ(lib.queue[lib.queueCurrent].content.line1, std::string("1"));
	CHECK(moveInQueue(lib, 3, 0)); // a row from after it moves before it
	CHECK_EQ(lib.queueCurrent, 3);
	CHECK(removeFromQueue(lib, 0));
	CHECK_EQ(lib.queueCurrent, 2);
	CHECK_EQ(lib.queue[lib.queueCurrent].content.line1, std::string("1"));

	CHECK(replaceInQueue(lib, 0, {"news-bar", contentOf("edited", "", ""), "Crawl"}));
	CHECK_EQ(lib.queue[0].layer, std::string("Crawl"));
	CHECK(!replaceInQueue(lib, 9, {}));
}

TEST_CASE("titles queue: saved services")
{
	Library lib;
	CHECK(enqueue(lib, {"clean-bar", contentOf("Welcome", "", ""), ""}));
	CHECK(enqueue(lib, {"clean-bar", contentOf("Offering", "", ""), ""}));
	CHECK(!saveService(lib, "   "));
	CHECK(saveService(lib, "  Sunday 9am "));
	CHECK_EQ(lib.serviceName, std::string("Sunday 9am"));
	CHECK(lib.services.count("Sunday 9am") == 1);

	lib.queue.clear();
	lib.queueCurrent = 3;
	CHECK(loadService(lib, "Sunday 9am"));
	CHECK_EQ(int(lib.queue.size()), 2);
	CHECK_EQ(lib.queueCurrent, -1); // back at the top
	CHECK(!loadService(lib, "Nope"));

	lib.queue.pop_back();
	CHECK(saveService(lib, "Sunday 9am")); // same name replaces
	CHECK_EQ(int(lib.services.size()), 1);
	CHECK_EQ(int(lib.services["Sunday 9am"].size()), 1);

	for (size_t i = 0; i < kMaxServices + 5; ++i)
		saveService(lib, "S" + std::to_string(i));
	CHECK(lib.services.size() <= kMaxServices);

	CHECK(deleteService(lib, "Sunday 9am"));
	CHECK(!deleteService(lib, "Sunday 9am"));

	lib.queueCurrent = 99;
	sanitize(lib);
	CHECK_EQ(lib.queueCurrent, -1);
}

TEST_CASE("titles: the seven new cards exist and can be shown")
{
	for (const char *id : {"coming-up", "event-date", "prayer-request", "welcome", "celebrate", "question",
			       "hashtag", "social-subscribe"}) {
		const TitleTemplate *t = findTemplate(id);
		CHECK(t != nullptr);
		if (t) {
			CHECK(t->usesLine1);
			CHECK(t->holdSeconds > 0.0);
		}
	}
	const TitleTemplate *question = findTemplate("question");
	CHECK(question && question->longText); // a question can run to a few lines
	const TitleTemplate *hash = findTemplate("hashtag");
	CHECK(hash && !hash->usesLine2);
}

TEST_CASE("titles: layers are named after what they show, and the name is followed everywhere")
{
	TitleContent c;
	c.line1 = "Pastor David Okafor";
	CHECK_EQ(layerNameFor(c),
		 std::string("ttl. Pastor David Okafo\xE2\x80\xA6")); // 18 characters, then an ellipsis
	c.line1 = "Pastor Dave";
	CHECK_EQ(layerNameFor(c), std::string("ttl. Pastor Dave"));
	c.line1 = "  Welcome\n   home  ";
	CHECK_EQ(layerNameFor(c), std::string("ttl. Welcome")); // the first line, tidy
	c.line1 = "";
	c.line2 = "Senior pastor";
	CHECK_EQ(layerNameFor(c), std::string("ttl. Senior pastor"));
	c.line2 = "";
	c.tag = "";
	CHECK(layerNameFor(c).empty());
	c.line1 = "\xC3\x89glise Saint-\xC3\x89tienne de Paris"; // accented letters count as one character each
	CHECK(layerNameFor(c).find("ttl. \xC3\x89glise") == 0);

	CHECK(isAutoLayerName("ttl. Pastor", "Laycue Title"));
	CHECK(isAutoLayerName("Laycue Title", "Laycue Title"));
	CHECK(isAutoLayerName("Laycue Title 3", "Laycue Title"));
	CHECK(!isAutoLayerName("Crawl", "Laycue Title")); // the operator's own name is left alone
	CHECK(!isAutoLayerName("Laycue Title Final", "Laycue Title"));

	Library lib;
	lib.targetSource = "Laycue Title";
	lib.layers["Laycue Title"] = LayerState{"clean-bar", c};
	lib.queue.push_back({"clean-bar", c, "Laycue Title"});
	lib.queue.push_back({"clean-bar", c, "Other"});
	lib.services["Sunday"] = {QueueItem{"clean-bar", c, "Laycue Title"}};
	renameLayerRefs(lib, "Laycue Title", "ttl. Pastor");
	CHECK_EQ(lib.targetSource, std::string("ttl. Pastor"));
	CHECK(lib.layers.count("ttl. Pastor") == 1 && lib.layers.count("Laycue Title") == 0);
	CHECK_EQ(lib.queue[0].layer, std::string("ttl. Pastor"));
	CHECK_EQ(lib.queue[1].layer, std::string("Other"));
	CHECK_EQ(lib.services["Sunday"][0].layer, std::string("ttl. Pastor"));
}

TEST_CASE("titles: a bank point shows alone, numbered by its place in the bank")
{
	TitleContent draft;
	draft.line1 = "Faith Under Pressure";
	draft.tag = "Sermon notes";
	draft.rows = {{"1", "one"}, {"2", "two"}, {"3", "three"}};
	const std::vector<std::string> points = {"God is present", "Trust is a decision", "Pressure reveals belief"};

	const TitleContent second = contentForPoint(draft, points, 1);
	CHECK_EQ(second.rows.size(), size_t(1)); // never stacked
	CHECK_EQ(second.rows[0].label, std::string("2"));
	CHECK_EQ(second.rows[0].value, std::string("Trust is a decision"));
	CHECK_EQ(second.line1, std::string("Faith Under Pressure")); // the heading stays
	CHECK_EQ(second.tag, std::string("Sermon notes"));

	CHECK(contentForPoint(draft, points, 9) == draft); // outside the bank: unchanged
}
