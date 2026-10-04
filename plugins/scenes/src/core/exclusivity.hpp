// Overlay rules: when one tagged overlay is shown, which other overlays should Laycue hide?
// Pure logic: no libobs, no Qt. See CLAUDE.md, "Exclusivity".
#pragma once

#include "core/geometry.hpp"

#include <cstdint>
#include <string>
#include <vector>

namespace laycue {

enum class Scope {
	SameScene,    // only overlays in the same scene as the one just shown
	AllScenes,    // overlays in every scene
	ChosenScenes, // overlays in the listed scenes (and the change itself must be in one of them)
};

// One rule: overlays with these tags replace each other.
struct Rule {
	std::string name;
	std::vector<std::string> tags;
	Scope scope = Scope::SameScene;
	std::vector<std::string> scenes; // scene UUIDs, for ChosenScenes
	bool strict = false;             // true: only one at a time, wherever they are. false: only if they overlap.
	bool paused = false;
};

// One item in a scene that carries a tag.
struct ItemRef {
	std::string sceneUuid;
	int64_t itemId = 0;

	bool operator==(const ItemRef &other) const { return sceneUuid == other.sceneUuid && itemId == other.itemId; }
};

struct TaggedItem {
	ItemRef ref;
	std::string sourceUuid;
	std::string tag;
	RectF spot; // where it shows on the canvas
};

// `shown` just became visible. `visibleOthers` are the other tagged items that are visible now.
// Returns the items to hide. Never returns an item outside a rule's scope, and paused rules
// return nothing.
std::vector<ItemRef> itemsToHide(const TaggedItem &shown, const std::vector<Rule> &rules,
				 const std::vector<TaggedItem> &visibleOthers);

// The starting rule offered to a new operator: text overlays replace each other where they overlap.
std::vector<Rule> defaultRules();

} // namespace laycue
