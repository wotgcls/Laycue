/*
Laycue Titles: broadcast lower thirds, tickers and scripture bars made inside OBS
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
#include "obs/title-source.hpp"
#include "ui/titles-dock.hpp"
#include "render/font-set.hpp"

#include <obs-frontend-api.h>
#include <obs-module.h>
#include <plugin-support.h>
#include <util/base.h>

#include <QDir>
#include <QFileInfo>
#include <QMainWindow>
#include <QPointer>

#include <memory>

OBS_DECLARE_MODULE()
OBS_MODULE_USE_DEFAULT_LOCALE(PLUGIN_NAME, "en-US")

namespace {

constexpr const char *kDockId = "laycue_titles";

std::unique_ptr<laycue::titles::FontSet> g_fonts;
QPointer<laycue::titles::ui::TitlesDock> g_dock; // owned by OBS's main window once added
std::unique_ptr<laycue::HotkeySet> g_hotkeys;

void onFrontendEvent(enum obs_frontend_event event, void *)
{
	try {
		if (g_dock)
			g_dock->handleFrontendEvent(event);
	} catch (...) {
		obs_log(LOG_ERROR, "Laycue Titles: error handling OBS event %d", static_cast<int>(event));
	}
}

} // namespace

bool obs_module_load(void)
{
	char *probe = obs_module_file("fonts/BarlowCondensed-SemiBold.ttf");
	if (!probe) {
		obs_log(LOG_ERROR, "Laycue Titles: its fonts folder was not found; titles will use a system font");
	}
	const QString folder = probe ? QFileInfo(QString::fromUtf8(probe)).absolutePath() : QString();
	bfree(probe);

	g_fonts = std::make_unique<laycue::titles::FontSet>(folder);
	obs_log(LOG_INFO, "Laycue Titles %s loaded (%d fonts)", PLUGIN_VERSION, g_fonts->loadedCount());
	laycue::titles::setSharedFonts(g_fonts.get());
	laycue::titles::registerTitleSource(g_fonts.get());

	try {
		auto *mainWindow = static_cast<QMainWindow *>(obs_frontend_get_main_window());
		auto *dock = new laycue::titles::ui::TitlesDock(mainWindow);
		if (obs_frontend_add_dock_by_id(kDockId, obs_module_text("Laycue.Titles.DockTitle"), dock)) {
			g_dock = dock;
			g_hotkeys = std::make_unique<laycue::HotkeySet>(
				dock, "laycue_titles_hotkeys",
				std::vector<laycue::HotkeySet::Definition>{
					{"Laycue.Titles.Toggle", "Laycue.Titles.Hotkey.PanelToggle"},
					{"Laycue.Titles.PlayNext", "Laycue.Titles.Hotkey.PlayNext"},
					{"Laycue.Titles.HideAll", "Laycue.Titles.Hotkey.HideAll"},
					{"Laycue.Titles.PlayPrevious", "Laycue.Titles.Hotkey.PlayPrevious"},
				},
				[](int index) {
					if (g_dock)
						g_dock->runHotkey(index);
				});
			obs_frontend_add_event_callback(onFrontendEvent, nullptr);
		} else {
			obs_log(LOG_ERROR, "Laycue Titles: could not add the panel (id '%s' already in use?)", kDockId);
			delete dock;
		}
	} catch (...) {
		obs_log(LOG_ERROR,
			"Laycue Titles: the panel could not start; titles still work from the Properties window");
	}
	return true;
}

void obs_module_unload(void)
{
	obs_frontend_remove_event_callback(onFrontendEvent, nullptr);
	g_hotkeys.reset();
	// OBS has destroyed every title source by now; nothing refers to the fonts any more.
	laycue::titles::setSharedFonts(nullptr);
	g_fonts.reset();
}
