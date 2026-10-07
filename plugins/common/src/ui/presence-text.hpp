// One plain sentence about where a layer is: "On Program now, in Worship" or "Not in the live scene".
#pragma once

#include "obs/scene-presence.hpp"

#include <QColor>
#include <QString>

namespace laycue::ui {

// `live` is true when viewers can see the layer on Program right now (green line), otherwise the line is quiet.
QString presenceText(const ScenePresence &presence, bool &live);

} // namespace laycue::ui
