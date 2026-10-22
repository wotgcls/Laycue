#include "core/exclusivity.hpp"

#include "core/autotag.hpp"

#include <algorithm>

namespace laycue {

namespace {

bool hasTag(const Rule &rule, const std::string &tagName)
{
	return std::find(rule.tags.begin(), rule.tags.end(), tagName) != rule.tags.end();
}

bool sceneListed(const Rule &rule, const std::string &sceneUuid)
{
	return std::find(rule.scenes.begin(), rule.scenes.end(), sceneUuid) != rule.scenes.end();
}

// Does the rule reach an item in `otherScene`, when the change happened in `changeScene`?
bool inScope(const Rule &rule, const std::string &changeScene, const std::string &otherScene)
{
	switch (rule.scope) {
	case Scope::SameScene:
		return changeScene == otherScene;
	case Scope::AllScenes:
		return true;
	case Scope::ChosenScenes:
		return sceneListed(rule, changeScene) && sceneListed(rule, otherScene);
	}
	return false;
}

} // namespace

std::vector<ItemRef> itemsToHide(const TaggedItem &shown, const std::vector<Rule> &rules,
				 const std::vector<TaggedItem> &visibleOthers)
{
	std::vector<ItemRef> result;
	for (const Rule &rule : rules) {
		if (rule.paused || !hasTag(rule, shown.tag))
			continue;
		if (rule.scope == Scope::ChosenScenes && !sceneListed(rule, shown.ref.sceneUuid))
			continue;

		for (const TaggedItem &other : visibleOthers) {
			if (other.ref == shown.ref || !hasTag(rule, other.tag))
				continue;
			// The same source shown again elsewhere is not a rival, it is the same overlay.
			if (!shown.sourceUuid.empty() && other.sourceUuid == shown.sourceUuid)
				continue;
			if (!inScope(rule, shown.ref.sceneUuid, other.ref.sceneUuid))
				continue;
			if (!rule.strict && !overlapsMeaningfully(shown.spot, other.spot))
				continue;
			if (std::find(result.begin(), result.end(), other.ref) == result.end())
				result.push_back(other.ref);
		}
	}
	return result;
}

std::vector<Rule> defaultRules()
{
	Rule text;
	text.name = "Text overlays";
	text.tags = {tag::scripture, tag::lyrics, tag::lowerThird};
	text.scope = Scope::SameScene;
	text.strict = false;
	return {text};
}

} // namespace laycue
