/*
Laycue Network: choose the right bitrate and protect the stream
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
#include "ui/network-dock.hpp"

#include <obs-frontend-api.h>
#include <obs-module.h>
#include <plugin-support.h>

#include <QMainWindow>
#include <QPointer>

#include <memory>

OBS_DECLARE_MODULE()
OBS_MODULE_USE_DEFAULT_LOCALE(PLUGIN_NAME, "en-US")

namespace {

constexpr const char *kDockId = "laycue_network";

QPointer<laycue::network::ui::NetworkDock> g_dock; // owned by OBS's main window once added
std::unique_ptr<laycue::HotkeySet> g_hotkeys;

void onFrontendEvent(enum obs_frontend_event event, void *)
{
	try {
		if (g_dock)
			g_dock->handleFrontendEvent(event);
	} catch (...) {
		obs_log(LOG_ERROR, "Laycue Network: error handling OBS event %d", static_cast<int>(event));
	}
}

} // namespace

bool obs_module_load(void)
{
	try {
		auto *mainWindow = static_cast<QMainWindow *>(obs_frontend_get_main_window());
		auto *dock = new laycue::network::ui::NetworkDock(mainWindow);
		if (obs_frontend_add_dock_by_id(kDockId, obs_module_text("Laycue.Network.DockTitle"), dock)) {
			g_dock = dock;
			g_hotkeys = std::make_unique<laycue::HotkeySet>(
				dock, "laycue_network_hotkeys",
				std::vector<laycue::HotkeySet::Definition>{
					{"Laycue.Network.ToggleAuto", "Laycue.Network.Hotkey.ToggleAuto"},
					{"Laycue.Network.TryFull", "Laycue.Network.Hotkey.TryFull"},
					{"Laycue.Network.RunTest", "Laycue.Network.Hotkey.RunTest"},
				},
				[](int index) {
					if (g_dock)
						g_dock->runHotkey(index);
				});
			obs_frontend_add_event_callback(onFrontendEvent, nullptr);
			obs_log(LOG_INFO, "Laycue Network %s loaded", PLUGIN_VERSION);
		} else {
			obs_log(LOG_ERROR, "Laycue Network: could not add the panel (id '%s' already in use?)",
				kDockId);
			delete dock;
		}
	} catch (...) {
		obs_log(LOG_ERROR, "Laycue Network: the panel could not start");
	}
	return true;
}

void obs_module_unload(void)
{
	obs_frontend_remove_event_callback(onFrontendEvent, nullptr);
	g_hotkeys.reset();
}
