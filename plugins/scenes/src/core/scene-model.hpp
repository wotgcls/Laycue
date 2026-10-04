// A plain snapshot of OBS's scenes, read by obs/scene-reader and shown by the dock.
// Pure data: no libobs, no Qt.
#pragma once

#include "core/source-kinds.hpp"

#include <cstdint>
#include <string>
#include <vector>

namespace laycue {

struct ItemInfo {
	int64_t id = 0; // obs_sceneitem_get_id, unique within its scene
	std::string sourceName;
	std::string sourceUuid;
	std::string typeId; // obs_source_get_id, e.g. "dshow_input"
	SourceKind kind = SourceKind::Other;
	bool visible = true;
	std::vector<ItemInfo> children; // only filled for groups, top of the Sources list first
};

struct SceneInfo {
	std::string name;
	std::string uuid;
	std::vector<ItemInfo> items; // top of the Sources list first, like OBS shows it
};

struct Snapshot {
	uint32_t canvasWidth = 0;
	uint32_t canvasHeight = 0;
	bool studioMode = false;
	std::string programSceneUuid;
	std::string previewSceneUuid;  // empty unless studio mode is on
	std::vector<SceneInfo> scenes; // in OBS's scene list order
};

// Total number of scene items, including items inside groups.
size_t countItems(const std::vector<ItemInfo> &items);

} // namespace laycue
