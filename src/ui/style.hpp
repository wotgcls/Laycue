// The one style sheet for every Laycue page. Put on the inner container that only Laycue touches
// (OBS may restyle the widget it docks), with objectName "LaycueContent".
#pragma once

#include <QString>

class QWidget;

namespace laycue::ui {

QString panelStyleSheet();

// Touch-friendly sizes on or off (the "Large buttons" setting): sets a property the style sheet reads.
void setLargeControls(QWidget *content, bool large);

} // namespace laycue::ui
