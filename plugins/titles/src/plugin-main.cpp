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

#include "obs/title-source.hpp"
#include "render/font-set.hpp"

#include <obs-module.h>
#include <plugin-support.h>
#include <util/base.h>

#include <QDir>
#include <QFileInfo>

#include <memory>

OBS_DECLARE_MODULE()
OBS_MODULE_USE_DEFAULT_LOCALE(PLUGIN_NAME, "en-US")

namespace {

std::unique_ptr<laycue::titles::FontSet> g_fonts;

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
	laycue::titles::registerTitleSource(g_fonts.get());
	return true;
}

void obs_module_unload(void)
{
	// OBS has destroyed every title source by now; nothing refers to the fonts any more.
	g_fonts.reset();
}
