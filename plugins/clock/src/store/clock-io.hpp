// Saves and loads what the Clock panel remembers (the settings and which clock layer it controls) as a JSON file in
// the OBS user settings folder. Never throws; a missing or damaged file gives the defaults.
#pragma once

#include "core/clock-settings.hpp"

#include <string>

namespace laycue::clock {

struct PanelState {
	ClockSettings settings;
	std::string targetSource; // name of the "Laycue Clock" source the panel controls
};

PanelState loadPanelState();
bool savePanelState(const PanelState &state);

} // namespace laycue::clock
