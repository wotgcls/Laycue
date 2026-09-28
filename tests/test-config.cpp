#include "check.hpp"

#include "core/config.hpp"

using laycue::Config;

TEST_CASE("config: tags are set, replaced and cleared per source")
{
	Config c;
	laycue::setTag(c, "u1", "Scripture", "scripture");
	CHECK_EQ(laycue::tagOf(c, "u1"), std::string("scripture"));
	laycue::setTag(c, "u1", "Scripture", "lyrics");
	CHECK_EQ(c.tags.size(), size_t(1));
	CHECK_EQ(laycue::tagOf(c, "u1"), std::string("lyrics"));
	laycue::setTag(c, "u1", "Scripture", "");
	CHECK(c.tags.empty());
	CHECK_EQ(laycue::tagOf(c, "missing"), std::string());
}

TEST_CASE("config: sanitize drops broken entries and fixes numbers")
{
	Config c;
	c.version = 0;
	c.settings.insideFadeMs = 99999;
	c.tags = {{"u1", "A", "logo"}, {"u1", "A again", "lyrics"}, {"", "", "logo"}, {"u2", "B", ""}};
	laycue::Rule empty;
	empty.name = "no tags";
	c.rules = {empty, laycue::defaultRules()[0]};
	laycue::Pushback nameless;
	nameless.id = "pb1";
	laycue::Pushback good;
	good.id = "pb2";
	good.name = "Prayers";
	good.feeds = {{"f1", "Pastor"}, {"f2", "Wide"}};
	good.activeFeed = "gone";
	c.pushbacks = {nameless, good};

	laycue::sanitize(c);
	CHECK_EQ(c.version, laycue::kConfigVersion);
	CHECK_EQ(c.settings.insideFadeMs, 3000);
	CHECK_EQ(c.tags.size(), size_t(1));
	CHECK_EQ(c.rules.size(), size_t(1));
	CHECK_EQ(c.pushbacks.size(), size_t(1));
	CHECK_EQ(c.pushbacks[0].activeFeed, std::string("f1")); // unknown active feed falls back to the first
}

TEST_CASE("config: tips are remembered once")
{
	Config c;
	CHECK(!laycue::tipDismissed(c, "shots"));
	laycue::dismissTip(c, "shots");
	laycue::dismissTip(c, "shots");
	CHECK(laycue::tipDismissed(c, "shots"));
	CHECK_EQ(c.dismissedTips.size(), size_t(1));
}

TEST_CASE("config: new pushback ids never repeat")
{
	Config c;
	CHECK_EQ(laycue::newPushbackId(c), std::string("pb1"));
	laycue::Pushback p;
	p.id = "pb1";
	c.pushbacks.push_back(p);
	CHECK_EQ(laycue::newPushbackId(c), std::string("pb2"));
}
