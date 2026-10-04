#include "core/library.hpp"

#include <algorithm>

namespace laycue::titles {

namespace {

constexpr size_t kMaxText = 600;

void clip(std::string &s)
{
	if (s.size() > kMaxText)
		s.resize(kMaxText);
}

} // namespace

std::string customId(const std::string &name)
{
	return "custom:" + name;
}

bool isCustomId(const std::string &id)
{
	return id.rfind("custom:", 0) == 0;
}

std::string customNameOf(const std::string &id)
{
	return isCustomId(id) ? id.substr(7) : std::string();
}

const Overrides *findCustom(const Library &lib, const std::string &name)
{
	for (const Overrides &o : lib.customs) {
		if (o.name == name)
			return &o;
	}
	return nullptr;
}

TitleTemplate resolveStyle(const Library &lib, const std::string &styleId, std::string *baseId)
{
	if (isCustomId(styleId)) {
		if (const Overrides *o = findCustom(lib, customNameOf(styleId))) {
			if (const TitleTemplate *base = findTemplate(o->baseId)) {
				if (baseId)
					*baseId = base->id;
				TitleTemplate t = resolve(*base, *o);
				t.id = base->id;
				return t;
			}
		}
	} else if (const TitleTemplate *t = findTemplate(styleId)) {
		if (baseId)
			*baseId = t->id;
		return *t;
	}
	const TitleTemplate &first = builtInTemplates().front();
	if (baseId)
		*baseId = first.id;
	return first;
}

std::string uniqueCustomName(const Library &lib, const std::string &wanted)
{
	std::string base = wanted.empty() ? "My template" : wanted;
	if (!findCustom(lib, base))
		return base;
	for (int n = 2;; ++n) {
		const std::string candidate = base + " " + std::to_string(n);
		if (!findCustom(lib, candidate))
			return candidate;
	}
}

bool saveCustom(Library &lib, const Overrides &overrides)
{
	if (overrides.name.empty())
		return false;
	for (Overrides &o : lib.customs) {
		if (o.name == overrides.name) {
			o = overrides;
			return true;
		}
	}
	if (lib.customs.size() >= kMaxCustoms)
		return false;
	lib.customs.push_back(overrides);
	return true;
}

bool removeCustom(Library &lib, const std::string &name)
{
	const auto it = std::find_if(lib.customs.begin(), lib.customs.end(),
				     [&](const Overrides &o) { return o.name == name; });
	if (it == lib.customs.end())
		return false;
	const std::string id = customId(name);
	const std::string base = it->baseId;
	lib.customs.erase(it);
	if (lib.selected == id)
		lib.selected = base;
	for (QueueItem &q : lib.queue) {
		if (q.styleId == id)
			q.styleId = base;
	}
	return true;
}

bool enqueue(Library &lib, const QueueItem &item)
{
	if (lib.queue.size() >= kMaxQueue)
		return false;
	lib.queue.push_back(item);
	return true;
}

bool removeFromQueue(Library &lib, size_t index)
{
	if (index >= lib.queue.size())
		return false;
	lib.queue.erase(lib.queue.begin() + static_cast<std::ptrdiff_t>(index));
	return true;
}

bool moveInQueue(Library &lib, size_t from, size_t to)
{
	if (from >= lib.queue.size() || to >= lib.queue.size())
		return false;
	if (from == to)
		return true;
	QueueItem item = lib.queue[from];
	lib.queue.erase(lib.queue.begin() + static_cast<std::ptrdiff_t>(from));
	lib.queue.insert(lib.queue.begin() + static_cast<std::ptrdiff_t>(to), item);
	return true;
}

bool popQueue(Library &lib, QueueItem &out)
{
	if (lib.queue.empty())
		return false;
	out = lib.queue.front();
	lib.queue.erase(lib.queue.begin());
	return true;
}

void sanitize(Library &lib)
{
	for (Overrides &o : lib.customs)
		laycue::titles::sanitize(o);
	// Custom templates need a name, and names are unique.
	std::vector<Overrides> kept;
	for (const Overrides &o : lib.customs) {
		if (o.name.empty())
			continue;
		const bool duplicate =
			std::any_of(kept.begin(), kept.end(), [&](const Overrides &k) { return k.name == o.name; });
		if (!duplicate && kept.size() < kMaxCustoms)
			kept.push_back(o);
	}
	lib.customs = std::move(kept);

	const auto valid = [&](const std::string &id) {
		return isCustomId(id) ? findCustom(lib, customNameOf(id)) != nullptr : findTemplate(id) != nullptr;
	};
	if (!valid(lib.selected))
		lib.selected = builtInTemplates().front().id;
	for (QueueItem &q : lib.queue) {
		if (!valid(q.styleId))
			q.styleId = builtInTemplates().front().id;
		clip(q.content.line1);
		clip(q.content.line2);
		clip(q.content.tag);
	}
	if (lib.queue.size() > kMaxQueue)
		lib.queue.resize(kMaxQueue);
	clip(lib.draft.line1);
	clip(lib.draft.line2);
	clip(lib.draft.tag);
	if (lib.holdOverride < -1 || lib.holdOverride > 600)
		lib.holdOverride = -1;
}

} // namespace laycue::titles
