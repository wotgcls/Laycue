#include "core/autotag.hpp"

#include <algorithm>
#include <cctype>

namespace laycue {

namespace {

std::string lower(std::string text)
{
	std::transform(text.begin(), text.end(), text.begin(), [](unsigned char c) { return std::tolower(c); });
	return text;
}

bool containsAny(const std::string &text, std::initializer_list<const char *> words)
{
	for (const char *word : words) {
		if (text.find(word) != std::string::npos)
			return true;
	}
	return false;
}

// Short words only count as whole words, so "bell" matches "Bell ringer" but not "Bellevue Camera".
bool containsWord(const std::string &text, std::initializer_list<const char *> words)
{
	for (const char *word : words) {
		const std::string w(word);
		size_t at = text.find(w);
		while (at != std::string::npos) {
			const bool startOk = at == 0 || !std::isalnum(static_cast<unsigned char>(text[at - 1]));
			const size_t end = at + w.size();
			const bool endOk = end >= text.size() || !std::isalnum(static_cast<unsigned char>(text[end]));
			if (startOk && endOk)
				return true;
			at = text.find(w, at + 1);
		}
	}
	return false;
}

} // namespace

const std::vector<std::string> &builtInTags()
{
	static const std::vector<std::string> tags{tag::scripture, tag::lyrics, tag::lowerThird,
						   tag::logo,      tag::clock,  tag::overlay};
	return tags;
}

namespace {

// Where on the screen it sits tells which kind of overlay it probably is.
TagSuggestion byPlace(const SourceFacts &f, const char *why)
{
	if (!f.spot.empty() && f.canvasWidth > 0 && f.canvasHeight > 0) {
		const double share = f.spot.area() / (f.canvasWidth * f.canvasHeight);
		const bool bottomBand = f.spot.y > f.canvasHeight * 0.5 && f.spot.w > f.spot.h * 2.2 && share > 0.02;
		const bool inCorner = (f.spot.x < f.canvasWidth * 0.3 || f.spot.right() > f.canvasWidth * 0.7) &&
				      (f.spot.y < f.canvasHeight * 0.3 || f.spot.bottom() > f.canvasHeight * 0.7);
		if (bottomBand)
			return TagSuggestion{tag::lowerThird, false, why};
		if (inCorner && share < 0.08)
			return TagSuggestion{tag::logo, false, why};
	}
	return TagSuggestion{tag::overlay, false, why};
}

bool startsWith(const std::string &text, const char *prefix)
{
	return text.rfind(prefix, 0) == 0;
}

bool seesTheWorld(SourceKind kind) // a picture of the world or of a scene, never an overlay by itself
{
	switch (kind) {
	case SourceKind::Camera:
	case SourceKind::ScreenCapture:
	case SourceKind::WindowCapture:
	case SourceKind::GameCapture:
	case SourceKind::Ndi:
	case SourceKind::Scene:
	case SourceKind::Group:
	case SourceKind::Audio:
		return true;
	default:
		return false;
	}
}

} // namespace

std::optional<TagSuggestion> classifySource(const SourceFacts &f)
{
	const std::string name = lower(f.name);

	// A camera, a capture, a scene fills the picture; it does not sit on top of it. Even if its name says "logo".
	if (seesTheWorld(f.kind))
		return std::nullopt;

	// Laycue's own sources are overlays by design.
	if (startsWith(f.typeId, "laycue_title"))
		return TagSuggestion{tag::lowerThird, false, "laycue"};
	if (startsWith(f.typeId, "laycue_clock"))
		return TagSuggestion{tag::clock, false, "laycue"};

	// Names first: the operator named it, so it is the strongest hint.
	if (containsAny(name, {"scripture", "verse", "bible", "passage", "reading"}))
		return TagSuggestion{tag::scripture, true, "name"};
	if (containsAny(name, {"lyric", "song", "hymn", "chorus", "worship words"}))
		return TagSuggestion{tag::lyrics, true, "name"};
	if (containsAny(name, {"lower third", "lowerthird", "lower-third", "lower_third", "name tag", "nametag",
			       "caption", "subtitle", "speaker name", "chyron", "ticker", "crawl", "name bar"}))
		return TagSuggestion{tag::lowerThird, true, "name"};
	if (containsAny(name, {"logo", "watermark", "brand", "badge", "bumper"}) || containsWord(name, {"bug"}))
		return TagSuggestion{tag::logo, true, "name"};
	if (containsAny(name, {"countdown", "clock", "timer", "starting soon"}))
		return TagSuggestion{tag::clock, true, "name"};
	if (containsAny(name, {"overlay", "banner", "alert", "giving", "donate", "offering", "social", "follow",
			       "subscribe", "announcement"}) ||
	    containsWord(name, {"qr", "bell", "frame"}))
		return TagSuggestion{tag::overlay, true, "name"};

	// A colour that fills the screen is a background (a green one is the backdrop a camera is keyed against), not an overlay.
	if (f.kind == SourceKind::Color) {
		const double colourShare = (f.spot.empty() || f.canvasWidth <= 0 || f.canvasHeight <= 0)
						   ? 1.0
						   : f.spot.area() / (f.canvasWidth * f.canvasHeight);
		if (colourShare > 0.3)
			return std::nullopt;
	}

	// What the picture looks like once its filters have run.
	if (f.keyFilter)
		return byPlace(f, "key"); // a Chroma Key or Color Key filter: it is made to be laid over something
	if (f.green >= 0.25)
		return byPlace(f, "green"); // a green screen
	if (f.seeThrough >= 0.05)
		return byPlace(
			f, "seethrough"); // transparent parts: a PNG without a background, a see-through browser page

	if (f.kind == SourceKind::Text)
		return byPlace(f, "text");

	// What is left depends on size: small and opaque things on top of the picture are logos and bars; big ones are
	// the picture itself (slides, backgrounds).
	if (f.spot.empty() || f.canvasWidth <= 0 || f.canvasHeight <= 0)
		return std::nullopt;
	const double share = f.spot.area() / (f.canvasWidth * f.canvasHeight);
	if (share > 0.3)
		return std::nullopt;
	if (f.kind == SourceKind::Browser && share <= 0.3)
		return byPlace(f, "browser");
	const TagSuggestion place = byPlace(f, "place");
	if (place.tag == tag::overlay)
		return std::nullopt; // small and opaque but not at an edge: not sure enough, leave it to the operator
	return place;
}

std::optional<TagSuggestion> suggestTag(const std::string &sourceName, const RectF &spot, double canvasWidth,
					double canvasHeight)
{
	SourceFacts facts;
	facts.name = sourceName;
	facts.spot = spot;
	facts.canvasWidth = canvasWidth;
	facts.canvasHeight = canvasHeight;
	return classifySource(facts);
}

} // namespace laycue
