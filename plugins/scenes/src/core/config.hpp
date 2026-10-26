// Everything Laycue remembers, as plain data. It is saved with the OBS scene collection
// (store/config-io). Pure logic: no libobs, no Qt.
#pragma once

#include "core/detect.hpp"
#include "core/exclusivity.hpp"
#include "core/geometry.hpp"

#include <string>
#include <vector>

namespace laycue {

constexpr int kConfigVersion = 1;

// An overlay source the operator (or Auto-tag) has labelled.
struct TagAssignment {
	std::string sourceUuid;
	std::string sourceName; // last known name, for display and as a fallback if the UUID is gone
	std::string tag;
};

// Something that can show inside a pushback's window: a camera, any video source, or a scene.
struct Feed {
	std::string uuid;
	std::string name;
};

enum class FitMode { Fill, Whole };

struct Pushback {
	std::string id;
	std::string name;
	std::string sceneUuid; // the scene Laycue built for it
	std::string sceneName;
	std::string imageUuid; // the pushback picture
	std::string imageName;
	int imageWidth = 0;
	int imageHeight = 0;
	Rect window; // in the picture's own pixels
	FitMode fit = FitMode::Fill;
	std::vector<Feed> feeds;
	std::string activeFeed; // UUID of the feed showing inside the window
	bool keyed = false;     // Laycue added a Chroma Key to the picture
	Rgb keyColour;
};

// One slot of a layout: which source sits in it.
struct SlotSource {
	std::string uuid;
	std::string name;
};

// A layout the operator made from a template (split screen, picture in picture...). Laycue builds
// an OBS scene for it, "Laycue · <name>", with one source in each slot.
struct Layout {
	std::string id;
	std::string name;
	std::string templateId;
	std::string sceneUuid;
	std::string sceneName;
	FitMode fit = FitMode::Fill;
	std::vector<SlotSource> sources; // one per slot of the template, in order; uuid empty = empty slot
	int split = 0; // first part's share in percent for a two-part split; 0 = the template's own proportion
	// An optional background picture. With one, each slot gets a gold or silver frame picked to suit it.
	std::string background;     // path of the picture; empty = none
	std::string framePath;      // the frame picture Laycue drew for this layout
	std::string metal;          // "gold" or "silver" (what was picked), for display
	std::string backgroundUuid; // the scene's image sources, so a rebuild finds them again
	std::string frameUuid;
};

struct Settings {
	bool insideFade = true; // switching the camera inside a pushback: fade (true) or cut
	int insideFadeMs = 400;
	bool largeButtons = false; // touchscreen sizes
	bool locked = false;       // hides everything that edits setup
	bool showTips = true;      // guide cards and hints
	bool autoKey = true;       // "Remove the background colour for me" is on by default in Add pushback
	bool laycueView = false;   // hide OBS's own Preview/Program area so Laycue is the main view
	bool autoUpdate = false;   // check the published releases for a newer Laycue quietly when OBS starts
	bool requeueLive = true;   // after Go Live, Up next fills with the shot that was live (like OBS's own swap)
	// Fail-over: watch one source and, if it goes blank or freezes while it is on the air, put one
	// shot live at once. Off by default, and nothing ever switches back by itself.
	bool failoverOn = false;
	std::string failoverSourceUuid; // the source being watched (usually a camera)
	std::string failoverSourceName; // last known name, for display if the UUID is gone
	std::string failoverShotUuid;   // the shot put live on a failure
	std::string failoverShotName;
};

struct Config {
	int version = kConfigVersion;
	Settings settings;
	std::vector<TagAssignment> tags;
	std::vector<Rule> rules;
	std::vector<Pushback> pushbacks;
	std::vector<Layout> layouts;
	std::vector<std::string> pinned;        // sources (UUIDs) kept in every scene, including scenes made later
	std::vector<std::string> dismissedTips; // guide cards the operator has closed
	bool rulesInitialised = false;          // the starter rule has been offered once
	bool wentLive = false;                  // Go Live or Cut has been used (ticks off the getting-started guide)
};

// Brings a config read from an older or damaged file to the current shape: fills in the
// version, clamps out-of-range numbers, drops entries that can't work. Never throws.
void sanitize(Config &config);

// The tag on a source, or "" if it has none.
std::string tagOf(const Config &config, const std::string &sourceUuid);

// Sets or clears (empty tag) a source's tag.
void setTag(Config &config, const std::string &sourceUuid, const std::string &sourceName, const std::string &tagName);

// Pinned sources show in every scene (also in scenes made later).
bool isPinned(const Config &config, const std::string &sourceUuid);
void setPinned(Config &config, const std::string &sourceUuid, bool pinned);

bool tipDismissed(const Config &config, const std::string &tipId);
void dismissTip(Config &config, const std::string &tipId);

// A short id for a new pushback that no other pushback uses.
std::string newPushbackId(const Config &config);
std::string newLayoutId(const Config &config);

} // namespace laycue
