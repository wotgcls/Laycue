#include "core/library.hpp"

#include <algorithm>
#include <cctype>

namespace laycue::titles {

TitleContent contentForPoint(const TitleContent &draft, const std::vector<std::string> &points, size_t index)
{
	TitleContent out = draft;
	if (index >= points.size())
		return out;
	out.rows.clear();
	out.rows.push_back({std::to_string(index + 1), points[index]});
	return out;
}

namespace {

constexpr size_t kMaxText = 600;

void clip(std::string &s)
{
	if (s.size() > kMaxText)
		s.resize(kMaxText);
}

void clipContent(TitleContent &c)
{
	clip(c.line1);
	clip(c.line2);
	clip(c.tag);
	clip(c.note);
	if (c.logo.size() > 1024)
		c.logo.clear();
	if (c.logoCut != 0)
		c.logoCut = std::clamp(c.logoCut, kMinLogoCut, kMaxLogoCut);
	if (c.rows.size() > kMaxRows)
		c.rows.resize(kMaxRows);
	for (Row &r : c.rows) {
		clip(r.label);
		clip(r.value);
	}
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

Placement placementFor(const Library &lib, const std::string &styleId)
{
	const auto found = lib.placements.find(styleId);
	return found == lib.placements.end() ? Placement() : found->second;
}

void setPlacement(Library &lib, const std::string &styleId, Placement placement)
{
	placement.align = std::clamp(placement.align, 0, 2);
	placement.percent = std::clamp(placement.percent, kMinSizePercent, kMaxSizePercent);
	placement.fit = std::clamp(placement.fit, 0, 2);
	placement.width = std::clamp(placement.width, kMinFitWidth, kMaxFitWidth);
	if (placement == Placement())
		lib.placements.erase(styleId);
	else
		lib.placements[styleId] = placement;
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
	// The marker follows the title it pointed at; if that title was removed, it points to the one before.
	if (lib.queueCurrent >= 0 && static_cast<size_t>(lib.queueCurrent) >= index)
		--lib.queueCurrent;
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
	// Keep the "played last" marker on the same title.
	if (lib.queueCurrent >= 0) {
		const size_t cur = static_cast<size_t>(lib.queueCurrent);
		if (cur == from)
			lib.queueCurrent = static_cast<int>(to);
		else if (from < cur && to >= cur)
			--lib.queueCurrent;
		else if (from > cur && to <= cur)
			++lib.queueCurrent;
	}
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

int nextQueueIndex(const Library &lib)
{
	const int next = lib.queueCurrent + 1;
	return next >= 0 && static_cast<size_t>(next) < lib.queue.size() ? next : -1;
}

int previousQueueIndex(const Library &lib)
{
	const int previous = lib.queueCurrent - 1;
	return previous >= 0 && static_cast<size_t>(previous) < lib.queue.size() ? previous : -1;
}

bool replaceInQueue(Library &lib, size_t index, const QueueItem &item)
{
	if (index >= lib.queue.size())
		return false;
	lib.queue[index] = item;
	return true;
}

bool saveService(Library &lib, const std::string &name)
{
	std::string trimmed = name;
	const auto space = [](unsigned char c) {
		return std::isspace(c) != 0;
	};
	while (!trimmed.empty() && space(trimmed.front()))
		trimmed.erase(trimmed.begin());
	while (!trimmed.empty() && space(trimmed.back()))
		trimmed.pop_back();
	if (trimmed.empty() || trimmed.size() > 60)
		return false;
	if (lib.services.find(trimmed) == lib.services.end() && lib.services.size() >= kMaxServices)
		return false;
	lib.services[trimmed] = lib.queue;
	lib.serviceName = trimmed;
	return true;
}

bool loadService(Library &lib, const std::string &name)
{
	const auto it = lib.services.find(name);
	if (it == lib.services.end())
		return false;
	lib.queue = it->second;
	lib.queueCurrent = -1;
	lib.serviceName = name;
	return true;
}

bool deleteService(Library &lib, const std::string &name)
{
	if (lib.services.erase(name) == 0)
		return false;
	if (lib.serviceName == name)
		lib.serviceName.clear();
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
	for (auto it = lib.placements.begin(); it != lib.placements.end();) {
		it->second.align = std::clamp(it->second.align, 0, 2);
		it->second.percent = std::clamp(it->second.percent, kMinSizePercent, kMaxSizePercent);
		it->second.fit = std::clamp(it->second.fit, 0, 2);
		it->second.width = std::clamp(it->second.width, kMinFitWidth, kMaxFitWidth);
		if (!valid(it->first) || it->second == Placement())
			it = lib.placements.erase(it);
		else
			++it;
	}
	for (QueueItem &q : lib.queue) {
		if (!valid(q.styleId))
			q.styleId = builtInTemplates().front().id;
		clipContent(q.content);
	}
	if (lib.queue.size() > kMaxQueue)
		lib.queue.resize(kMaxQueue);
	if (lib.queueCurrent < -1 || lib.queueCurrent >= static_cast<int>(lib.queue.size()))
		lib.queueCurrent = -1;
	for (auto it = lib.services.begin(); it != lib.services.end();) {
		if (it->first.empty() || lib.services.size() > kMaxServices) {
			it = lib.services.erase(it);
			continue;
		}
		for (QueueItem &q : it->second) {
			if (!valid(q.styleId))
				q.styleId = builtInTemplates().front().id;
			clipContent(q.content);
		}
		if (it->second.size() > kMaxQueue)
			it->second.resize(kMaxQueue);
		++it;
	}
	clipContent(lib.draft);
	if (lib.holdOverride < -1 || lib.holdOverride > 600)
		lib.holdOverride = -1;
	lib.speed = std::clamp(lib.speed, 0, kSpeedSteps - 1);
}

double transitionScale(int step)
{
	static const double scale[kSpeedSteps] = {1.6, 1.25, 1.0, 0.75, 0.55};
	return scale[std::clamp(step, 0, kSpeedSteps - 1)];
}

} // namespace laycue::titles

namespace laycue::titles {

namespace {
// The first `count` characters (not bytes) of UTF-8 text; true if something was cut off.
bool cutUtf8(std::string &text, size_t count)
{
	size_t chars = 0, i = 0;
	while (i < text.size()) {
		if ((static_cast<unsigned char>(text[i]) & 0xC0) != 0x80) {
			if (chars == count) {
				text.resize(i);
				return true;
			}
			++chars;
		}
		++i;
	}
	return false;
}
} // namespace

std::string layerNameFor(const TitleContent &content)
{
	std::string source = !content.line1.empty()   ? content.line1
			     : !content.line2.empty() ? content.line2
						      : content.tag;
	// One line, single spaces. For a ticker, the first item.
	const size_t newline = source.find('\n');
	if (newline != std::string::npos)
		source.resize(newline);
	std::string text;
	bool space = true;
	for (const char ch : source) {
		const bool blank = ch == ' ' || ch == '\t' || ch == '\r';
		if (blank) {
			if (!space)
				text.push_back(' ');
			space = true;
		} else {
			text.push_back(ch);
			space = false;
		}
	}
	while (!text.empty() && text.back() == ' ')
		text.pop_back();
	if (text.empty())
		return std::string();
	if (cutUtf8(text, 18))
		text += "\xE2\x80\xA6"; // …
	return "ttl. " + text;
}

bool isAutoLayerName(const std::string &name, const std::string &defaultName)
{
	if (name.rfind("ttl. ", 0) == 0 || name == defaultName)
		return true;
	// "<default> 2", "<default> 3"...
	if (name.size() > defaultName.size() + 1 && name.compare(0, defaultName.size(), defaultName) == 0 &&
	    name[defaultName.size()] == ' ') {
		return std::all_of(name.begin() + static_cast<std::ptrdiff_t>(defaultName.size()) + 1, name.end(),
				   [](unsigned char c) { return c >= '0' && c <= '9'; });
	}
	return false;
}

void renameLayerRefs(Library &lib, const std::string &from, const std::string &to)
{
	if (from.empty() || to.empty() || from == to)
		return;
	if (lib.targetSource == from)
		lib.targetSource = to;
	const auto found = lib.layers.find(from);
	if (found != lib.layers.end()) {
		lib.layers[to] = found->second;
		lib.layers.erase(from);
	}
	for (QueueItem &q : lib.queue) {
		if (q.layer == from)
			q.layer = to;
	}
	for (auto &service : lib.services) {
		for (QueueItem &q : service.second) {
			if (q.layer == from)
				q.layer = to;
		}
	}
}

void rememberLayer(Library &lib, const std::string &name)
{
	if (name.empty())
		return;
	lib.layers[name] = LayerState{lib.selected, lib.draft};
}

void recallLayer(Library &lib, const std::string &name)
{
	const auto it = lib.layers.find(name);
	if (it == lib.layers.end())
		return;
	lib.selected = it->second.selected;
	lib.draft = it->second.draft;
}

} // namespace laycue::titles
