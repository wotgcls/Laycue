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
constexpr Rgba kBurgundy = rgb(0x5A, 0x1A, 0x2B);
constexpr Rgba kTeal = rgb(0x0F, 0x76, 0x6E);
constexpr Rgba kSlate = rgb(0x1F, 0x29, 0x37);

Rgba withAlpha(Rgba c, uint8_t a)
{
	c.a = a;
	return c;
}

TitleTemplate make(const char *id, Category category, Palette palette, FontFace display, FontFace body,
		   MotionKind in, MotionKind out)
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
		auto t = make("clean-bar", Category::People, {withAlpha(kInk, 235), kAmber, kCream, withAlpha(kCream, 190)},
			      F::BarlowCondensedSemiBold, F::BarlowRegular, M::Slide, M::Slide);
		all.push_back(t);
	}
	{
		auto t = make("accent-edge", Category::People, {withAlpha(kCream, 245), kBlue, kInk, withAlpha(kInk, 170)},
			      F::BarlowCondensedBold, F::BarlowRegular, M::Wipe, M::Wipe);
		t.cornerRadius = 6;
		all.push_back(t);
	}
	{
		auto t = make("split-blocks", Category::People, {withAlpha(kInk, 240), kRed, kWhite, withAlpha(kWhite, 200)},
			      F::BarlowCondensedBold, F::BarlowSemiBold, M::Slide, M::Slide);
		all.push_back(t);
	}
	{
		auto t = make("underline", Category::People, {withAlpha(kInk, 0), kGreen, kWhite, withAlpha(kWhite, 220)},
			      F::BarlowCondensedSemiBold, F::BarlowRegular, M::Rise, M::Fade);
		all.push_back(t);
	}
	{
		auto t = make("pop-card", Category::People, {withAlpha(kGraphite, 245), kAmber, kCream, withAlpha(kCream, 190)},
			      F::BarlowCondensedSemiBold, F::BarlowRegular, M::Pop, M::Fade);
		t.usesTag = true;
		t.cornerRadius = 14;
		all.push_back(t);
	}
	{
		auto t = make("corner-tag", Category::People, {withAlpha(kNavy, 245), kGold, kWhite, withAlpha(kWhite, 205)},
			      F::BarlowCondensedBold, F::BarlowRegular, M::Slide, M::Slide);
		t.usesTag = true;
		all.push_back(t);
	}
	{
		auto t = make("stacked-panels", Category::People, {withAlpha(kCream, 250), kBlue, kInk, kWhite},
			      F::BarlowCondensedBold, F::BarlowSemiBold, M::Rise, M::Slide);
		all.push_back(t);
	}
	{
		auto t = make("hairline", Category::People, {withAlpha(kInk, 0), kCream, kCream, withAlpha(kCream, 190)},
			      F::BarlowSemiBold, F::BarlowRegular, M::Fade, M::Fade);
		all.push_back(t);
	}

	// ---- Worship: scripture, songs, series ----
	{
		auto t = make("scripture-bar", Category::Worship, {withAlpha(kNavy, 242), kGold, kWhite, kGold},
			      F::BaskervilleItalic, F::BarlowCondensedSemiBold, M::Rise, M::Fade);
		t.usesLine2 = true; // line 1: the verse, line 2: the reference
		t.cornerRadius = 0;
		all.push_back(t);
	}
	{
		auto t = make("verse-card", Category::Worship, {withAlpha(kBurgundy, 245), kGold, kCream, withAlpha(kCream, 210)},
			      F::Baskerville, F::BarlowCondensedMedium, M::Fade, M::Fade);
		t.cornerRadius = 18;
		all.push_back(t);
	}
	{
		auto t = make("series-title", Category::Worship, {withAlpha(kInk, 0), kAmber, kWhite, withAlpha(kWhite, 220)},
			      F::BebasNeue, F::BarlowSemiBold, M::Wipe, M::Wipe);
		t.holdSeconds = 9.0;
		all.push_back(t);
	}
	{
		auto t = make("song-lyric", Category::Worship, {withAlpha(kInk, 0), kAmber, kWhite, withAlpha(kWhite, 235)},
			      F::BarlowSemiBold, F::BarlowSemiBold, M::Fade, M::Fade);
		t.inSeconds = 0.5;
		t.outSeconds = 0.35;
		all.push_back(t);
	}
	{
		auto t = make("big-quote", Category::Worship, {withAlpha(kInk, 0), kAmber, kWhite, withAlpha(kWhite, 210)},
			      F::Baskerville, F::BarlowCondensedSemiBold, M::Rise, M::Fade);
		all.push_back(t);
	}

	// ---- Announce: events, alerts, social ----
	{
		auto t = make("announcement", Category::Announce, {withAlpha(kTeal, 245), kAmber, kWhite, withAlpha(kWhite, 220)},
			      F::BarlowCondensedBold, F::BarlowRegular, M::Slide, M::Slide);
		t.usesTag = true;
		all.push_back(t);
	}
	{
		auto t = make("breaking", Category::Announce, {withAlpha(kWhite, 248), kRed, kInk, withAlpha(kInk, 180)},
			      F::BarlowCondensedBold, F::BarlowSemiBold, M::Slide, M::Slide);
		t.usesTag = true;
		all.push_back(t);
	}
	{
		auto t = make("news-bar", Category::Announce, {withAlpha(kSlate, 245), kBlue, kWhite, withAlpha(kWhite, 195)},
			      F::BarlowCondensedSemiBold, F::BarlowRegular, M::Slide, M::Slide);
		t.usesTag = true;
		all.push_back(t);
	}
	{
		auto t = make("handle-pill", Category::Announce, {withAlpha(kInk, 235), kGreen, kCream, withAlpha(kCream, 190)},
			      F::BarlowSemiBold, F::BarlowCondensedMedium, M::Pop, M::Fade);
		t.usesLine2 = false;
		t.usesTag = true;
		t.cornerRadius = 40;
		t.holdSeconds = 8.0;
		all.push_back(t);
	}
	{
		auto t = make("thank-you", Category::Announce, {withAlpha(kInk, 0), kAmber, kWhite, withAlpha(kWhite, 220)},
			      F::BebasNeue, F::BarlowRegular, M::Fade, M::Fade);
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
		all.push_back(t);
	}
	{
		auto t = make("headline-ticker", Category::Ticker, {withAlpha(kNavy, 245), kRed, kWhite, kWhite},
			      F::BarlowCondensedSemiBold, F::BarlowCondensedBold, M::Slide, M::Slide);
		t.usesLine2 = false;
		t.usesTag = true;
		t.tickerSpeed = 130.0;
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
	for (MotionKind m : {MotionKind::Slide, MotionKind::Rise, MotionKind::Fade, MotionKind::Pop, MotionKind::Wipe}) {
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
	}
	return "barlow";
}

bool faceFromName(const std::string &name, FontFace &out)
{
	for (FontFace f : {FontFace::BarlowCondensedMedium, FontFace::BarlowCondensedSemiBold,
			   FontFace::BarlowCondensedBold, FontFace::BarlowRegular, FontFace::BarlowSemiBold,
			   FontFace::BebasNeue, FontFace::Baskerville, FontFace::BaskervilleItalic}) {
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
