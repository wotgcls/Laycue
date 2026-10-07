// Suggests what an overlay source is, from its name and where it sits on the canvas.
// Pure logic: no libobs, no Qt. The operator always has the last word: Laycue only suggests.
#pragma once

#include "core/geometry.hpp"
#include "core/source-kinds.hpp"

#include <optional>
#include <string>
#include <vector>

namespace laycue {

// Built-in tags. A tag is just a string, so operators can also type their own.
namespace tag {
inline constexpr const char *scripture = "scripture";
inline constexpr const char *lyrics = "lyrics";
inline constexpr const char *lowerThird = "lowerthird";
inline constexpr const char *logo = "logo";
inline constexpr const char *clock = "clock";     // countdowns and timers
inline constexpr const char *overlay = "overlay"; // anything else that sits on top of the picture
} // namespace tag

const std::vector<std::string> &builtInTags();

struct TagSuggestion {
	std::string tag;
	bool fromName = false; // otherwise from what it is or where it sits
	// Why Laycue thinks so, as a short id the panel turns into words: "name", "laycue", "key", "green", "seethrough",
	// "text", "browser", "place".
	std::string why;
};

// Everything Laycue can learn about one source. Anything not measured is left at its "unknown" value.
struct SourceFacts {
	std::string name;
	std::string typeId; // OBS's id for the kind of source, e.g. "image_source" or "laycue_title"
	SourceKind kind = SourceKind::Other;
	RectF spot; // where it shows on the canvas (empty if unknown)
	double canvasWidth = 0;
	double canvasHeight = 0;
	double seeThrough = -1.0; // share of the picture that is see-through (0..1), after its filters; -1 not measured
	double green = -1.0;      // share of the visible picture that is strongly green (0..1); -1 not measured
	bool keyFilter = false;   // a Chroma Key or Color Key filter is on it
};

// Is this source an overlay, and which label fits? Names come first (the operator named it), then what it is
// (a Laycue title, a see-through picture, a green screen, a text source), then where it sits.
// A camera, a capture or a scene is never suggested unless its name says so.
std::optional<TagSuggestion> classifySource(const SourceFacts &facts);

// `spot` is where the source shows on the canvas (empty if unknown).
std::optional<TagSuggestion> suggestTag(const std::string &sourceName, const RectF &spot, double canvasWidth,
					double canvasHeight);

} // namespace laycue
