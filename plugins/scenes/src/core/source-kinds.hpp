// Friendly categories for OBS source types. Pure logic: no libobs, no Qt.
#pragma once

#include <string>
#include <string_view>

namespace laycue {

enum class SourceKind {
	Camera,
	ScreenCapture,
	WindowCapture,
	GameCapture,
	Image,
	Slideshow,
	Media,
	Browser,
	Text,
	Color,
	Ndi,
	Audio,
	Scene,
	Group,
	Other,
};

// Removes a trailing version suffix such as "_v2" or "_v3" from an OBS source type id,
// so "text_gdiplus_v3" and "text_gdiplus" are treated the same.
std::string stripVersionSuffix(std::string_view typeId);

// Maps an OBS source type id (obs_source_get_id) to a friendly category.
SourceKind kindFromTypeId(std::string_view typeId);

// Key in data/locale/en-US.ini for the category's display name, e.g. "Laycue.Kind.Camera".
const char *kindLocaleKey(SourceKind kind);

} // namespace laycue
