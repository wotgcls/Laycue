#include "core/title-templates.hpp"

#include <algorithm>
#include <cstdio>
#include <cstdlib>

namespace laycue::titles {

namespace {

constexpr Rgba rgb(uint8_t r, uint8_t g, uint8_t b, uint8_t a = 255)
{
	return Rgba{r, g, b, a};
}

// The brand colours, plus the broadcast neutrals the templates use.
constexpr Rgba kInk = rgb(0x11, 0x11, 0x10);
constexpr Rgba kGraphite = rgb(0x1C, 0x1B, 0x19);
constexpr Rgba kCream = rgb(0xF1, 0xEB, 0xDD);
constexpr Rgba kWhite = rgb(0xFF, 0xFF, 0xFF);
constexpr Rgba kAmber = rgb(0xFF, 0xB3, 0x1A);
constexpr Rgba kBlue = rgb(0x2E, 0x8B, 0xFF);
constexpr Rgba kGreen = rgb(0x2F, 0xD1, 0x8B);
constexpr Rgba kRed = rgb(0xFF, 0x4D, 0x2E);
constexpr Rgba kNavy = rgb(0x0B, 0x25, 0x45);
constexpr Rgba kGold = rgb(0xD4, 0xA8, 0x4B);
constexpr Rgba kTeal = rgb(0x0F, 0x76, 0x6E);
constexpr Rgba kNewsBlue = rgb(0x0A, 0x3C, 0xB8);
constexpr Rgba kNewsRed = rgb(0xD4, 0x12, 0x1B);
constexpr Rgba kNewsAmber = rgb(0xFF, 0xB4, 0x00);
constexpr Rgba kPaper = rgb(0xEC, 0xF1, 0xF5);
constexpr Rgba kGiveBlue = rgb(0x12, 0x2F, 0x8A);
constexpr Rgba kGiveOrange = rgb(0xFF, 0x6A, 0x13);
constexpr Rgba kGivePurple = rgb(0x6B, 0x1F, 0x8F);
constexpr Rgba kSlate = rgb(0x1F, 0x29, 0x37);

Rgba withAlpha(Rgba c, uint8_t a)
{
	c.a = a;
	return c;
}

TitleTemplate make(const char *id, Category category, Palette palette, FontFace display, FontFace body, MotionKind in,
		   MotionKind out)
{
	TitleTemplate t;
	t.id = id;
	t.category = category;
	t.palette = palette;
	t.display = display;
	t.body = body;
	t.motionIn = in;
	t.motionOut = out;
	return t;
}

std::vector<TitleTemplate> buildAll()
{
	using F = FontFace;
	using M = MotionKind;
	std::vector<TitleTemplate> all;

	// ---- People: who is speaking ----
	{
		auto t = make("clean-bar", Category::People,
			      {withAlpha(kInk, 235), kAmber, kCream, withAlpha(kCream, 190)},
			      F::BarlowCondensedSemiBold, F::BarlowRegular, M::Slide, M::Slide);
		all.push_back(t);
	}
	{
		auto t = make("accent-edge", Category::People,
			      {withAlpha(kCream, 245), kBlue, kInk, withAlpha(kInk, 170)}, F::BarlowCondensedBold,
			      F::BarlowRegular, M::Wipe, M::Wipe);
		t.cornerRadius = 6;
		all.push_back(t);
	}
	{
		auto t = make("split-blocks", Category::People,
			      {withAlpha(kInk, 240), kRed, kWhite, withAlpha(kWhite, 200)}, F::BarlowCondensedBold,
			      F::BarlowSemiBold, M::Slide, M::Slide);
		all.push_back(t);
	}
	{
		auto t = make("underline", Category::People,
			      {withAlpha(kInk, 0), kGreen, kWhite, withAlpha(kWhite, 220)}, F::BarlowCondensedSemiBold,
			      F::BarlowRegular, M::Rise, M::Fade);
		all.push_back(t);
	}
	{
		auto t = make("pop-card", Category::People,
			      {withAlpha(kGraphite, 245), kAmber, kCream, withAlpha(kCream, 190)},
			      F::BarlowCondensedSemiBold, F::BarlowRegular, M::Pop, M::Fade);
		t.usesTag = true;
		t.cornerRadius = 14;
		t.usesInk = true;
		all.push_back(t);
	}
	{
		auto t = make("corner-tag", Category::People,
			      {withAlpha(kNavy, 245), kGold, kWhite, withAlpha(kWhite, 205)}, F::BarlowCondensedBold,
			      F::BarlowRegular, M::Slide, M::Slide);
		t.usesTag = true;
		t.usesInk = true;
		all.push_back(t);
	}
	{
		auto t = make("stacked-panels", Category::People, {withAlpha(kCream, 250), kBlue, kInk, kWhite},
			      F::BarlowCondensedBold, F::BarlowSemiBold, M::Rise, M::Slide);
		all.push_back(t);
	}
	{
		auto t = make("hairline", Category::People,
			      {withAlpha(kInk, 0), kCream, kCream, withAlpha(kCream, 190)}, F::BarlowSemiBold,
			      F::BarlowRegular, M::Fade, M::Fade);
		all.push_back(t);
	}

	// ---- Guest and giving cards (a logo, tabs above and below the bar) ----
	{
		// An orange tab ("NOW MINISTERING") over a blue bar with the name, and a white tab ("church name") under it.
		auto t = make("guest-minister", Category::People, {kGiveBlue, kGiveOrange, kWhite, kWhite},
			      F::BarlowCondensedBold, F::BarlowSemiBold, M::Slide, M::Slide);
		t.palette.shape2 = kWhite;
		t.palette.ink = rgb(0x0B, 0x25, 0x45);
		t.usesTag = true;
		t.usesLogo = true;
		t.usesShape2 = true;
		t.usesInk = true;
		all.push_back(t);
	}
	{
		// Where to give: a tab ("OFFERING · TITHE"), a bar with the account holder and one column per account
		// (as many as the operator adds), and a note tab under it (a sort code).
		auto t = make("giving", Category::Announce, {kGiveBlue, kGiveOrange, kWhite, kAmber}, F::BebasNeue,
			      F::BarlowCondensedBold, M::Slide, M::Slide);
		t.palette.shape2 = rgb(0xF4, 0xEE, 0xE3);
		t.palette.shape3 = kGivePurple;
		t.palette.ink = rgb(0x0B, 0x25, 0x45);
		t.usesTag = true;
		t.usesNote = true;
		t.usesLogo = true;
		t.maxRows = 4;
		t.usesShape2 = true;
		t.usesShape3 = true;
		t.usesInk = true;
		all.push_back(t);
	}

	// ---- Modern: rounded, friendly cards ----
	const Rgba kOrange = rgb(0xF2, 0x65, 0x22), kCream = rgb(0xF8, 0xF1, 0xE4), kBrown = rgb(0x1B, 0x12, 0x0E);
	const auto modern = [&](const char *id, Palette pal) {
		auto t = make(id, Category::People, pal, F::OutfitBold, F::OutfitSemiBold, M::Slide, M::Slide);
		t.usesShape2 = true;
		t.usesInk = true;
		return t;
	};
	{
		auto t = modern("modern-pill", {kOrange, rgb(0xE0, 0x4F, 0x12), kCream, kCream});
		t.palette.shape2 = kCream;
		t.palette.ink = kBrown;
		t.motionIn = M::Pop;
		all.push_back(t);
	}
	{
		auto t = modern("modern-badge", {kOrange, kOrange, kCream, kCream});
		t.palette.shape2 = kCream;
		t.usesLogo = true;
		all.push_back(t);
	}
	{
		auto t = make("modern-card", Category::People, {kCream, kOrange, kOrange, kBrown}, F::OutfitBold,
			      F::OutfitSemiBold, M::Rise, M::Fade);
		all.push_back(t);
	}
	{
		auto t = make("modern-stack", Category::People, {withAlpha(kOrange, 0), kOrange, kCream, kCream},
			      F::OutfitExtraBold, F::OutfitBold, M::Rise, M::Fade);
		all.push_back(t);
	}
	{
		auto t = modern("modern-logo", {kOrange, kOrange, kCream, kCream});
		t.palette.shape2 = kCream;
		t.usesLogo = true;
		all.push_back(t);
	}

	// ---- News: glass bar, desk bars, network bars ----
	{
		auto t = make("glass-bar", Category::News,
			      {rgb(0x12, 0x3E, 0x8A), rgb(0xFF, 0xB3, 0x00), kWhite, rgb(0xF3, 0xE6, 0xB5)},
			      F::BarlowSemiBold, F::BarlowSemiBold, M::Wipe, M::Fade);
		all.push_back(t);
	}
	const auto desk = [&](const char *id) {
		auto t = make(id, Category::News,
			      {rgb(0xF3, 0xF3, 0xF5), rgb(0xF5, 0xA6, 0x23), rgb(0x2B, 0x2B, 0x2E), kWhite},
			      F::BarlowSemiBold, F::BarlowSemiBold, M::Slide, M::Slide);
		t.palette.shape2 = kWhite;
		t.palette.shape3 = rgb(0x3B, 0x3B, 0x3E);
		t.palette.ink = kWhite;
		t.usesTag = true;
		t.usesShape2 = t.usesShape3 = t.usesInk = true;
		return t;
	};
	{
		auto t = desk("desk-two");
		all.push_back(t);
	}
	{
		auto t = desk("desk-one");
		t.usesLine2 = false;
		all.push_back(t);
	}
	{
		auto t = desk("desk-clock");
		t.pulseSeconds = 60.0; // redrawn every frame so the clock stays right
		all.push_back(t);
	}
	{
		auto t = desk("desk-crawl");
		t.usesNote = true;
		t.tickerSpeed = 120.0;
		t.category = Category::Ticker;
		all.push_back(t);
	}
	{
		auto t = make("network-name", Category::News,
			      {rgb(0x0B, 0x1F, 0x4B), rgb(0xC8, 0x10, 0x2E), kWhite, kWhite}, F::BarlowSemiBold,
			      F::BarlowSemiBold, M::Wipe, M::Fade);
		t.palette.shape2 = kWhite;
		t.palette.ink = rgb(0x0B, 0x1F, 0x4B);
		t.usesLogo = true;
		t.usesShape2 = t.usesInk = true;
		all.push_back(t);
	}
	{
		auto t = make("network-breaking", Category::News,
			      {rgb(0x0B, 0x2A, 0x66), rgb(0xC8, 0x10, 0x2E), kWhite, rgb(0xD8, 0xE0, 0xF0)},
			      F::BarlowSemiBold, F::BarlowSemiBold, M::Wipe, M::Fade);
		t.palette.shape2 = kWhite;
		t.palette.ink = kWhite;
		t.usesTag = true;
		t.usesNote = true;
		t.usesLogo = true;
		t.usesShape2 = t.usesInk = true;
		all.push_back(t);
	}
	{
		auto t = make("angular-logo", Category::People,
			      {rgb(0x14, 0x14, 0x14), rgb(0xF2, 0xF2, 0xF2), kWhite, kWhite}, F::OutfitSemiBold,
			      F::OutfitBold, M::Slide, M::Slide);
		t.palette.shape2 = rgb(0xF2, 0xF2, 0xF2);
		t.palette.ink = rgb(0x22, 0x22, 0x22);
		t.usesLogo = true;
		t.usesShape2 = t.usesInk = true;
		all.push_back(t);
	}

	// ---- Live: logo animations (a picture of the operator's logo, animated) ----
	{
		auto t = make("logo-sweep", Category::Live, {withAlpha(kInk, 0), kOrange, kWhite, kWhite},
			      F::OutfitBold, F::OutfitBold, M::Fade, M::Fade);
		t.palette.ink = kWhite;
		t.anchorTop = true;
		t.usesLine1 = false;
		t.usesLine2 = false;
		t.usesLogo = true;
		t.pulseSeconds = 4.0;
		all.push_back(t);
	}
	{
		auto t = make("logo-spin", Category::Live, {withAlpha(kInk, 0), kOrange, kWhite, kWhite}, F::OutfitBold,
			      F::OutfitBold, M::Fade, M::Fade);
		t.palette.ink = kWhite;
		t.anchorTop = true;
		t.usesLine1 = false;
		t.usesLine2 = false;
		t.usesLogo = true;
		t.pulseSeconds = 5.0;
		all.push_back(t);
	}
	{
		auto t = make("logo-pulse", Category::Live, {withAlpha(kInk, 0), kOrange, kWhite, kWhite},
			      F::OutfitBold, F::OutfitBold, M::Fade, M::Fade);
		t.palette.ink = kWhite;
		t.anchorTop = true;
		t.usesLine1 = false;
		t.usesLine2 = false;
		t.usesLogo = true;
		t.pulseSeconds = 3.5;
		all.push_back(t);
	}
	{
		auto t = make("logo-float", Category::Live, {withAlpha(kInk, 0), kOrange, kWhite, kWhite},
			      F::OutfitBold, F::OutfitBold, M::Fade, M::Fade);
		t.palette.ink = kWhite;
		t.anchorTop = true;
		t.usesLine1 = false;
		t.usesLine2 = false;
		t.usesLogo = true;
		t.pulseSeconds = 5.0;
		all.push_back(t);
	}
	{
		auto t = make("logo-flip", Category::Live, {withAlpha(kInk, 0), kOrange, kWhite, kWhite}, F::OutfitBold,
			      F::OutfitBold, M::Fade, M::Fade);
		t.palette.ink = kWhite;
		t.anchorTop = true;
		t.usesLine1 = false;
		t.usesLine2 = false;
		t.usesLogo = true;
		t.pulseSeconds = 5.0;
		all.push_back(t);
	}
	{
		auto t = make("logo-ripple", Category::Live, {withAlpha(kInk, 0), kOrange, kWhite, kWhite},
			      F::OutfitBold, F::OutfitBold, M::Fade, M::Fade);
		t.palette.ink = kWhite;
		t.anchorTop = true;
		t.usesLine1 = false;
		t.usesLine2 = false;
		t.usesLogo = true;
		t.pulseSeconds = 3.5;
		all.push_back(t);
	}
	{
		auto t = make("logo-hop", Category::Live, {withAlpha(kInk, 0), kOrange, kWhite, kWhite}, F::OutfitBold,
			      F::OutfitBold, M::Fade, M::Fade);
		t.palette.ink = kWhite;
		t.anchorTop = true;
		t.usesLine1 = false;
		t.usesLine2 = false;
		t.usesLogo = true;
		t.pulseSeconds = 4.5;
		all.push_back(t);
	}
	{
		auto t = make("logo-reveal", Category::Live, {withAlpha(kInk, 0), kOrange, kWhite, kWhite},
			      F::OutfitBold, F::OutfitBold, M::Fade, M::Fade);
		t.palette.ink = kWhite;
		t.anchorTop = true;
		t.usesLine1 = false;
		t.usesLine2 = false;
		t.usesLogo = true;
		t.pulseSeconds = 5.0;
		all.push_back(t);
	}
	{
		auto t = make("logo-orbit", Category::Live, {withAlpha(kInk, 0), kOrange, kWhite, kWhite},
			      F::OutfitBold, F::OutfitBold, M::Fade, M::Fade);
		t.palette.ink = kWhite;
		t.anchorTop = true;
		t.usesLine1 = false;
		t.usesLine2 = false;
		t.usesLogo = true;
		t.pulseSeconds = 3.0;
		all.push_back(t);
	}
	{
		auto t = make("logo-water", Category::Live, {withAlpha(kInk, 0), kOrange, kWhite, kWhite},
			      F::OutfitBold, F::OutfitBold, M::Fade, M::Fade);
		t.palette.ink = kWhite;
		t.anchorTop = true;
		t.usesLine1 = false;
		t.usesLine2 = false;
		t.usesLogo = true;
		t.pulseSeconds = 5.0;
		all.push_back(t);
	}
	{
		auto t = make("logo-glass-cube", Category::Live, {withAlpha(kInk, 0), kOrange, kWhite, kWhite},
			      F::OutfitBold, F::OutfitBold, M::Fade, M::Fade);
		t.palette.ink = kWhite;
		t.anchorTop = true;
		t.usesLine1 = false;
		t.usesLine2 = false;
		t.usesLogo = true;
		t.pulseSeconds = 6.0;
		all.push_back(t);
	}
	{
		auto t = make("logo-glass-disc", Category::Live, {withAlpha(kInk, 0), kOrange, kWhite, kWhite},
			      F::OutfitBold, F::OutfitBold, M::Fade, M::Fade);
		t.palette.ink = kWhite;
		t.anchorTop = true;
		t.usesLine1 = false;
		t.usesLine2 = false;
		t.usesLogo = true;
		t.pulseSeconds = 6.0;
		all.push_back(t);
	}
	{
		auto t = make("logo-lockup", Category::Live, {withAlpha(kInk, 0), kOrange, kWhite, kWhite},
			      F::OutfitBold, F::OutfitBold, M::Fade, M::Fade);
		t.palette.ink = kWhite;
		t.anchorTop = true;
		t.usesLine1 = false;
		t.usesLine2 = false;
		t.usesLogo = true;
		t.usesLogoParts = true;
		t.pulseSeconds = 6.0;
		all.push_back(t);
	}

	// ---- Quick Link: a link as a QR code, with a title, a description and a tag (five looks) ----
	{
		auto t = make("quick-link-card", Category::Announce,
			      {rgb(0xF6, 0xF1, 0xE7), kGiveOrange, kInk, rgb(0x4A, 0x47, 0x42)},
			      F::BarlowCondensedBold, F::BarlowRegular, M::Slide, M::Slide);
		t.palette.shape2 = kWhite;
		t.palette.ink = kInk;
		t.usesTag = true;
		t.usesNote = true;
		t.usesShape2 = true;
		t.usesInk = true;
		all.push_back(t);
	}
	{
		auto t = make("quick-link-bar", Category::Announce,
			      {withAlpha(kNavy, 245), kAmber, kWhite, rgb(0xC9, 0xD6, 0xEA)},
			      F::BarlowCondensedBold, F::BarlowRegular, M::Rise, M::Rise);
		t.palette.shape2 = kWhite;
		t.palette.ink = kInk;
		t.usesTag = true;
		t.usesNote = true;
		t.usesShape2 = true;
		t.usesInk = true;
		all.push_back(t);
	}
	{
		auto t = make("quick-link-poster", Category::Announce,
			      {kWhite, kGivePurple, kInk, rgb(0x4A, 0x47, 0x42)},
			      F::BebasNeue, F::BarlowRegular, M::Pop, M::Fade);
		t.palette.shape2 = kWhite;
		t.palette.ink = kInk;
		t.usesTag = true;
		t.usesNote = true;
		t.usesShape2 = true;
		t.usesInk = true;
		all.push_back(t);
	}
	{
		auto t = make("quick-link-glass", Category::Announce,
			      {withAlpha(kInk, 150), kGreen, kWhite, rgb(0xDD, 0xE3, 0xEC)},
			      F::OutfitBold, F::OutfitRegular, M::Fade, M::Fade);
		t.palette.shape2 = kWhite;
		t.palette.ink = kInk;
		t.usesTag = true;
		t.usesNote = true;
		t.usesShape2 = true;
		t.usesInk = true;
		all.push_back(t);
	}
	{
		auto t = make("quick-link-dots", Category::Announce,
			      {kCream, kRed, kInk, rgb(0x4A, 0x47, 0x42)},
			      F::OutfitExtraBold, F::OutfitRegular, M::Pop, M::Fade);
		t.palette.shape2 = kWhite;
		t.palette.ink = kInk;
		t.usesTag = true;
		t.usesNote = true;
		t.usesShape2 = true;
		t.usesInk = true;
		all.push_back(t);
	}
	// ---- Notes: sermon notes and key points ----
	const Rgba kNavyCard = rgb(0x14, 0x21, 0x3D), kGoldNote = rgb(0xFC, 0xA3, 0x11);
	{
		auto t = make("notes-card", Category::Notes,
			      {withAlpha(kNavyCard, 245), kGoldNote, rgb(0xF6, 0xF1, 0xE7), rgb(0xC9, 0xD3, 0xE8)},
			      F::OutfitBold, F::OutfitRegular, M::Rise, M::Fade);
		t.palette.ink = kNavyCard;
		t.usesTag = true;
		t.maxRows = 6;
		t.usesInk = true;
		all.push_back(t);
	}
	{
		auto t = make("notes-points", Category::Notes, {rgb(0xF6, 0xF1, 0xE7), kOrange, kBrown, kBrown},
			      F::OutfitBold, F::OutfitSemiBold, M::Slide, M::Slide);
		t.palette.ink = kWhite;
		t.maxRows = 6;
		t.usesInk = true;
		all.push_back(t);
	}
	{
		auto t = make("notes-key", Category::Notes, {withAlpha(kInk, 242), kGoldNote, kWhite, kWhite},
			      F::OutfitBold, F::OutfitSemiBold, M::Rise, M::Fade);
		t.palette.ink = kInk;
		t.usesLine2 = false;
		t.usesTag = true;
		t.longText = true;
		t.usesInk = true;
		all.push_back(t);
	}
	{
		auto t = make("notes-columns", Category::Notes, {withAlpha(kNavyCard, 245), kGoldNote, kWhite, kWhite},
			      F::OutfitExtraBold, F::OutfitRegular, M::Rise, M::Fade);
		t.palette.ink = kNavyCard;
		t.maxRows = 4;
		t.usesLine2 = false;
		t.usesInk = true;
		all.push_back(t);
	}

	// ---- Worship: series titles, songs, quotes ----
	{
		auto t = make("series-title", Category::Worship,
			      {withAlpha(kInk, 0), kAmber, kWhite, withAlpha(kWhite, 220)}, F::BebasNeue,
			      F::BarlowSemiBold, M::Wipe, M::Wipe);
		t.holdSeconds = 9.0;
		all.push_back(t);
	}
	{
		auto t = make("song-lyric", Category::Worship,
			      {withAlpha(kInk, 0), kAmber, kWhite, withAlpha(kWhite, 235)}, F::BarlowSemiBold,
			      F::BarlowSemiBold, M::Fade, M::Fade);
		t.inSeconds = 0.5;
		t.outSeconds = 0.35;
		all.push_back(t);
	}
	{
		auto t = make("big-quote", Category::Worship,
			      {withAlpha(kInk, 0), kAmber, kWhite, withAlpha(kWhite, 210)}, F::Baskerville,
			      F::BarlowCondensedSemiBold, M::Rise, M::Fade);
		t.longText = true;
		all.push_back(t);
	}

	// ---- Announce: events, alerts, social ----
	// More cards for a service: what is next, an event, a prayer, a welcome, a celebration, a question, a hashtag.
	{
		auto t = make("coming-up", Category::Announce,
			      {withAlpha(kInk, 240), kAmber, kCream, withAlpha(kCream, 200)}, F::BarlowCondensedBold,
			      F::BarlowRegular, M::Slide, M::Slide);
		t.usesTag = true;
		t.usesInk = true;
		t.holdSeconds = 8.0;
		all.push_back(t);
	}
	{
		auto t = make("event-date", Category::Announce,
			      {withAlpha(kInk, 238), kAmber, kCream, withAlpha(kCream, 190)}, F::BarlowCondensedBold,
			      F::BarlowRegular, M::Slide, M::Slide);
		t.usesTag = true;
		t.usesInk = true;
		t.cornerRadius = 18;
		t.holdSeconds = 10.0;
		all.push_back(t);
	}
	{
		auto t = make("prayer-request", Category::Worship,
			      {withAlpha(kInk, 232), kAmber, kCream, withAlpha(kCream, 200)}, F::Baskerville,
			      F::BarlowRegular, M::Rise, M::Fade);
		t.usesTag = true;
		t.usesInk = true;
		t.longText = true;
		t.cornerRadius = 22;
		t.holdSeconds = 12.0;
		all.push_back(t);
	}
	{
		auto t = make("welcome", Category::Announce,
			      {withAlpha(kInk, 0), kAmber, kWhite, withAlpha(kWhite, 225)}, F::BarlowCondensedBold,
			      F::BarlowRegular, M::Rise, M::Fade);
		t.holdSeconds = 7.0;
		all.push_back(t);
	}
	{
		auto t = make("celebrate", Category::Announce,
			      {withAlpha(kCream, 246), kBlue, kInk, withAlpha(kInk, 170)}, F::OutfitBold,
			      F::OutfitRegular, M::Pop, M::Fade);
		t.usesTag = true;
		t.usesInk = true;
		t.palette.ink = kWhite;
		t.cornerRadius = 20;
		t.holdSeconds = 9.0;
		all.push_back(t);
	}
	{
		auto t = make("question", Category::Live, {withAlpha(kCream, 246), kBlue, kInk, withAlpha(kInk, 170)},
			      F::OutfitSemiBold, F::OutfitRegular, M::Rise, M::Fade);
		t.usesTag = true;
		t.usesInk = true;
		t.palette.ink = kWhite;
		t.longText = true;
		t.holdSeconds = 12.0;
		all.push_back(t);
	}
	{
		auto t = make("hashtag", Category::Social,
			      {withAlpha(kInk, 238), kGreen, kCream, withAlpha(kCream, 190)}, F::BarlowCondensedBold,
			      F::BarlowCondensedMedium, M::Slide, M::Slide);
		t.usesLine2 = false;
		t.usesTag = true;
		t.usesInk = true;
		t.cornerRadius = 18;
		t.holdSeconds = 10.0;
		all.push_back(t);
	}
	{
		auto t = make("announcement", Category::Announce,
			      {withAlpha(kTeal, 245), kAmber, kWhite, withAlpha(kWhite, 220)}, F::BarlowCondensedBold,
			      F::BarlowRegular, M::Slide, M::Slide);
		t.usesTag = true;
		t.usesInk = true;
		all.push_back(t);
	}
	{
		auto t = make("breaking", Category::News, {withAlpha(kWhite, 248), kRed, kInk, withAlpha(kInk, 180)},
			      F::BarlowCondensedBold, F::BarlowSemiBold, M::Slide, M::Slide);
		t.usesTag = true;
		t.usesInk = true;
		t.palette.ink = kWhite;
		all.push_back(t);
	}
	{
		auto t = make("news-bar", Category::News,
			      {withAlpha(kSlate, 245), kBlue, kWhite, withAlpha(kWhite, 195)},
			      F::BarlowCondensedSemiBold, F::BarlowRegular, M::Slide, M::Slide);
		t.usesTag = true;
		t.usesInk = true;
		t.palette.ink = kWhite;
		all.push_back(t);
	}
	{
		auto t = make("handle-pill", Category::Social,
			      {withAlpha(kInk, 235), kGreen, kCream, withAlpha(kCream, 190)}, F::BarlowSemiBold,
			      F::BarlowCondensedMedium, M::Pop, M::Fade);
		t.usesLine2 = false;
		t.usesTag = true;
		t.cornerRadius = 40;
		t.holdSeconds = 8.0;
		all.push_back(t);
	}
	{
		auto t = make("thank-you", Category::Announce,
			      {withAlpha(kInk, 0), kAmber, kWhite, withAlpha(kWhite, 220)}, F::BebasNeue,
			      F::BarlowRegular, M::Fade, M::Fade);
		t.fullHeight = true;
		all.push_back(t);
	}

	// ---- Tickers: scrolling text ----
	{
		auto t = make("crawl-ticker", Category::Ticker, {withAlpha(kInk, 240), kAmber, kCream, kInk},
			      F::BarlowCondensedSemiBold, F::BarlowCondensedBold, M::Slide, M::Slide);
		t.usesLine2 = false;
		t.usesTag = true;
		t.tickerSpeed = 150.0;
		t.longText = true;
		all.push_back(t);
	}
	{
		auto t = make("headline-ticker", Category::Ticker, {withAlpha(kNavy, 245), kRed, kWhite, kWhite},
			      F::BarlowCondensedSemiBold, F::BarlowCondensedBold, M::Slide, M::Slide);
		t.usesLine2 = false;
		t.usesTag = true;
		t.tickerSpeed = 130.0;
		t.longText = true;
		all.push_back(t);
	}

	// ---- News: tabbed headline bars in the broadcast-news style ----
	{
		// An amber tab ("NEW AT 5") over a blue headline bar.
		auto t = make("news-flash", Category::News, {kNewsBlue, kNewsAmber, kWhite, rgb(0x0B, 0x25, 0x45)},
			      F::BarlowCondensedBold, F::BarlowSemiBold, M::Slide, M::Slide);
		t.usesLine2 = false;
		t.usesTag = true;
		all.push_back(t);
	}
	{
		// A red tab ("BREAKING NEWS"), a light bar with headline and detail line, and a red LIVE block.
		auto t = make("news-report", Category::News,
			      {rgb(0xE6, 0xE6, 0xE6), kNewsRed, rgb(0x11, 0x11, 0x10), rgb(0x33, 0x33, 0x33)},
			      F::BarlowCondensedBold, F::BarlowSemiBold, M::Slide, M::Slide);
		t.usesTag = true;
		t.pulseSeconds = 1.4;
		t.usesInk = true;
		t.palette.ink = kWhite;
		all.push_back(t);
	}
	{
		// A blue LIVE block, a white headline bar with a blue tab ("CURRENT NEWS") and a strip under it.
		auto t = make("news-desk", Category::News,
			      {kPaper, rgb(0x0B, 0x57, 0xD0), rgb(0x11, 0x11, 0x10), kWhite}, F::BarlowCondensedBold,
			      F::BarlowSemiBold, M::Slide, M::Slide);
		t.usesTag = true;
		t.pulseSeconds = 1.4;
		t.usesInk = true;
		t.palette.ink = kWhite;
		all.push_back(t);
	}

	// ---- Live: show that the stream is on air ----
	{
		// A small badge in the top corner with a pulsing light. Line 1 is the word ("LIVE", "ON AIR",
		// "REPLAY"), line 2 an optional name beside it.
		auto t = make("live-badge", Category::Live,
			      {withAlpha(kInk, 230), kRed, kWhite, withAlpha(kWhite, 210)}, F::BarlowCondensedBold,
			      F::BarlowSemiBold, M::Pop, M::Fade);
		t.usesTag = false;
		t.anchorTop = true;
		t.cornerRadius = 12;
		t.pulseSeconds = 1.4;
		t.usesInk = true;
		t.palette.ink = kWhite;
		all.push_back(t);
	}
	{
		// A wider lower-third: a red LIVE block, the title of the broadcast and a line under it.
		auto t = make("on-air", Category::Live, {withAlpha(kInk, 238), kRed, kCream, withAlpha(kCream, 190)},
			      F::BarlowCondensedSemiBold, F::BarlowRegular, M::Slide, M::Slide);
		t.usesTag = true;
		t.pulseSeconds = 1.4;
		t.usesInk = true;
		t.palette.ink = kWhite;
		all.push_back(t);
	}

	// ---- Social: where to find you (each carries the platform's own icon) ----
	// Subscribe, bell, like and share: a cursor clicks each one in turn (it repeats while the title is up).
	{
		auto t = make("social-subscribe", Category::Social,
			      {withAlpha(kInk, 238), kBlue, kCream, withAlpha(kCream, 190)}, F::BarlowCondensedSemiBold,
			      F::BarlowRegular, M::Slide, M::Slide);
		t.usesTag = true;
		t.cornerRadius = 18;
		t.holdSeconds = 20.0;
		t.pulseSeconds = 10.0; // one full round of clicks
		all.push_back(t);
	}
	// Line 1 is the handle, line 2 the call to action ("Subscribe"), the tag the platform's name.
	for (const char *id :
	     {"social-youtube", "social-instagram", "social-tiktok", "social-facebook", "social-mixlr"}) {
		auto t = make(id, Category::Social, {withAlpha(kInk, 238), kAmber, kCream, withAlpha(kCream, 190)},
			      F::BarlowCondensedSemiBold, F::BarlowRegular, M::Slide, M::Slide);
		t.usesTag = true;
		t.cornerRadius = 18;
		t.holdSeconds = 10.0;
		all.push_back(t);
	}

	return all;
}

std::string fmtDouble(double v)
{
	char buffer[32];
	std::snprintf(buffer, sizeof(buffer), "%.3f", v);
	return buffer;
}

double parseDouble(const std::string &text, double fallback)
{
	char *end = nullptr;
	const double v = std::strtod(text.c_str(), &end);
	return end && end != text.c_str() ? v : fallback;
}

const char *motionName(MotionKind m)
{
	switch (m) {
	case MotionKind::Slide:
		return "slide";
	case MotionKind::Rise:
		return "rise";
	case MotionKind::Fade:
		return "fade";
	case MotionKind::Pop:
		return "pop";
	case MotionKind::Wipe:
		return "wipe";
	}
	return "slide";
}

bool motionFromName(const std::string &name, MotionKind &out)
{
	for (MotionKind m :
	     {MotionKind::Slide, MotionKind::Rise, MotionKind::Fade, MotionKind::Pop, MotionKind::Wipe}) {
		if (name == motionName(m)) {
			out = m;
			return true;
		}
	}
	return false;
}

const char *faceName(FontFace f)
{
	switch (f) {
	case FontFace::BarlowCondensedMedium:
		return "barlow-condensed-medium";
	case FontFace::BarlowCondensedSemiBold:
		return "barlow-condensed-semibold";
	case FontFace::BarlowCondensedBold:
		return "barlow-condensed-bold";
	case FontFace::BarlowRegular:
		return "barlow";
	case FontFace::BarlowSemiBold:
		return "barlow-semibold";
	case FontFace::BebasNeue:
		return "bebas-neue";
	case FontFace::Baskerville:
		return "baskerville";
	case FontFace::BaskervilleItalic:
		return "baskerville-italic";
	case FontFace::OutfitRegular:
		return "outfit";
	case FontFace::OutfitSemiBold:
		return "outfit-semibold";
	case FontFace::OutfitBold:
		return "outfit-bold";
	case FontFace::OutfitExtraBold:
		return "outfit-extrabold";
	}
	return "barlow";
}

bool faceFromName(const std::string &name, FontFace &out)
{
	for (FontFace f :
	     {FontFace::BarlowCondensedMedium, FontFace::BarlowCondensedSemiBold, FontFace::BarlowCondensedBold,
	      FontFace::BarlowRegular, FontFace::BarlowSemiBold, FontFace::BebasNeue, FontFace::Baskerville,
	      FontFace::BaskervilleItalic, FontFace::OutfitRegular, FontFace::OutfitSemiBold, FontFace::OutfitBold,
	      FontFace::OutfitExtraBold}) {
		if (name == faceName(f)) {
			out = f;
			return true;
		}
	}
	return false;
}

} // namespace

bool parseColour(const std::string &text, Rgba &out)
{
	if (text.size() != 7 && text.size() != 9)
		return false;
	if (text[0] != '#')
		return false;
	for (size_t i = 1; i < text.size(); ++i) {
		const char c = text[i];
		const bool hex = (c >= '0' && c <= '9') || (c >= 'a' && c <= 'f') || (c >= 'A' && c <= 'F');
		if (!hex)
			return false;
	}
	const auto byte = [&](size_t at) {
		return static_cast<uint8_t>(std::strtoul(text.substr(at, 2).c_str(), nullptr, 16));
	};
	out.r = byte(1);
	out.g = byte(3);
	out.b = byte(5);
	out.a = text.size() == 9 ? byte(7) : 255;
	return true;
}

std::string colourToString(const Rgba &c)
{
	char buffer[16];
	std::snprintf(buffer, sizeof(buffer), "#%02X%02X%02X%02X", c.r, c.g, c.b, c.a);
	return buffer;
}

std::string nameKey(const TitleTemplate &t)
{
	return "Laycue.Titles.T." + t.id;
}

std::string sampleKey(const TitleTemplate &t, int field)
{
	return nameKey(t) + ".S" + std::to_string(field);
}

std::string fieldKey(const TitleTemplate &t, int field)
{
	return nameKey(t) + ".F" + std::to_string(field);
}

const char *categoryKey(Category category)
{
	switch (category) {
	case Category::People:
		return "Laycue.Titles.Cat.People";
	case Category::Worship:
		return "Laycue.Titles.Cat.Worship";
	case Category::Announce:
		return "Laycue.Titles.Cat.Announce";
	case Category::Ticker:
		return "Laycue.Titles.Cat.Ticker";
	case Category::Live:
		return "Laycue.Titles.Cat.Live";
	case Category::Social:
		return "Laycue.Titles.Cat.Social";
	case Category::News:
		return "Laycue.Titles.Cat.News";
	case Category::Notes:
		return "Laycue.Titles.Cat.Notes";
	}
	return "Laycue.Titles.Cat.People";
}

const std::vector<TitleTemplate> &builtInTemplates()
{
	static const std::vector<TitleTemplate> all = buildAll();
	return all;
}

const TitleTemplate *findTemplate(const std::string &id)
{
	for (const TitleTemplate &t : builtInTemplates()) {
		if (t.id == id)
			return &t;
	}
	return nullptr;
}

TitleTemplate resolve(const TitleTemplate &base, const Overrides &o)
{
	TitleTemplate t = base;
	if (o.hasPanel)
		t.palette.panel = o.palette.panel;
	if (o.hasAccent)
		t.palette.accent = o.palette.accent;
	if (o.hasText)
		t.palette.text = o.palette.text;
	if (o.hasSubtext)
		t.palette.subtext = o.palette.subtext;
	if (o.hasShape2)
		t.palette.shape2 = o.palette.shape2;
	if (o.hasShape3)
		t.palette.shape3 = o.palette.shape3;
	if (o.hasInk)
		t.palette.ink = o.palette.ink;
	if (o.hasDisplay)
		t.display = o.display;
	if (o.hasBody)
		t.body = o.body;
	if (o.hasMotionIn)
		t.motionIn = o.motionIn;
	if (o.hasMotionOut)
		t.motionOut = o.motionOut;
	if (o.hasCornerRadius)
		t.cornerRadius = o.cornerRadius;
	if (o.hasHold)
		t.holdSeconds = o.holdSeconds;
	if (o.hasSpeed && t.tickerSpeed > 0.0)
		t.tickerSpeed = o.tickerSpeed;
	return t;
}

std::map<std::string, std::string> toMap(const Overrides &o)
{
	std::map<std::string, std::string> m;
	m["base"] = o.baseId;
	m["name"] = o.name;
	if (o.hasPanel)
		m["panel"] = colourToString(o.palette.panel);
	if (o.hasAccent)
		m["accent"] = colourToString(o.palette.accent);
	if (o.hasText)
		m["text"] = colourToString(o.palette.text);
	if (o.hasSubtext)
		m["subtext"] = colourToString(o.palette.subtext);
	if (o.hasShape2)
		m["shape2"] = colourToString(o.palette.shape2);
	if (o.hasShape3)
		m["shape3"] = colourToString(o.palette.shape3);
	if (o.hasInk)
		m["ink"] = colourToString(o.palette.ink);
	if (o.hasDisplay)
		m["display"] = faceName(o.display);
	if (o.hasBody)
		m["body"] = faceName(o.body);
	if (o.hasMotionIn)
		m["in"] = motionName(o.motionIn);
	if (o.hasMotionOut)
		m["out"] = motionName(o.motionOut);
	if (o.hasCornerRadius)
		m["radius"] = fmtDouble(o.cornerRadius);
	if (o.hasHold)
		m["hold"] = fmtDouble(o.holdSeconds);
	if (o.hasSpeed)
		m["speed"] = fmtDouble(o.tickerSpeed);
	return m;
}

Overrides fromMap(const std::map<std::string, std::string> &values)
{
	Overrides o;
	const auto get = [&](const char *key) -> const std::string * {
		const auto it = values.find(key);
		return it == values.end() ? nullptr : &it->second;
	};
	if (const auto *v = get("base"))
		o.baseId = *v;
	if (const auto *v = get("name"))
		o.name = *v;
	if (const auto *v = get("panel"))
		o.hasPanel = parseColour(*v, o.palette.panel);
	if (const auto *v = get("accent"))
		o.hasAccent = parseColour(*v, o.palette.accent);
	if (const auto *v = get("text"))
		o.hasText = parseColour(*v, o.palette.text);
	if (const auto *v = get("subtext"))
		o.hasSubtext = parseColour(*v, o.palette.subtext);
	if (const auto *v = get("shape2"))
		o.hasShape2 = parseColour(*v, o.palette.shape2);
	if (const auto *v = get("shape3"))
		o.hasShape3 = parseColour(*v, o.palette.shape3);
	if (const auto *v = get("ink"))
		o.hasInk = parseColour(*v, o.palette.ink);
	if (const auto *v = get("display"))
		o.hasDisplay = faceFromName(*v, o.display);
	if (const auto *v = get("body"))
		o.hasBody = faceFromName(*v, o.body);
	if (const auto *v = get("in"))
		o.hasMotionIn = motionFromName(*v, o.motionIn);
	if (const auto *v = get("out"))
		o.hasMotionOut = motionFromName(*v, o.motionOut);
	if (const auto *v = get("radius")) {
		o.hasCornerRadius = true;
		o.cornerRadius = parseDouble(*v, 0.0);
	}
	if (const auto *v = get("hold")) {
		o.hasHold = true;
		o.holdSeconds = parseDouble(*v, 0.0);
	}
	if (const auto *v = get("speed")) {
		o.hasSpeed = true;
		o.tickerSpeed = parseDouble(*v, 0.0);
	}
	sanitize(o);
	return o;
}

void sanitize(Overrides &o)
{
	if (!findTemplate(o.baseId))
		o.baseId = builtInTemplates().front().id;
	o.cornerRadius = std::clamp(o.cornerRadius, 0.0, 60.0);
	o.holdSeconds = std::clamp(o.holdSeconds, 0.0, 120.0);
	o.tickerSpeed = std::clamp(o.tickerSpeed, 20.0, 600.0);
	if (o.name.size() > 60)
		o.name.resize(60);
}

} // namespace laycue::titles
