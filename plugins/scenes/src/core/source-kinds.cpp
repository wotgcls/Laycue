#include "core/source-kinds.hpp"

#include <cctype>
#include <string_view>

namespace laycue {

namespace {

struct KindEntry {
	std::string_view typeId;
	SourceKind kind;
};

// Type ids without version suffixes. Covers the built-in sources on Windows, macOS and Linux.
constexpr KindEntry kKnownKinds[] = {
	// Cameras and capture cards
	{"dshow_input", SourceKind::Camera},
	{"av_capture_input", SourceKind::Camera},
	{"macos-avcapture", SourceKind::Camera},
	{"macos-avcapture-fast", SourceKind::Camera},
	{"v4l2_input", SourceKind::Camera},
	{"decklink-input", SourceKind::Camera},
	// Screen, window and game capture
	{"monitor_capture", SourceKind::ScreenCapture},
	{"display_capture", SourceKind::ScreenCapture},
	{"screen_capture", SourceKind::ScreenCapture},
	{"xshm_input", SourceKind::ScreenCapture},
	{"pipewire-desktop-capture-source", SourceKind::ScreenCapture},
	{"pipewire-screen-capture-source", SourceKind::ScreenCapture},
	{"window_capture", SourceKind::WindowCapture},
	{"xcomposite_input", SourceKind::WindowCapture},
	{"pipewire-window-capture-source", SourceKind::WindowCapture},
	{"game_capture", SourceKind::GameCapture},
	// Stills and media
	{"image_source", SourceKind::Image},
	{"slideshow", SourceKind::Slideshow},
	{"ffmpeg_source", SourceKind::Media},
	{"vlc_source", SourceKind::Media},
	{"browser_source", SourceKind::Browser},
	// Text and colour
	{"text_gdiplus", SourceKind::Text},
	{"text_ft2_source", SourceKind::Text},
	{"color_source", SourceKind::Color},
	// Network video
	{"ndi_source", SourceKind::Ndi},
	// Audio
	{"wasapi_input_capture", SourceKind::Audio},
	{"wasapi_output_capture", SourceKind::Audio},
	{"wasapi_process_output_capture", SourceKind::Audio},
	{"coreaudio_input_capture", SourceKind::Audio},
	{"coreaudio_output_capture", SourceKind::Audio},
	{"sck_audio_capture", SourceKind::Audio},
	{"pulse_input_capture", SourceKind::Audio},
	{"pulse_output_capture", SourceKind::Audio},
	{"alsa_input_capture", SourceKind::Audio},
	{"jack_output_capture", SourceKind::Audio},
	// Containers
	{"scene", SourceKind::Scene},
	{"group", SourceKind::Group},
};

} // namespace

std::string stripVersionSuffix(std::string_view typeId)
{
	// Look for "_v" followed by one or more digits at the very end.
	const size_t pos = typeId.rfind("_v");
	if (pos == std::string_view::npos || pos + 2 >= typeId.size())
		return std::string(typeId);

	for (size_t i = pos + 2; i < typeId.size(); ++i) {
		if (!std::isdigit(static_cast<unsigned char>(typeId[i])))
			return std::string(typeId);
	}
	return std::string(typeId.substr(0, pos));
}

SourceKind kindFromTypeId(std::string_view typeId)
{
	const std::string base = stripVersionSuffix(typeId);
	for (const KindEntry &entry : kKnownKinds) {
		if (entry.typeId == base)
			return entry.kind;
	}
	return SourceKind::Other;
}

const char *kindLocaleKey(SourceKind kind)
{
	switch (kind) {
	case SourceKind::Camera:
		return "Laycue.Kind.Camera";
	case SourceKind::ScreenCapture:
		return "Laycue.Kind.ScreenCapture";
	case SourceKind::WindowCapture:
		return "Laycue.Kind.WindowCapture";
	case SourceKind::GameCapture:
		return "Laycue.Kind.GameCapture";
	case SourceKind::Image:
		return "Laycue.Kind.Image";
	case SourceKind::Slideshow:
		return "Laycue.Kind.Slideshow";
	case SourceKind::Media:
		return "Laycue.Kind.Media";
	case SourceKind::Browser:
		return "Laycue.Kind.Browser";
	case SourceKind::Text:
		return "Laycue.Kind.Text";
	case SourceKind::Color:
		return "Laycue.Kind.Color";
	case SourceKind::Ndi:
		return "Laycue.Kind.Ndi";
	case SourceKind::Audio:
		return "Laycue.Kind.Audio";
	case SourceKind::Scene:
		return "Laycue.Kind.Scene";
	case SourceKind::Group:
		return "Laycue.Kind.Group";
	case SourceKind::Other:
		return "Laycue.Kind.Other";
	}
	return "Laycue.Kind.Other";
}

} // namespace laycue
