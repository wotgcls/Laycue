// Turns Laycue's Config into OBS's own data format and back. That is how it is saved with the
// scene collection, and how settings are exported and imported as JSON.
#pragma once

#include "core/config.hpp"

#include <obs.hpp>

#include <string>

namespace laycue {

// Config → obs_data (caller owns the reference).
OBSData configToData(const Config &config);

// obs_data → Config. Missing or wrong fields fall back to defaults; the result is sanitized.
Config configFromData(obs_data_t *data);

std::string configToJson(const Config &config);

// False if the text isn't valid JSON.
bool configFromJson(const std::string &json, Config &out);

} // namespace laycue
