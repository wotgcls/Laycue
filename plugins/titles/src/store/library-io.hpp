// Saves and loads the Titles library (custom templates, queue, last used) as a JSON file in the OBS
// user settings folder. Never throws; a missing or damaged file gives an empty library.
#pragma once

#include "core/library.hpp"

namespace laycue::titles {

Library loadLibrary();
bool saveLibrary(const Library &library);

} // namespace laycue::titles
