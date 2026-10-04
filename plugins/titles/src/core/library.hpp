// What the Titles panel remembers for the operator, as plain data: their saved custom templates,
// the queue of titles prepared for later, and the last things typed. Pure logic: no libobs, no Qt.
// (It is saved by store/library-io, in the OBS user settings folder, so it follows the person,
// not the scene collection.)
#pragma once

#include "core/title-templates.hpp"

#include <string>
#include <vector>

namespace laycue::titles {

struct TitleContent {
	std::string line1;
	std::string line2;
	std::string tag;
	bool operator==(const TitleContent &o) const { return line1 == o.line1 && line2 == o.line2 && tag == o.tag; }
};

// A title prepared for later. `styleId` is a built-in id or "custom:<name>".
struct QueueItem {
	std::string styleId;
	TitleContent content;
};

struct Library {
	std::vector<Overrides> customs;
	std::vector<QueueItem> queue;
	std::string selected = "clean-bar"; // built-in id or "custom:<name>"
	TitleContent draft;                 // what is in the text boxes
	bool useBrand = false;              // paint every title's accent in the operator's brand colour
	Rgba brand{0xFF, 0xB3, 0x1A, 255};
	int holdOverride = -1;    // seconds on air before hiding by itself; -1 = the template's own
	std::string targetSource; // name of the "Laycue Title" source the panel controls
};

constexpr size_t kMaxQueue = 50;
constexpr size_t kMaxCustoms = 40;

std::string customId(const std::string &name);
bool isCustomId(const std::string &id);
std::string customNameOf(const std::string &id);

const Overrides *findCustom(const Library &lib, const std::string &name);

// The template a style id means: a built-in one, or a custom one applied to its base. Falls back to the
// first built-in template if the id is unknown (a deleted custom template must never break a title).
// `baseId` is set to the built-in template the result draws like.
TitleTemplate resolveStyle(const Library &lib, const std::string &styleId, std::string *baseId = nullptr);

// A name nobody uses yet, based on `wanted` ("Sunday", "Sunday 2", ...).
std::string uniqueCustomName(const Library &lib, const std::string &wanted);

// Adds or replaces (same name) a custom template. False if the library is full.
bool saveCustom(Library &lib, const Overrides &overrides);
// Removes it; anything that pointed to it (selected, queued) falls back to its base template.
bool removeCustom(Library &lib, const std::string &name);

bool enqueue(Library &lib, const QueueItem &item);
bool removeFromQueue(Library &lib, size_t index);
bool moveInQueue(Library &lib, size_t from, size_t to);
// Takes the first item out of the queue. False if it is empty.
bool popQueue(Library &lib, QueueItem &out);

// Repairs a library read from a file: bad ids, over-long text, too many entries.
void sanitize(Library &lib);

} // namespace laycue::titles
