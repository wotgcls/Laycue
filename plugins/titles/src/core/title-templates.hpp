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
	OutfitRegular,
	OutfitSemiBold,
	OutfitBold,
	OutfitExtraBold,
};

// What a template is for: how the gallery groups them.
enum class Category { People, Worship, Announce, Ticker, Live, Social, News, Notes };

// How a title arrives and leaves. Every template also has its own signature choreography inside
// this (stagger, wipe, accent growing); this is the movement of the whole group.
enum class MotionKind { Slide, Rise, Fade, Pop, Wipe };

struct Palette {
	Rgba panel;   // the main surface behind the text (alpha 0 = no panel)
	Rgba accent;  // the one accent colour: a rule, a chip, a block
	Rgba text;    // main line
	Rgba subtext; // second line and tag text
	// Extra colours, used only by templates that have more shapes (flagged usesShape2 / usesShape3 / usesInk).
	Rgba shape2{255, 255, 255, 255}; // a second shape: a tab under the bar, the note tab
	Rgba shape3{107, 31, 143, 255};  // a third shape: the logo block
	Rgba ink{17, 17, 16, 255};       // text that sits on the tabs and blocks
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
	bool longText = false;      // line 1 is a sentence or more (a verse, a quote, a ticker): edited in a taller box
	bool anchorTop = false;     // drawn in the top of the picture instead of the bottom
	bool fullHeight = false;    // uses the whole picture, not just the lower part
	double cornerRadius = 0.0;  // in 1920-wide design pixels
	double tickerSpeed = 0.0;   // > 0 for tickers: pixels per second at 1920 wide
	double pulseSeconds = 0.0;  // > 0 for titles with a pulsing light (the LIVE dot): seconds per pulse
	bool usesNote = false;      // an extra line under the title (a sort code, a web address)
	bool usesLine1 = true;      // false for titles with no words (a logo animation)
	bool usesLogo = false;      // the operator can choose a logo picture
	bool usesLogoParts = false; // ...and mark which part is the mark and which the wordmark
	int maxRows = 0;            // > 0: a list of label and value rows (bank accounts), up to this many
	bool usesShape2 = false;    // the colour pickers offer the extra colours below
	bool usesShape3 = false;
	bool usesInk = false;
};

// Titles that need a new picture every frame while on air: tickers scroll, live lights pulse. The renderer
// is given a steadily growing "position" for both; for a pulse it is 100 px per second.
inline bool isContinuous(const TitleTemplate &t)
{
	return t.tickerSpeed > 0.0 || t.pulseSeconds > 0.0;
}
// Where a title sits across the picture, and how big it is. Only titles that do not span the whole width
// (not tickers, not the full-screen Thank You) can be placed.
enum class Align { Left = 0, Center = 1, Right = 2 };
constexpr int kMinSizePercent = 50;
constexpr int kMaxSizePercent = 150;
inline bool supportsPlacement(const TitleTemplate &t)
{
	return t.tickerSpeed <= 0.0 && !t.fullHeight && t.id != "thank-you";
}

// Shape width choices for the name lower thirds: the template's own width, a shape that hugs the text, or a
// width the operator sets (the text then shrinks to fit). Only templates whose shape is sized by the name.
constexpr int kMinFitWidth = 300;
constexpr int kMaxFitWidth = 1500;
constexpr int kDefaultFitWidth = 700;
inline bool supportsFit(const TitleTemplate &t)
{
	static const char *const ids[] = {"clean-bar",      "accent-edge", "split-blocks",
					  "pop-card",       "corner-tag",  "stacked-panels",
					  "guest-minister", "modern-pill", "modern-card"};
	for (const char *id : ids)
		if (t.id == id)
			return true;
	return false;
}

inline double continuousSpeed(const TitleTemplate &t)
{
	return t.tickerSpeed > 0.0 ? t.tickerSpeed : (t.pulseSeconds > 0.0 ? 100.0 : 0.0);
}

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
	bool hasShape2 = false;
	bool hasShape3 = false;
	bool hasInk = false;
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
