// The title templates as plain data, and the "custom template" that changes one. Pure logic: no
// libobs, no Qt. The renderer (render/) turns a template plus text into pixels.
#pragma once

#include <cstdint>
#include <map>
#include <string>
#include <vector>

namespace laycue::titles {

struct Rgba {
	uint8_t r = 0;
	uint8_t g = 0;
	uint8_t b = 0;
	uint8_t a = 255;
	bool operator==(const Rgba &o) const { return r == o.r && g == o.g && b == o.b && a == o.a; }
};

// "#RRGGBB" or "#RRGGBBAA". Returns false and leaves `out` alone if the text isn't a colour.
bool parseColour(const std::string &text, Rgba &out);
std::string colourToString(const Rgba &colour);

// The fonts bundled with the plugin (all SIL Open Font License). Bundling is deliberate: a title
// looks the same on every computer, which browser-based lower thirds cannot promise.
enum class FontFace {
	BarlowCondensedMedium,
	BarlowCondensedSemiBold,
	BarlowCondensedBold,
	BarlowRegular,
	BarlowSemiBold,
	BebasNeue,
	Baskerville,
	BaskervilleItalic,
};

// What a template is for: how the gallery groups them.
enum class Category { People, Worship, Announce, Ticker };

// How a title arrives and leaves. Every template also has its own signature choreography inside
// this (stagger, wipe, accent growing); this is the movement of the whole group.
enum class MotionKind { Slide, Rise, Fade, Pop, Wipe };

struct Palette {
	Rgba panel;   // the main surface behind the text (alpha 0 = no panel)
	Rgba accent;  // the one accent colour: a rule, a chip, a block
	Rgba text;    // main line
	Rgba subtext; // second line and tag text
};

// One of the built-in designs. `id` is saved in scenes and never changes.
struct TitleTemplate {
	std::string id;
	Category category = Category::People;
	Palette palette;
	FontFace display = FontFace::BarlowCondensedSemiBold; // the main line
	FontFace body = FontFace::BarlowRegular;              // second line and tag
	MotionKind motionIn = MotionKind::Slide;
	MotionKind motionOut = MotionKind::Slide;
	double inSeconds = 0.8;
	double outSeconds = 0.5;  // exits are shorter than entrances: they get out of the way
	double holdSeconds = 0.0; // 0 = stays until hidden
	bool usesLine2 = true;
	bool usesTag = false;
	bool longText = false;     // line 1 is a sentence or more (a verse, a quote, a ticker): edited in a taller box
	bool anchorTop = false;    // drawn in the top of the picture instead of the bottom
	bool fullHeight = false;   // uses the whole picture, not just the lower part
	double cornerRadius = 0.0; // in 1920-wide design pixels
	double tickerSpeed = 0.0;  // > 0 for tickers: pixels per second at 1920 wide
};

// Locale keys are built from the id: "Laycue.Titles.T.<id>" is the name, ".S1" ".S2" ".S3" are the
// sample line 1, line 2 and tag the gallery shows, and ".F1" ".F2" ".F3" label the fields.
std::string nameKey(const TitleTemplate &t);
std::string sampleKey(const TitleTemplate &t, int field);
std::string fieldKey(const TitleTemplate &t, int field);
const char *categoryKey(Category category);

// All templates in gallery order.
const std::vector<TitleTemplate> &builtInTemplates();
const TitleTemplate *findTemplate(const std::string &id);

// A custom template is a built-in one with some things changed. Only what the operator changed is
// stored, so improvements to the built-in design still reach custom templates.
struct Overrides {
	std::string baseId;
	std::string name; // the operator's name for it
	bool hasPanel = false;
	bool hasAccent = false;
	bool hasText = false;
	bool hasSubtext = false;
	Palette palette; // only the parts whose has* flag is set count
	bool hasDisplay = false;
	FontFace display = FontFace::BarlowCondensedSemiBold;
	bool hasBody = false;
	FontFace body = FontFace::BarlowRegular;
	bool hasMotionIn = false;
	MotionKind motionIn = MotionKind::Slide;
	bool hasMotionOut = false;
	MotionKind motionOut = MotionKind::Slide;
	bool hasCornerRadius = false;
	double cornerRadius = 0.0;
	bool hasHold = false;
	double holdSeconds = 0.0;
	bool hasSpeed = false;
	double tickerSpeed = 0.0;
};

// The base template with the overrides applied. Null `base` gives the first built-in template.
TitleTemplate resolve(const TitleTemplate &base, const Overrides &overrides);

// Overrides as text (for saving in an OBS scene or a file) and back. Unknown or broken entries are
// ignored; the result is always usable. Never throws.
std::map<std::string, std::string> toMap(const Overrides &overrides);
Overrides fromMap(const std::map<std::string, std::string> &values);

// Keeps numbers in sensible ranges.
void sanitize(Overrides &overrides);

} // namespace laycue::titles
