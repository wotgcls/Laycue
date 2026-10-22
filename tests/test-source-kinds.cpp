#include "check.hpp"

#include "core/scene-model.hpp"
#include "core/source-kinds.hpp"

#include <string>

using laycue::SourceKind;

TEST_CASE("stripVersionSuffix removes a trailing _v<number>")
{
	CHECK_EQ(laycue::stripVersionSuffix("text_gdiplus_v3"), std::string("text_gdiplus"));
	CHECK_EQ(laycue::stripVersionSuffix("color_source_v12"), std::string("color_source"));
	CHECK_EQ(laycue::stripVersionSuffix("image_source"), std::string("image_source"));
}

TEST_CASE("stripVersionSuffix leaves ids alone when the suffix is not a version")
{
	CHECK_EQ(laycue::stripVersionSuffix("my_video"), std::string("my_video"));
	CHECK_EQ(laycue::stripVersionSuffix("source_v"), std::string("source_v"));
	CHECK_EQ(laycue::stripVersionSuffix("source_v2a"), std::string("source_v2a"));
	CHECK_EQ(laycue::stripVersionSuffix(""), std::string(""));
}

TEST_CASE("cameras are recognised on every platform")
{
	CHECK(laycue::kindFromTypeId("dshow_input") == SourceKind::Camera);
	CHECK(laycue::kindFromTypeId("av_capture_input_v2") == SourceKind::Camera);
	CHECK(laycue::kindFromTypeId("macos-avcapture") == SourceKind::Camera);
	CHECK(laycue::kindFromTypeId("v4l2_input") == SourceKind::Camera);
	CHECK(laycue::kindFromTypeId("decklink-input") == SourceKind::Camera);
}

TEST_CASE("versioned source types map to the same kind as the original")
{
	CHECK(laycue::kindFromTypeId("text_gdiplus") == SourceKind::Text);
	CHECK(laycue::kindFromTypeId("text_gdiplus_v3") == SourceKind::Text);
	CHECK(laycue::kindFromTypeId("color_source_v3") == SourceKind::Color);
	CHECK(laycue::kindFromTypeId("slideshow_v2") == SourceKind::Slideshow);
}

TEST_CASE("images, media, scenes and groups are recognised")
{
	CHECK(laycue::kindFromTypeId("image_source") == SourceKind::Image);
	CHECK(laycue::kindFromTypeId("ffmpeg_source") == SourceKind::Media);
	CHECK(laycue::kindFromTypeId("browser_source") == SourceKind::Browser);
	CHECK(laycue::kindFromTypeId("scene") == SourceKind::Scene);
	CHECK(laycue::kindFromTypeId("group") == SourceKind::Group);
}

TEST_CASE("unknown source types are Other")
{
	CHECK(laycue::kindFromTypeId("some_third_party_source") == SourceKind::Other);
	CHECK(laycue::kindFromTypeId("") == SourceKind::Other);
}

TEST_CASE("every kind has a locale key")
{
	const SourceKind all[] = {SourceKind::Camera,      SourceKind::ScreenCapture, SourceKind::WindowCapture,
				  SourceKind::GameCapture, SourceKind::Image,         SourceKind::Slideshow,
				  SourceKind::Media,       SourceKind::Browser,       SourceKind::Text,
				  SourceKind::Color,       SourceKind::Ndi,           SourceKind::Audio,
				  SourceKind::Scene,       SourceKind::Group,         SourceKind::Other};
	for (SourceKind kind : all) {
		const std::string key = laycue::kindLocaleKey(kind);
		CHECK(key.rfind("Laycue.Kind.", 0) == 0);
	}
}

TEST_CASE("countItems includes items inside groups")
{
	laycue::ItemInfo group;
	group.children.resize(3);
	std::vector<laycue::ItemInfo> items(2);
	items.push_back(group);

	CHECK_EQ(laycue::countItems(items), size_t(6));
	CHECK_EQ(laycue::countItems({}), size_t(0));
}
