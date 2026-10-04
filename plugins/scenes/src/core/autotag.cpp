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

} // namespace

const std::vector<std::string> &builtInTags()
{
	static const std::vector<std::string> tags{tag::scripture, tag::lyrics, tag::lowerThird, tag::logo};
	return tags;
}

std::optional<TagSuggestion> suggestTag(const std::string &sourceName, const RectF &spot, double canvasWidth,
					double canvasHeight)
{
	const std::string name = lower(sourceName);

	// Names first: the operator named it, so it's the strongest hint.
	if (containsAny(name, {"scripture", "verse", "bible", "passage"}))
		return TagSuggestion{tag::scripture, true};
	if (containsAny(name, {"lyric", "song", "hymn", "chorus"}))
		return TagSuggestion{tag::lyrics, true};
	if (containsAny(name, {"lower third", "lowerthird", "lower-third", "lower_third", "name tag", "nametag",
			       "caption", "speaker name", "chyron"}))
		return TagSuggestion{tag::lowerThird, true};
	if (containsAny(name, {"logo", "watermark", "bug", "brand"}))
		return TagSuggestion{tag::logo, true};

	// Then position, for sources that have a known spot on the canvas.
	if (spot.empty() || canvasWidth <= 0 || canvasHeight <= 0)
		return std::nullopt;
	const double share = spot.area() / (canvasWidth * canvasHeight);
	if (share > 0.2)
		return std::nullopt; // big: a camera, slides, a background
	const bool inCorner = (spot.x < canvasWidth * 0.25 || spot.right() > canvasWidth * 0.75) &&
			      (spot.y < canvasHeight * 0.25 || spot.bottom() > canvasHeight * 0.75);
	const bool bottomBand = spot.y > canvasHeight * 0.55 && spot.w > spot.h * 2.5;
	if (bottomBand && share > 0.03)
		return TagSuggestion{tag::lowerThird, false};
	if (inCorner && share < 0.05)
		return TagSuggestion{tag::logo, false};
	return std::nullopt;
}

} // namespace laycue
