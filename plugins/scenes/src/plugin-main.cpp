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

#include "features/app.hpp"
#include "features/hotkeys.hpp"
#include "obs/brand-transitions.hpp"
#include "ui/laycue-dock.hpp"
#include "update/updater.hpp"

#include "ui/laycue-menu.hpp"

#include <obs-frontend-api.h>
#include <obs-module.h>
#include <plugin-support.h>

#include <QMainWindow>
#include <QPointer>
#include <QString>
#include <QTimer>

#include <chrono>
#include <memory>

OBS_DECLARE_MODULE()
OBS_MODULE_USE_DEFAULT_LOCALE(PLUGIN_NAME, "en-US")

namespace {

constexpr const char *kDockId = "laycue";

// Once added, OBS's main window owns the dock and deletes it on exit.
// QPointer becomes null automatically when that happens.
QPointer<laycue::LaycueDock> g_dock;

// The one top-level object. Created first, destroyed last, so the dock never outlives it.
std::unique_ptr<laycue::App> g_app;
std::unique_ptr<laycue::Hotkeys> g_hotkeys; // destroyed first, before the app
// The quiet "check when OBS starts" updater (Settings opt-in); reset in obs_module_unload.
std::unique_ptr<laycue::update::Updater> g_autoUpdater;

void onFrontendEvent(enum obs_frontend_event event, void *)
{
	try {
		if (g_app)
			g_app->handleFrontendEvent(event);
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
		laycue::registerBrandTransitions(); // in OBS's transition list, whether or not the panel opens
		g_app = std::make_unique<laycue::App>();
		auto *mainWindow = static_cast<QMainWindow *>(obs_frontend_get_main_window());
		auto *dock = new laycue::LaycueDock(*g_app, mainWindow);

		if (!obs_frontend_add_dock_by_id(kDockId, obs_module_text("Laycue.DockTitle"), dock)) {
			obs_log(LOG_ERROR, "Could not add the Laycue dock (id '%s' already in use?)", kDockId);
			delete dock;
			return true; // stay loaded; OBS keeps working without the dock
		}
		g_dock = dock;
		laycue::ui::addToLaycueMenu(dock, QString::fromUtf8(obs_module_text("Laycue.MenuTitle")),
					    QString::fromUtf8(obs_module_text("Laycue.MenuItem")));
		g_hotkeys = std::make_unique<laycue::Hotkeys>(dock, [](laycue::HotkeyAction action) {
			if (g_dock)
				g_dock->runHotkey(action);
		});

		// When Settings says to, quietly check for a newer release once OBS is up. The log tells the
		// story ("update available: Laycue x.y.z"), and the Settings page's Updates card re-checks and
		// shows it when opened.
		if (g_app->config().settings.autoUpdate) {
			g_autoUpdater = std::make_unique<laycue::update::Updater>(QString::fromUtf8(PLUGIN_VERSION));
			auto *updater = g_autoUpdater.get();
			QObject::connect(updater, &laycue::update::Updater::changed, updater, [updater] {
				if (updater->state() == laycue::update::Updater::State::Available)
					obs_log(LOG_INFO, "update available: Laycue %s (installed %s)",
						updater->latestVersion().toUtf8().constData(),
						updater->currentVersion().toUtf8().constData());
			});
			QTimer::singleShot(std::chrono::seconds(15), updater, [updater] { updater->check(); });
		}

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
	g_autoUpdater.reset();
	g_hotkeys.reset();
	g_app.reset();
	obs_log(LOG_INFO, "plugin unloaded");
}
