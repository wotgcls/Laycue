// Suggests what an overlay source is, from its name and where it sits on the canvas.
// Pure logic: no libobs, no Qt. The operator always has the last word: Laycue only suggests.
#pragma once

#include "core/geometry.hpp"

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
} // namespace tag

const std::vector<std::string> &builtInTags();

struct TagSuggestion {
	std::string tag;
	bool fromName = false; // otherwise from position
};

// `spot` is where the source shows on the canvas (empty if unknown).
std::optional<TagSuggestion> suggestTag(const std::string &sourceName, const RectF &spot, double canvasWidth,
					double canvasHeight);

} // namespace laycue
