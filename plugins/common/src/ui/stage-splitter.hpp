// The top and bottom of a panel with a bar between them: the top (the live controls) starts at its natural height,
// and dragging the bar gives the part below (templates, shots, tabs) more room, or less.
#pragma once

#include <QSplitter>

namespace laycue::ui {

// `top` scrolls on its own if it is made shorter than it needs; so does `bottom` when `scrollBottom` is set (leave it
// off when the bottom already scrolls inside).
QSplitter *stageSplitter(QWidget *top, QWidget *bottom, QWidget *parent, bool scrollBottom = false);

} // namespace laycue::ui
