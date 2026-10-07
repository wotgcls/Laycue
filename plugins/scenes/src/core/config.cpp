#include "core/config.hpp"

#include "core/layouts.hpp"

#include <algorithm>

namespace laycue {

void sanitize(Config &config)
{
	config.version = kConfigVersion;
	config.settings.insideFadeMs = std::clamp(config.settings.insideFadeMs, 0, 3000);

	// Tags: one per source, none empty.
	std::vector<TagAssignment> tags;
	for (const TagAssignment &t : config.tags) {
		if (t.tag.empty() || (t.sourceUuid.empty() && t.sourceName.empty()))
			continue;
		const bool duplicate = std::any_of(tags.begin(), tags.end(), [&](const TagAssignment &kept) {
			return !t.sourceUuid.empty() && kept.sourceUuid == t.sourceUuid;
		});
		if (!duplicate)
			tags.push_back(t);
	}
	config.tags = std::move(tags);

	// Rules: one with no tags simply does nothing (it may be a rule being set up), so it is kept.

	// Pushbacks: need an id and a name; the active feed must be one of the feeds.
	std::vector<Pushback> pushbacks;
	for (Pushback &p : config.pushbacks) {
		if (p.id.empty() || p.name.empty())
			continue;
		const bool known = std::any_of(p.feeds.begin(), p.feeds.end(),
					       [&](const Feed &f) { return f.uuid == p.activeFeed; });
		if (!known)
			p.activeFeed = p.feeds.empty() ? std::string() : p.feeds.front().uuid;
		pushbacks.push_back(std::move(p));
	}
	config.pushbacks = std::move(pushbacks);

	// Layouts: need an id, a name and a template Laycue knows; slots match the template.
	std::vector<Layout> layouts;
	for (Layout &l : config.layouts) {
		const LayoutTemplate *shape = findTemplate(l.templateId);
		if (l.id.empty() || l.name.empty() || !shape)
			continue;
		l.sources.resize(shape->shapes.size());
		layouts.push_back(std::move(l));
	}
	config.layouts = std::move(layouts);
}

std::string tagOf(const Config &config, const std::string &sourceUuid)
{
	for (const TagAssignment &t : config.tags) {
		if (t.sourceUuid == sourceUuid)
			return t.tag;
	}
	return std::string();
}

void setTag(Config &config, const std::string &sourceUuid, const std::string &sourceName, const std::string &tagName)
{
	for (auto it = config.tags.begin(); it != config.tags.end(); ++it) {
		if (it->sourceUuid != sourceUuid)
			continue;
		if (tagName.empty())
			config.tags.erase(it);
		else {
			it->tag = tagName;
			it->sourceName = sourceName;
		}
		return;
	}
	if (!tagName.empty())
		config.tags.push_back({sourceUuid, sourceName, tagName});
}

bool isPinned(const Config &config, const std::string &sourceUuid)
{
	return std::find(config.pinned.begin(), config.pinned.end(), sourceUuid) != config.pinned.end();
}

void setPinned(Config &config, const std::string &sourceUuid, bool pinned)
{
	const auto it = std::find(config.pinned.begin(), config.pinned.end(), sourceUuid);
	if (pinned && it == config.pinned.end() && !sourceUuid.empty())
		config.pinned.push_back(sourceUuid);
	else if (!pinned && it != config.pinned.end())
		config.pinned.erase(it);
}

bool tipDismissed(const Config &config, const std::string &tipId)
{
	return std::find(config.dismissedTips.begin(), config.dismissedTips.end(), tipId) != config.dismissedTips.end();
}

void dismissTip(Config &config, const std::string &tipId)
{
	if (!tipDismissed(config, tipId))
		config.dismissedTips.push_back(tipId);
}

std::string newPushbackId(const Config &config)
{
	for (int n = 1;; ++n) {
		const std::string id = "pb" + std::to_string(n);
		const bool taken = std::any_of(config.pushbacks.begin(), config.pushbacks.end(),
					       [&](const Pushback &p) { return p.id == id; });
		if (!taken)
			return id;
	}
}

std::string newLayoutId(const Config &config)
{
	for (int n = 1;; ++n) {
		const std::string id = "ly" + std::to_string(n);
		const bool taken = std::any_of(config.layouts.begin(), config.layouts.end(),
					       [&](const Layout &l) { return l.id == id; });
		if (!taken)
			return id;
	}
}

} // namespace laycue
