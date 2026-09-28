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

struct Settings {
	bool insideFade = true; // switching the camera inside a pushback: fade (true) or cut
	int insideFadeMs = 400;
	bool largeButtons = false; // touchscreen sizes
	bool locked = false;       // hides everything that edits setup
	bool showTips = true;      // guide cards and hints
	bool autoKey = true;       // "Remove the background colour for me" is on by default in Add pushback
};

struct Config {
	int version = kConfigVersion;
	Settings settings;
	std::vector<TagAssignment> tags;
	std::vector<Rule> rules;
	std::vector<Pushback> pushbacks;
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

bool tipDismissed(const Config &config, const std::string &tipId);
void dismissTip(Config &config, const std::string &tipId);

// A short id for a new pushback that no other pushback uses.
std::string newPushbackId(const Config &config);

} // namespace laycue
