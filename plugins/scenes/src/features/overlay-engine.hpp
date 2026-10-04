// Applies the overlay rules: when a labelled overlay is switched on, hide the overlays the rules
// say it replaces. Glue between the pure rule logic (core/exclusivity) and OBS.
#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace laycue {

class App;

struct Replacement {
	std::string shownName;
	std::vector<std::string> hiddenNames;
	bool sameSpot = true; // false when a strict rule hid them wherever they were
};

class OverlayEngine {
public:
	explicit OverlayEngine(const App &app) : app_(app) {}

	// An item was shown or hidden somewhere. UI thread only. Returns what was replaced, if anything.
	std::optional<Replacement> handleItemVisible(const std::string &sceneUuid, int64_t itemId, bool visible);

private:
	const App &app_;
	bool busy_ = false; // true while Laycue itself is hiding overlays, so it doesn't react to its own changes
};

} // namespace laycue
