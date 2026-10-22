// Locale text for the panel: obs_module_text as a QString. Each plugin of the pack has its own
// locale file; this resolves in whichever plugin the code is linked into.
#pragma once

#include <QString>

namespace laycue::ui {

QString lt(const char *key);

} // namespace laycue::ui
