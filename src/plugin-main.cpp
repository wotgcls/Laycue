/*
Laycue — layout and layer manager for OBS Studio
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

#include "ui/laycue-dock.hpp"

#include <obs-frontend-api.h>
#include <obs-module.h>
#include <plugin-support.h>

#include <QMainWindow>
#include <QPointer>

OBS_DECLARE_MODULE()
OBS_MODULE_USE_DEFAULT_LOCALE(PLUGIN_NAME, "en-US")

namespace {

constexpr const char *kDockId = "laycue";

// Once added, OBS's main window owns the dock and deletes it on exit.
// QPointer becomes null automatically when that happens.
QPointer<laycue::LaycueDock> g_dock;

void onFrontendEvent(enum obs_frontend_event event, void *)
{
	try {
		if (g_dock)
			g_dock->handleFrontendEvent(event);
	} catch (const std::exception &error) {
		obs_log(LOG_ERROR, "Error handling OBS event %d: %s", static_cast<int>(event), error.what());
	} catch (...) {
		obs_log(LOG_ERROR, "Error handling OBS event %d", static_cast<int>(event));
	}
}

} // namespace

bool obs_module_load(void)
{
	try {
		auto *mainWindow = static_cast<QMainWindow *>(obs_frontend_get_main_window());
		auto *dock = new laycue::LaycueDock(mainWindow);

		if (!obs_frontend_add_dock_by_id(kDockId, obs_module_text("Laycue.DockTitle"), dock)) {
			obs_log(LOG_ERROR, "Could not add the Laycue dock (id '%s' already in use?)", kDockId);
			delete dock;
			return true; // stay loaded; OBS keeps working without the dock
		}
		g_dock = dock;

		obs_frontend_add_event_callback(onFrontendEvent, nullptr);
		obs_log(LOG_INFO, "plugin loaded successfully (version %s)", PLUGIN_VERSION);
	} catch (const std::exception &error) {
		obs_log(LOG_ERROR, "Failed to start: %s", error.what());
	} catch (...) {
		obs_log(LOG_ERROR, "Failed to start: unknown error");
	}
	return true;
}

void obs_module_unload(void)
{
	obs_frontend_remove_event_callback(onFrontendEvent, nullptr);
	obs_log(LOG_INFO, "plugin unloaded");
}
