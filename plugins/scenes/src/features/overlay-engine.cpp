#include "features/overlay-engine.hpp"

#include "core/config.hpp"
#include "core/exclusivity.hpp"
#include "features/app.hpp"
#include "obs/overlay-items.hpp"

#include <algorithm>
#include <map>

namespace laycue {

std::optional<Replacement> OverlayEngine::handleItemVisible(const std::string &sceneUuid, int64_t itemId, bool visible)
{
	if (busy_ || !visible)
		return std::nullopt;

	const Config &config = app_.config();
	if (config.rules.empty() || config.tags.empty())
		return std::nullopt;

	const std::vector<OverlayItem> items = readAllItems();
	const auto shownIt = std::find_if(items.begin(), items.end(), [&](const OverlayItem &item) {
		return item.sceneUuid == sceneUuid && item.itemId == itemId;
	});
	if (shownIt == items.end())
		return std::nullopt;

	const std::string shownTag = tagOf(config, shownIt->sourceUuid);
	if (shownTag.empty())
		return std::nullopt;

	// Only rules that involve this overlay matter; their tags are the only rivals.
	std::vector<std::string> rivalTags;
	for (const Rule &rule : config.rules) {
		if (rule.paused || std::find(rule.tags.begin(), rule.tags.end(), shownTag) == rule.tags.end())
			continue;
		rivalTags.insert(rivalTags.end(), rule.tags.begin(), rule.tags.end());
	}
	if (rivalTags.empty())
		return std::nullopt;

	TaggedItem shown;
	shown.ref = {shownIt->sceneUuid, shownIt->itemId};
	shown.sourceUuid = shownIt->sourceUuid;
	shown.tag = shownTag;
	shown.spot = measureSpot(shownIt->sceneUuid, shownIt->itemId);

	std::vector<TaggedItem> others;
	std::map<std::pair<std::string, int64_t>, std::string> names; // for the message
	for (const OverlayItem &item : items) {
		if (!item.visible || (item.sceneUuid == sceneUuid && item.itemId == itemId))
			continue;
		const std::string tag = tagOf(config, item.sourceUuid);
		if (tag.empty() || std::find(rivalTags.begin(), rivalTags.end(), tag) == rivalTags.end())
			continue;
		TaggedItem other;
		other.ref = {item.sceneUuid, item.itemId};
		other.sourceUuid = item.sourceUuid;
		other.tag = tag;
		other.spot = measureSpot(item.sceneUuid, item.itemId);
		others.push_back(std::move(other));
		names[{item.sceneUuid, item.itemId}] = item.sourceName;
	}

	const std::vector<ItemRef> toHide = itemsToHide(shown, config.rules, others);
	if (toHide.empty())
		return std::nullopt;

	Replacement result;
	result.shownName = shownIt->sourceName;
	busy_ = true;
	try {
		for (const ItemRef &ref : toHide) {
			if (setItemVisible(ref.sceneUuid, ref.itemId, false)) {
				const std::string &name = names[{ref.sceneUuid, ref.itemId}];
				if (std::find(result.hiddenNames.begin(), result.hiddenNames.end(), name) ==
				    result.hiddenNames.end())
					result.hiddenNames.push_back(name);
			}
		}
	} catch (...) {
		busy_ = false;
		throw;
	}
	busy_ = false;

	result.sameSpot = std::none_of(config.rules.begin(), config.rules.end(), [&](const Rule &r) {
		return !r.paused && r.strict && std::find(r.tags.begin(), r.tags.end(), shownTag) != r.tags.end();
	});
	if (result.hiddenNames.empty())
		return std::nullopt;
	return result;
}

} // namespace laycue
