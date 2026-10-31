/*
Laycue Scripture: type a Bible reference and put the verse on screen, inside OBS
Copyright (C) 2026 Laycue

This program is free software; you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation; either version 2 of the License, or
(at your option) any later version.

This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.

You should have received a copy of the GNU General Public License along
with this program. If not, see <https://www.gnu.org/licenses/>
*/

#include "hotkey-set.hpp"
#include "slides/slide-source.hpp"
#include "ui/laycue-menu.hpp"
#include "ui/scripture-dock.hpp"

#include <obs-frontend-api.h>
#include <obs-module.h>
#include <plugin-support.h>

#include <QMainWindow>
#include <QPointer>

#include <memory>

OBS_DECLARE_MODULE()
OBS_MODULE_USE_DEFAULT_LOCALE(PLUGIN_NAME, "en-US")

namespace {

constexpr const char *kDockId = "laycue_scripture";

QPointer<laycue::scripture::ScriptureDock> g_dock; // owned by OBS's main window once added
std::unique_ptr<laycue::HotkeySet> g_hotkeys;

} // namespace

bool obs_module_load(void)
{
	laycue::slides::registerSlideSource("laycue_scripture", "Laycue.Scripture.SourceName");
	obs_log(LOG_INFO, "Laycue Scripture %s loaded", PLUGIN_VERSION);
	try {
		auto *mainWindow = static_cast<QMainWindow *>(obs_frontend_get_main_window());
		auto *dock = new laycue::scripture::ScriptureDock(mainWindow);
		if (obs_frontend_add_dock_by_id(kDockId, obs_module_text("Laycue.Scripture.DockTitle"), dock)) {
			g_dock = dock;
			// The source in the Sources list opens this panel: one button brings it forward.
			laycue::slides::registerSourceReveal("laycue_scripture", [dock] { dock->bringForward(); });
			laycue::ui::addToLaycueMenu(dock, QString::fromUtf8(obs_module_text("Laycue.MenuTitle")),
						    QString::fromUtf8(obs_module_text("Laycue.Scripture.MenuItem")));
			g_hotkeys = std::make_unique<laycue::HotkeySet>(
				dock, "laycue_scripture_hotkeys",
				std::vector<laycue::HotkeySet::Definition>{
					{"Laycue.Scripture.Toggle", "Laycue.Scripture.Hotkey.Toggle"},
					{"Laycue.Scripture.NextVerse", "Laycue.Scripture.Hotkey.Next"},
					{"Laycue.Scripture.PreviousVerse", "Laycue.Scripture.Hotkey.Previous"},
				},
				[](int index) {
					if (g_dock)
						g_dock->runHotkey(index);
				});
		} else {
			obs_log(LOG_ERROR, "Laycue Scripture: could not add the panel (id '%s' already in use?)",
				kDockId);
			delete dock;
		}
	} catch (...) {
		obs_log(LOG_ERROR, "Laycue Scripture: the panel could not start");
	}
	return true;
}

void obs_module_unload(void)
{
	g_hotkeys.reset();
}
