// Display names for things stored as ids (tags, source kinds), read from the locale file.
#pragma once

#include "core/source-kinds.hpp"

#include <QString>

#include <string>

namespace laycue::ui {

// "scripture" → "Scripture". A custom tag the operator typed is shown as typed.
QString tagDisplayName(const std::string &tag);

QString kindDisplayName(SourceKind kind);

// obs_module_text as a QString.
QString lt(const char *key);

} // namespace laycue::ui
