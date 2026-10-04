#include "check.hpp"

#include "core/easing.hpp"
#include "core/ticker.hpp"
#include "core/title-clock.hpp"
#include "core/title-templates.hpp"

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

TEST_CASE("titles: there are 20 built-in templates with unique ids and sensible values")
{
	const auto &all = builtInTemplates();
	CHECK_EQ(int(all.size()), 20);
	std::set<std::string> ids;
	for (const auto &t : all) {
		ids.insert(t.id);
		CHECK(findTemplate(t.id) == &t);
		CHECK(t.inSeconds > 0.0 && t.inSeconds < 3.0);
		CHECK(t.outSeconds > 0.0 && t.outSeconds <= t.inSeconds); // exits are never slower than entrances
		if (t.tickerSpeed > 0.0)
			CHECK(t.category == Category::Ticker);
	}
	CHECK_EQ(int(ids.size()), 20);
	CHECK(findTemplate("nope") == nullptr);
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
						     {"radius", "9999"}, {"hold", "-4"}, {"speed", "nan"}};
	const Overrides o = fromMap(broken);
	CHECK(findTemplate(o.baseId) != nullptr);
	CHECK(!o.hasAccent);
	CHECK(!o.hasMotionIn);
	CHECK(o.cornerRadius <= 60.0);
	CHECK(o.holdSeconds >= 0.0);
}
