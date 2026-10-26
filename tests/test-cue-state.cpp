#include "check.hpp"

#include "core/cue-state.hpp"

#include <string>

using laycue::CueState;
using laycue::PickResult;

TEST_CASE("tapping a shot cues it as Up next")
{
	CueState s{"worship", ""};
	CHECK(laycue::pickShot(s, "sermon") == PickResult::Cued);
	CHECK_EQ(s.next, std::string("sermon"));
	CHECK(laycue::canTake(s));
}

TEST_CASE("tapping the Up next shot again clears it")
{
	CueState s{"worship", "sermon"};
	CHECK(laycue::pickShot(s, "sermon") == PickResult::Uncued);
	CHECK(s.next.empty());
	CHECK(!laycue::canTake(s));
}

TEST_CASE("tapping the live shot changes nothing")
{
	CueState s{"worship", "sermon"};
	CHECK(laycue::pickShot(s, "worship") == PickResult::AlreadyLive);
	CHECK_EQ(s.next, std::string("sermon"));
}

TEST_CASE("tapping another shot replaces Up next")
{
	CueState s{"worship", "sermon"};
	CHECK(laycue::pickShot(s, "band") == PickResult::Cued);
	CHECK_EQ(s.next, std::string("band"));
}

TEST_CASE("going live moves Up next to Live and clears Up next")
{
	CueState s{"worship", "sermon"};
	laycue::take(s);
	CHECK_EQ(s.live, std::string("sermon"));
	CHECK(s.next.empty());
}

TEST_CASE("pressing Go Live twice never jumps back")
{
	CueState s{"worship", "sermon"};
	laycue::take(s);
	laycue::take(s);
	CHECK_EQ(s.live, std::string("sermon"));
	CHECK(!laycue::canTake(s));
}

TEST_CASE("with a requeue, Go Live fills Up next with the shot that was live")
{
	CueState s{"worship", "sermon"};
	laycue::take(s, "worship");
	CHECK_EQ(s.live, std::string("sermon"));
	CHECK_EQ(s.next, std::string("worship"));
	CHECK(laycue::canTake(s));
	// And going live again does return to the previous shot — the swap OBS Studio Mode does itself.
	laycue::take(s, s.live == "worship" ? std::string("sermon") : std::string("worship"));
	CHECK_EQ(s.live, std::string("worship"));
}

TEST_CASE("a requeue of the live shot itself clears Up next")
{
	CueState s{"worship", "sermon"};
	laycue::take(s, "sermon"); // the caller passed the new live by mistake; nothing oddly cued
	CHECK_EQ(s.live, std::string("sermon"));
	CHECK(s.next.empty());
}

TEST_CASE("nothing cued means Go Live is unavailable")
{
	CueState s{"worship", ""};
	CHECK(!laycue::canTake(s));
	laycue::take(s);
	CHECK_EQ(s.live, std::string("worship"));
}

TEST_CASE("a live change from OBS clears Up next only when it was the cued shot")
{
	CueState s{"worship", "sermon"};
	laycue::liveChanged(s, "band");
	CHECK_EQ(s.next, std::string("sermon"));
	laycue::liveChanged(s, "sermon");
	CHECK(s.next.empty());
	CHECK_EQ(s.live, std::string("sermon"));
}

TEST_CASE("a removed scene is cleared from Live and Up next")
{
	CueState s{"worship", "sermon"};
	laycue::shotRemoved(s, "sermon");
	CHECK(s.next.empty());
	laycue::shotRemoved(s, "worship");
	CHECK(s.live.empty());
}
