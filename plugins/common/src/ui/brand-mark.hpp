// The Laycue mark shown at the left of every panel header, picked for the OBS theme.
#pragma once

class QLabel;
class QWidget;

namespace laycue::ui {

// A small label holding the logo (empty, never an error, if the picture can't be found).
QLabel *brandMark(QWidget *parent);

} // namespace laycue::ui
