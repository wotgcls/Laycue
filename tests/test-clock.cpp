#include "check.hpp"
#include "core/clock-core.hpp"

using namespace laycue::clock;

TEST_CASE("clock: a countdown counts down, pauses and finishes")
{
	Timer t;
	t.configure(Mode::Countdown, 300.0);
	CHECK(!t.running());
	CHECK(t.read(0.0).seconds > 299.9 && t.read(0.0).seconds <= 300.0); // idle shows the full time
	t.start(100.0);
	CHECK(t.running());
	const Reading r = t.read(160.0);
	CHECK(r.seconds > 239.9 && r.seconds < 240.1);
	CHECK(!r.finished);
	t.pause(160.0);
	CHECK(t.read(500.0).seconds > 239.9 && t.read(500.0).seconds < 240.1); // paused: time stands still
	t.start(600.0);
	CHECK(t.read(630.0).seconds > 209.9 && t.read(630.0).seconds < 210.1);
	const Reading end = t.read(600.0 + 241.0);
	CHECK(end.finished);
	CHECK(end.level == Level::Over);
	CHECK_EQ(end.seconds, 0.0); // without overtime it stays at zero
	CHECK(end.fraction >= 1.0);
}

TEST_CASE("clock: overtime keeps counting after zero, and the warning level appears near the end")
{
	Timer t;
	t.configure(Mode::Countdown, 120.0);
	t.setWarning(30.0);
	t.setOvertime(true);
	t.start(0.0);
	CHECK(t.read(10.0).level == Level::Normal);
	CHECK(t.read(95.0).level == Level::Warning);
	const Reading over = t.read(132.0);
	CHECK(over.overtime);
	CHECK(over.finished);
	CHECK(over.seconds > 11.9 && over.seconds < 12.1);
	CHECK(over.level == Level::Over);
}

TEST_CASE("clock: a count up, reset and adjust")
{
	Timer t;
	t.configure(Mode::CountUp, 0.0);
	t.start(10.0);
	CHECK(t.read(70.0).seconds > 59.9 && t.read(70.0).seconds < 60.1);
	t.adjust(30.0, 70.0);
	CHECK(t.read(70.0).seconds > 89.9 && t.read(70.0).seconds < 90.1);
	t.adjust(-500.0, 70.0); // never below zero
	CHECK(t.read(70.0).seconds < 0.1);
	t.reset();
	CHECK(!t.running());
	CHECK(t.read(200.0).seconds < 0.1);

	Timer c;
	c.configure(Mode::Countdown, 300.0);
	c.start(0.0);
	c.adjust(60.0, 10.0); // one more minute
	CHECK(c.read(10.0).seconds > 349.9 && c.read(10.0).seconds < 350.1);
	c.adjust(-1000.0, 10.0); // cannot go negative
	CHECK(c.duration() >= 0.0);
}

TEST_CASE("clock: times are printed the way a person reads them")
{
	CHECK_EQ(formatTime(300.0, true), std::string("05:00"));
	CHECK_EQ(formatTime(299.2, true), std::string("05:00")); // a countdown rounds up: zero only at zero
	CHECK_EQ(formatTime(0.4, true), std::string("00:01"));
	CHECK_EQ(formatTime(0.0, true), std::string("00:00"));
	CHECK_EQ(formatTime(59.9, false), std::string("00:59")); // a count up rounds down
	CHECK_EQ(formatTime(3725.0, false), std::string("1:02:05"));
	CHECK_EQ(formatTime(65.0, false, true), std::string("0:01:05"));
	CHECK_EQ(formatTime(-5.0, true), std::string("00:00"));
}

TEST_CASE("clock: durations and times of day are read from what is typed")
{
	double s = 0.0;
	CHECK(parseDuration("5", s) && s == 300.0);
	CHECK(parseDuration("5:30", s) && s == 330.0);
	CHECK(parseDuration(" 1:30:00 ", s) && s == 5400.0);
	CHECK(parseDuration("90s", s) && s == 90.0);
	CHECK(parseDuration("10 min", s) && s == 600.0);
	CHECK(parseDuration("2h", s) && s == 7200.0);
	CHECK(!parseDuration("", s));
	CHECK(!parseDuration("abc", s));
	CHECK(!parseDuration("5:75", s));
	CHECK(!parseDuration("5:", s));
	CHECK(!parseDuration("-5", s));
	CHECK(!parseDuration("999999", s));

	int t = 0;
	CHECK(parseTimeOfDay("10:30", t) && t == 10 * 3600 + 30 * 60);
	CHECK(parseTimeOfDay("9", t) && t == 9 * 3600);
	CHECK(parseTimeOfDay("9:05 pm", t) && t == 21 * 3600 + 5 * 60);
	CHECK(parseTimeOfDay("12:00 am", t) && t == 0);
	CHECK(parseTimeOfDay("12:15pm", t) && t == 12 * 3600 + 15 * 60);
	CHECK(parseTimeOfDay("21:00", t) && t == 21 * 3600);
	CHECK(!parseTimeOfDay("24:00", t));
	CHECK(!parseTimeOfDay("13 pm", t));
	CHECK(!parseTimeOfDay("ten", t));
	CHECK(!parseTimeOfDay("10:60", t));
}

TEST_CASE("clock: counting down to a time of day goes to tomorrow when it has passed")
{
	CHECK_EQ(secondsUntilTimeOfDay(9 * 3600, 10 * 3600), 3600.0);
	CHECK_EQ(secondsUntilTimeOfDay(10 * 3600, 10 * 3600), 0.0);
	CHECK_EQ(secondsUntilTimeOfDay(10 * 3600 + 1, 10 * 3600), 86399.0);
	CHECK_EQ(secondsUntilTimeOfDay(23 * 3600, 1 * 3600), 2.0 * 3600.0); // across midnight
}
