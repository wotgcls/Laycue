// Saves and loads the Network settings as JSON in the OBS user settings folder.
// Never throws; a missing or damaged file gives the defaults.
#pragma once

#include "core/network-settings.hpp"

namespace laycue::network {

Settings loadSettings();
bool saveSettings(const Settings &settings);

} // namespace laycue::network
