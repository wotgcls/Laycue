/*
Laycue Clock: countdowns, count-ups and clocks drawn inside OBS, as an overlay on video or on a background
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
#include "obs/clock-source.hpp"
#include "render/clock-renderer.hpp"
#include "ui/clock-dock.hpp"

#include <obs-frontend-api.h>
#include <obs-module.h>
#include <plugin-support.h>
#include <util/base.h>

#include <QFileInfo>
#include <QMainWindow>
#include <QPointer>

#include <memory>

OBS_DECLARE_MODULE()
OBS_MODULE_USE_DEFAULT_LOCALE(PLUGIN_NAME, "en-US")

namespace {

constexpr const char *kDockId = "laycue_clock";

std::unique_ptr<laycue::clock::ClockFonts> g_fonts;
QPointer<laycue::clock::ui::ClockDock> g_dock; // owned by OBS's main window once added
std::unique_ptr<laycue::HotkeySet> g_hotkeys;

} // namespace

bool obs_module_load(void)
{
	char *probe = obs_module_file("fonts/BarlowCondensed-Bold.ttf");
	if (!probe)
		obs_log(LOG_ERROR, "Laycue Clock: its fonts folder was not found; the clock will use a system font");
	const QString folder = probe ? QFileInfo(QString::fromUtf8(probe)).absolutePath() : QString();
	bfree(probe);

	g_fonts = std::make_unique<laycue::clock::ClockFonts>(folder);
	obs_log(LOG_INFO, "Laycue Clock %s loaded (%d fonts)", PLUGIN_VERSION, g_fonts->loadedCount());
	laycue::clock::registerClockSource(g_fonts.get());

	try {
		auto *mainWindow = static_cast<QMainWindow *>(obs_frontend_get_main_window());
		auto *dock = new laycue::clock::ui::ClockDock(mainWindow);
		if (obs_frontend_add_dock_by_id(kDockId, obs_module_text("Laycue.Clock.DockTitle"), dock)) {
			g_dock = dock;
			g_hotkeys = std::make_unique<laycue::HotkeySet>(
				dock, "laycue_clock_hotkeys",
				std::vector<laycue::HotkeySet::Definition>{
					{"Laycue.Clock.Toggle", "Laycue.Clock.Hotkey.PanelToggle"},
					{"Laycue.Clock.Reset", "Laycue.Clock.Hotkey.PanelReset"},
				},
				[](int index) {
					if (g_dock)
						g_dock->runHotkey(index);
				});
		} else {
			obs_log(LOG_ERROR, "Laycue Clock: could not add the panel (id '%s' already in use?)", kDockId);
			delete dock;
		}
	} catch (...) {
		obs_log(LOG_ERROR,
			"Laycue Clock: the panel could not start; the clock still works from the Properties window");
	}
	return true;
}

void obs_module_unload(void)
{
	g_hotkeys.reset();
	g_fonts.reset();
}
