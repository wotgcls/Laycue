// What the Titles panel remembers for the operator, as plain data: their saved custom templates,
// the queue of titles prepared for later, and the last things typed. Pure logic: no libobs, no Qt.
// (It is saved by store/library-io, in the OBS user settings folder, so it follows the person,
// not the scene collection.)
#pragma once

#include "core/title-templates.hpp"

#include <map>
#include <string>
#include <vector>

namespace laycue::titles {

// One line of a list: a label and a value ("N NAIRA", "4091959745").
struct Row {
	std::string label;
	std::string value;
	bool operator==(const Row &o) const { return label == o.label && value == o.value; }
};

constexpr size_t kMaxRows = 6;
constexpr int kMinLogoCut = 10;
constexpr int kMaxLogoCut = 90;

struct TitleContent {
	std::string line1;
	std::string line2;
	std::string tag;
	std::string note; // an extra line (sort code...)
	std::string logo; // path of a logo picture, or empty
	// For a logo made of a mark and a wordmark: where the picture is cut (percent of its width, or of its
	// height when stacked) and which side is the mark. 0 = not cut: the logo is one piece.
	int logoCut = 0;
	bool logoStacked = false;
	bool logoMarkSecond = false;
	std::vector<Row> rows;
	bool operator==(const TitleContent &o) const
	{
		return line1 == o.line1 && line2 == o.line2 && tag == o.tag && note == o.note && logo == o.logo &&
		       logoCut == o.logoCut && logoStacked == o.logoStacked && logoMarkSecond == o.logoMarkSecond &&
		       rows == o.rows;
	}
};

// A title prepared for later. `styleId` is a built-in id or "custom:<name>".
struct QueueItem {
	std::string styleId;
	TitleContent content;
	std::string layer = {}; // the title layer it plays on; empty = whichever layer the panel is on
};

// Where a style sits and how big it is, remembered per style (a social card may sit on the right while the
// name bars stay on the left).
struct Placement {
	int align = 0;     // Align: 0 left, 1 center, 2 right
	int percent = 100; // size, kMinSizePercent..kMaxSizePercent
	int fit = 0;       // shape width for name templates: 0 the template's own, 1 hug the text, 2 manual
	int width = 700;   // the manual width in design pixels, kMinFitWidth..kMaxFitWidth
	bool operator==(const Placement &o) const
	{
		return align == o.align && percent == o.percent && fit == o.fit && (fit != 2 || width == o.width);
	}
};

// What one title layer last held, so switching layers brings back its own template and text.
struct LayerState {
	std::string selected;
	TitleContent draft;
};

struct Library {
	std::vector<Overrides> customs;
	std::vector<QueueItem> queue; // the service run order: items stay after they play
	int queueCurrent = -1; // index of the title played last (on air, or the last one that was); -1 = none yet
	std::map<std::string, std::vector<QueueItem>> services; // saved run orders by name
	std::string serviceName;                                // the saved run order the list came from, if any
	std::string selected = "clean-bar";                     // built-in id or "custom:<name>"
	TitleContent draft;                                     // what is in the text boxes
	bool useBrand = false; // paint every title's accent in the operator's brand colour
	Rgba brand{0xFF, 0xB3, 0x1A, 255};
	int speed = 2;                               // transition speed step, 0 slowest .. kSpeedSteps-1 fastest
	int holdOverride = -1;                       // seconds on air before hiding by itself; -1 = the template's own
	std::map<std::string, Placement> placements; // by style id; a style not listed uses the default
	std::string targetSource;                    // name of the "Laycue Title" source the panel controls
	std::map<std::string, LayerState> layers;    // by source name; what each layer held when last left
	// The points bank for the Notes templates: points kept for later, shown one at a time in any order.
	std::vector<std::string> points;
	// Titles kept for later, per title layer (by source name): the speakers and names for one layer, shown one at a
	// time with that layer's template. The template stays the layer's; only the words are kept here.
	std::map<std::string, std::vector<TitleContent>> banks;
	bool pointsOneAtATime = true; // false: the Notes rows are stacked on one card as before
};

constexpr size_t kMaxPoints = 200;
constexpr size_t kMaxKept = 100; // kept titles per layer

// Keeps `content` in the layer's bank. False (nothing changes) if it is empty, already kept, or the bank is full.
bool keepTitle(Library &lib, const std::string &layer, const TitleContent &content);

// What a kept title is called in the list: its first line, then the second.
std::string keptTitleLabel(const TitleContent &content);

// The draft with only point `index` on it, numbered by its place in the bank (point 7 shows the marker "7").
// An index outside the bank gives the draft unchanged.
TitleContent contentForPoint(const TitleContent &draft, const std::vector<std::string> &points, size_t index);

// How fast titles come and go. A small range on purpose: lower thirds look wrong when they crawl or snap.
constexpr int kSpeedSteps = 5;
constexpr int kDefaultSpeed = 2;
// The multiplier on a template's own arrival and exit times for a speed step (out of range is clamped).
double transitionScale(int step);

constexpr size_t kMaxQueue = 50;
constexpr size_t kMaxServices = 30;
constexpr size_t kMaxCustoms = 40;

std::string customId(const std::string &name);
bool isCustomId(const std::string &id);
std::string customNameOf(const std::string &id);

const Overrides *findCustom(const Library &lib, const std::string &name);

// The template a style id means: a built-in one, or a custom one applied to its base. Falls back to the
// first built-in template if the id is unknown (a deleted custom template must never break a title).
// `baseId` is set to the built-in template the result draws like.
TitleTemplate resolveStyle(const Library &lib, const std::string &styleId, std::string *baseId = nullptr);

// The name a title layer gets from what it shows, so the Sources panel says which is which: "ttl. Pastor David…"
// (cut short with an ellipsis). Empty if there is nothing to name it by.
std::string layerNameFor(const TitleContent &content);
// Layers Laycue named itself ("ttl. ..." or the plain default name) may be renamed again; a name the operator chose
// is left alone. `defaultName` is the plain name a new layer starts with.
bool isAutoLayerName(const std::string &name, const std::string &defaultName);
// A layer was renamed: everything that remembered its old name (the panel's target, saved layers, queue rows,
// saved services) follows.
void renameLayerRefs(Library &lib, const std::string &from, const std::string &to);

// Saves the template and text now in the panel under the layer `name`.
void rememberLayer(Library &lib, const std::string &name);
// Brings back what `name` held. A layer the panel has not held yet keeps what is in the panel now.
void recallLayer(Library &lib, const std::string &name);

Placement placementFor(const Library &lib, const std::string &styleId);
// Stores it (and forgets it again when it is the default). Numbers are kept in range.
void setPlacement(Library &lib, const std::string &styleId, Placement placement);

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

// The service run order. `queueCurrent` is the title played last; Next plays the one after it.
// Each returns an index into lib.queue, or -1 when there is nothing there.
int nextQueueIndex(const Library &lib);
int previousQueueIndex(const Library &lib);
// Replaces one row (the operator edited it). False if there is no such row.
bool replaceInQueue(Library &lib, size_t index, const QueueItem &item);

// Saved run orders ("Sunday 9am"). Saving the same name again replaces it.
bool saveService(Library &lib, const std::string &name);
// Puts a saved run order in the list (back at the top). False if there is no such name.
bool loadService(Library &lib, const std::string &name);
bool deleteService(Library &lib, const std::string &name);

// Repairs a library read from a file: bad ids, over-long text, too many entries.
void sanitize(Library &lib);

} // namespace laycue::titles
