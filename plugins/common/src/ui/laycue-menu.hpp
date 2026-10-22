// The "Laycue" menu in OBS's menu bar, between Tools and Help. Every Laycue plugin adds its own entry to it
// (the first one to load makes the menu), so the pack is one menu however many plugins are installed.
#pragma once

#include <QString>

class QWidget;

namespace laycue::ui {

// `dockContent` is the widget given to obs_frontend_add_dock_by_id. The entry shows or hides that dock,
// with a tick while it is shown. Does nothing if OBS has no such dock (a safe no-op, never an error).
void addToLaycueMenu(QWidget *dockContent, const QString &menuTitle, const QString &itemText);

} // namespace laycue::ui
