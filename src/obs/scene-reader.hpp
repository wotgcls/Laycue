// Reads OBS's current scenes, items and canvas into a plain Snapshot.
// Call from the UI thread, only after OBS has finished loading.
#pragma once

#include "core/scene-model.hpp"

namespace laycue {

Snapshot readSnapshot();

} // namespace laycue
