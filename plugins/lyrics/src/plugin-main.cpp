/*
Laycue Lyrics: type a song name and put its words on screen two lines at a time, inside OBS
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
#include "ui/lyrics-dock.hpp"

#include <obs-frontend-api.h>
#include <obs-module.h>
#include <plugin-support.h>

#include <QMainWindow>
#include <QPointer>

#include <memory>

OBS_DECLARE_MODULE()
OBS_MODULE_USE_DEFAULT_LOCALE(PLUGIN_NAME, "en-US")

namespace {

constexpr const char *kDockId = "laycue_lyrics";

QPointer<laycue::lyrics::LyricsDock> g_dock; // owned by OBS's main window once added
std::unique_ptr<laycue::HotkeySet> g_hotkeys;

} // namespace

bool obs_module_load(void)
{
	laycue::slides::registerSlideSource("laycue_lyrics", "Laycue.Lyrics.SourceName");
	obs_log(LOG_INFO, "Laycue Lyrics %s loaded", PLUGIN_VERSION);
	try {
		auto *mainWindow = static_cast<QMainWindow *>(obs_frontend_get_main_window());
		auto *dock = new laycue::lyrics::LyricsDock(mainWindow);
		if (obs_frontend_add_dock_by_id(kDockId, obs_module_text("Laycue.Lyrics.DockTitle"), dock)) {
			g_dock = dock;
			laycue::ui::addToLaycueMenu(dock, QString::fromUtf8(obs_module_text("Laycue.MenuTitle")),
						    QString::fromUtf8(obs_module_text("Laycue.Lyrics.MenuItem")));
			g_hotkeys = std::make_unique<laycue::HotkeySet>(
				dock, "laycue_lyrics_hotkeys",
				std::vector<laycue::HotkeySet::Definition>{
					{"Laycue.Lyrics.Toggle", "Laycue.Lyrics.Hotkey.Toggle"},
					{"Laycue.Lyrics.NextLines", "Laycue.Lyrics.Hotkey.Next"},
					{"Laycue.Lyrics.PreviousLines", "Laycue.Lyrics.Hotkey.Previous"},
				},
				[](int index) {
					if (g_dock)
						g_dock->runHotkey(index);
				});
		} else {
			obs_log(LOG_ERROR, "Laycue Lyrics: could not add the panel (id '%s' already in use?)", kDockId);
			delete dock;
		}
	} catch (...) {
		obs_log(LOG_ERROR, "Laycue Lyrics: the panel could not start");
	}
	return true;
}

void obs_module_unload(void)
{
	g_hotkeys.reset();
}
