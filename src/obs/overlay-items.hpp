// Reads and changes the scene items Laycue cares about as overlays. The only place overlay
// work talks to libobs. Call from the UI thread.
//
// An item inside a group is identified by  groupItemId * 2^32 + itemId, so an ItemRef (scene +
// number) is enough to find it again. Top-level items just use their own id.
#pragma once

#include "core/geometry.hpp"
#include "core/source-kinds.hpp"

#include <cstdint>
#include <string>
#include <vector>

namespace laycue {

constexpr int64_t kGroupIdStep = int64_t(1) << 32;

struct OverlayItem {
	std::string sceneUuid; // the top-level scene the item is in
	std::string sceneName;
	int64_t itemId = 0;
	std::string sourceUuid;
	std::string sourceName;
	SourceKind kind = SourceKind::Other;
	bool visible = false;
	RectF box; // where the item shows on the canvas (its bounding box), no pixel measuring
};

// Every item (including items inside groups) of every scene. Scenes nested as sources are skipped.
std::vector<OverlayItem> readAllItems();

// Every item of one scene.
std::vector<OverlayItem> readSceneItems(const std::string &sceneUuid);

// Shows or hides one item. False if it can't be found any more.
bool setItemVisible(const std::string &sceneUuid, int64_t itemId, bool visible);

// Where the item's visible pixels are on the canvas: measured by drawing the source and looking
// for pixels that aren't see-through, mapped through the item's transform. Falls back to the item's
// bounding box if the source has no picture. Empty if the item can't be found.
RectF measureSpot(const std::string &sceneUuid, int64_t itemId);

// Canvas size, or 0 x 0 if OBS can't say.
void canvasSize(double &width, double &height);

} // namespace laycue
