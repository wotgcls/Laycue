// Builds a layout's own OBS scene: each slot's source fitted into its share of the canvas.
// Laycue only changes the scene it made; the operator's sources are shared into it, never edited.
#pragma once

#include "core/config.hpp"

#include <string>

namespace laycue {

struct LayoutBuildResult {
	bool ok = false;
	std::string error;
	Layout layout; // with sceneUuid and sceneName filled in
};

// Makes the layout's scene if it doesn't exist (or repairs it if it does), puts each slot's
// source in place and takes out anything else. Safe to call again at any time.
LayoutBuildResult buildLayoutScene(const Layout &layout);

// Fits every slot's source again (a camera's real size is only known once OBS has drawn it).
bool fitLayout(const Layout &layout);

bool layoutSceneExists(const Layout &layout);

// Renames Laycue's scene to match a new layout name.
bool renameLayoutScene(const Layout &layout, const std::string &newName);

// Removes Laycue's scene. Never touches the operator's own sources.
bool removeLayoutScene(const Layout &layout);

} // namespace laycue
