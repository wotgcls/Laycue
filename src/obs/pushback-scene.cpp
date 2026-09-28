#include "obs/pushback-scene.hpp"

#include "core/detect.hpp"
#include "core/geometry.hpp"
#include "obs/overlay-items.hpp"
#include "obs/pixel-reader.hpp"
#include "obs/scene-util.hpp"

#include <obs-frontend-api.h>
#include <obs.hpp>
#include <plugin-support.h>

#include <algorithm>
#include <cmath>

namespace laycue {

namespace {

constexpr const char *kKeyName = "Laycue key";
constexpr const char *kFadeName = "Laycue fade";

// The pushback's scene, or null. The returned reference is released by the caller (RAII).
OBSSourceAutoRelease openScene(const Pushback &pushback)
{
	OBSSourceAutoRelease scene = obs_get_source_by_uuid(pushback.sceneUuid.c_str());
	if (!scene && !pushback.sceneName.empty())
		scene = obs_get_source_by_name(pushback.sceneName.c_str());
	if (scene && !obs_scene_from_source(scene))
		return OBSSourceAutoRelease(nullptr);
	return scene;
}

RectF windowOnCanvasFor(const Pushback &pushback, const Canvas &canvas, RectF &imageBox)
{
	imageBox = imagePlacementOnCanvas(pushback.imageWidth, pushback.imageHeight, canvas.w, canvas.h);
	return windowOnCanvas(pushback.window, pushback.imageWidth, pushback.imageHeight, imageBox);
}

bool isFeed(const Pushback &pushback, const std::string &uuid)
{
	return std::any_of(pushback.feeds.begin(), pushback.feeds.end(), [&](const Feed &f) { return f.uuid == uuid; });
}

struct StaleCollector {
	const Pushback *pushback;
	std::vector<obs_sceneitem_t *> stale;
};

bool collectStale(obs_scene_t *, obs_sceneitem_t *item, void *param)
{
	auto *collector = static_cast<StaleCollector *>(param);
	obs_source_t *source = obs_sceneitem_get_source(item);
	const std::string uuid = str(source ? obs_source_get_uuid(source) : nullptr);
	if (uuid != collector->pushback->imageUuid && !isFeed(*collector->pushback, uuid))
		collector->stale.push_back(item);
	return true;
}

int itemCount(obs_scene_t *scene)
{
	int count = 0;
	obs_scene_enum_items(
		scene,
		[](obs_scene_t *, obs_sceneitem_t *, void *param) {
			++*static_cast<int *>(param);
			return true;
		},
		&count);
	return count;
}

} // namespace

bool layoutPushback(const Pushback &pushback, bool applyVisibility)
{
	OBSSourceAutoRelease sceneSource = openScene(pushback);
	if (!sceneSource)
		return false;
	obs_scene_t *scene = obs_scene_from_source(sceneSource);
	const Canvas canvas = readCanvas();
	RectF imageBox;
	const RectF window = windowOnCanvasFor(pushback, canvas, imageBox);

	// Feeds, underneath.
	for (const Feed &feed : pushback.feeds) {
		OBSSourceAutoRelease source = obs_get_source_by_uuid(feed.uuid.c_str());
		if (!source)
			continue;
		obs_sceneitem_t *item = itemForSource(scene, feed.uuid);
		if (!item)
			item = obs_scene_add(scene, source);
		if (!item)
			continue;
		double w = 0, h = 0;
		sourceSize(source, canvas, w, h);
		const Placement placement = pushback.fit == FitMode::Whole ? fitInsideWindow(w, h, window)
									   : fillWindow(w, h, window);
		placeItem(item, placement.box, placement.crop);
		if (applyVisibility)
			obs_sceneitem_set_visible(item, feed.uuid == pushback.activeFeed);
	}

	// The picture, on top of everything.
	OBSSourceAutoRelease image = obs_get_source_by_uuid(pushback.imageUuid.c_str());
	if (image) {
		obs_sceneitem_t *item = itemForSource(scene, pushback.imageUuid);
		if (!item)
			item = obs_scene_add(scene, image);
		if (item) {
			placeItem(item, imageBox, Crop{});
			obs_sceneitem_set_visible(item, true);
			obs_sceneitem_set_order(item, OBS_ORDER_MOVE_TOP);
		}
	}

	// Anything else in Laycue's scene (a feed taken out of the list) goes.
	StaleCollector stale{&pushback, {}};
	obs_scene_enum_items(scene, collectStale, &stale);
	for (obs_sceneitem_t *item : stale.stale)
		obs_sceneitem_remove(item);
	return true;
}

BuildResult buildPushbackScene(const Pushback &input)
{
	BuildResult result;
	result.pushback = input;
	Pushback &pushback = result.pushback;

	OBSSourceAutoRelease image = obs_get_source_by_uuid(pushback.imageUuid.c_str());
	if (!image) {
		result.error = "The pushback picture is missing.";
		return result;
	}

	OBSSourceAutoRelease sceneSource = openScene(pushback);
	if (!sceneSource) {
		const std::string name = freeSourceName("Laycue · " + pushback.name);
		obs_scene_t *created = obs_scene_create(name.c_str());
		if (!created) {
			result.error = "OBS couldn't make a scene for this pushback.";
			return result;
		}
		sceneSource = OBSSourceAutoRelease(obs_source_get_ref(obs_scene_get_source(created)));
		obs_scene_release(created);
	}
	pushback.sceneUuid = str(obs_source_get_uuid(sceneSource));
	pushback.sceneName = str(obs_source_get_name(sceneSource));

	if (pushback.activeFeed.empty() && !pushback.feeds.empty())
		pushback.activeFeed = pushback.feeds.front().uuid;
	result.ok = layoutPushback(pushback);
	if (!result.ok)
		result.error = "OBS couldn't set up the pushback scene.";
	return result;
}

bool showFeed(const Pushback &pushback, const std::string &feedUuid, bool fade, int fadeMs)
{
	OBSSourceAutoRelease sceneSource = openScene(pushback);
	if (!sceneSource)
		return false;
	obs_scene_t *scene = obs_scene_from_source(sceneSource);
	obs_sceneitem_t *item = itemForSource(scene, feedUuid);
	if (!item)
		return false;

	if (fade && fadeMs > 0) {
		// The new feed goes just under the picture, above the old feed, and fades in.
		const int count = itemCount(scene);
		obs_sceneitem_set_order_position(item, std::max(0, count - 2));
		OBSSourceAutoRelease fadeTransition = obs_source_create_private("fade_transition", kFadeName, nullptr);
		if (fadeTransition) {
			obs_sceneitem_set_transition(item, true, fadeTransition);
			obs_sceneitem_set_transition_duration(item, true, static_cast<uint32_t>(fadeMs));
		}
		obs_sceneitem_set_visible(item, false); // start from hidden so the fade plays
		obs_sceneitem_set_visible(item, true);
		return true;
	}

	// Instant: show the new feed first, then hide the others, so there's never a gap.
	obs_sceneitem_set_transition_duration(item, true, 0);
	obs_sceneitem_set_visible(item, true);
	for (const Feed &feed : pushback.feeds) {
		if (feed.uuid == feedUuid)
			continue;
		if (obs_sceneitem_t *other = itemForSource(scene, feed.uuid)) {
			obs_sceneitem_set_transition_duration(other, false, 0);
			obs_sceneitem_set_visible(other, false);
		}
	}
	return true;
}

void hideInactiveFeeds(const Pushback &pushback)
{
	OBSSourceAutoRelease sceneSource = openScene(pushback);
	if (!sceneSource)
		return;
	obs_scene_t *scene = obs_scene_from_source(sceneSource);
	for (const Feed &feed : pushback.feeds) {
		if (feed.uuid == pushback.activeFeed)
			continue;
		if (obs_sceneitem_t *item = itemForSource(scene, feed.uuid))
			obs_sceneitem_set_visible(item, false);
	}
}

bool pushbackSceneExists(const Pushback &pushback)
{
	OBSSourceAutoRelease sceneSource = openScene(pushback);
	return sceneSource != nullptr;
}

bool renamePushbackScene(const Pushback &pushback, const std::string &newName)
{
	OBSSourceAutoRelease sceneSource = openScene(pushback);
	if (!sceneSource || str(obs_source_get_name(sceneSource)).rfind("Laycue · ", 0) != 0)
		return false;
	const std::string wanted = "Laycue · " + newName;
	if (str(obs_source_get_name(sceneSource)) == wanted)
		return true;
	obs_source_set_name(sceneSource, freeSourceName(wanted).c_str());
	return true;
}

bool removePushbackScene(const Pushback &pushback)
{
	OBSSourceAutoRelease sceneSource = openScene(pushback);
	if (!sceneSource)
		return false;
	// Only ever a scene Laycue made: its name starts with "Laycue · ".
	if (str(obs_source_get_name(sceneSource)).rfind("Laycue · ", 0) != 0)
		return false;
	obs_source_remove(sceneSource);

	// The picture too, but only if Laycue made it (made from a file, named the same way).
	OBSSourceAutoRelease image = obs_get_source_by_uuid(pushback.imageUuid.c_str());
	if (image && str(obs_source_get_name(image)).rfind("Laycue · ", 0) == 0)
		obs_source_remove(image);
	return true;
}

SourceHold createImageSource(const std::string &name, const std::string &file, std::string &uuid)
{
	uuid.clear();
	OBSDataAutoRelease settings = obs_data_create();
	obs_data_set_string(settings, "file", file.c_str());
	const std::string unique = freeSourceName(name);
	obs_source_t *source = obs_source_create("image_source", unique.c_str(), settings, nullptr);
	if (!source)
		return nullptr;
	uuid = str(obs_source_get_uuid(source));
	// The hold owns the reference obs_source_create gave us.
	return SourceHold(source, [](void *held) { obs_source_release(static_cast<obs_source_t *>(held)); });
}

bool hasLaycueKey(const std::string &sourceUuid)
{
	OBSSourceAutoRelease source = obs_get_source_by_uuid(sourceUuid.c_str());
	if (!source)
		return false;
	OBSSourceAutoRelease filter = obs_source_get_filter_by_name(source, kKeyName);
	return filter != nullptr;
}

bool setChromaKey(const std::string &sourceUuid, const Rgb &key, int similarity)
{
	OBSSourceAutoRelease source = obs_get_source_by_uuid(sourceUuid.c_str());
	if (!source)
		return false;

	OBSDataAutoRelease settings = obs_data_create();
	obs_data_set_string(settings, "key_color_type", "custom");
	// OBS stores colours as 0xAABBGGRR.
	const long long colour = (0xFFLL << 24) | (static_cast<long long>(key.b) << 16) |
				 (static_cast<long long>(key.g) << 8) | key.r;
	obs_data_set_int(settings, "key_color", colour);
	obs_data_set_int(settings, "similarity", similarity);
	obs_data_set_int(settings, "smoothness", 60);
	obs_data_set_int(settings, "spill", 100);

	OBSSourceAutoRelease existing = obs_source_get_filter_by_name(source, kKeyName);
	if (existing) {
		obs_source_update(existing, settings);
		return true;
	}
	OBSSourceAutoRelease filter = obs_source_create_private("chroma_key_filter_v2", kKeyName, settings);
	if (!filter)
		return false;
	obs_source_filter_add(source, filter);
	return true;
}

int tuneChromaKey(const std::string &sourceUuid, const Rgb &key, const Rect &expectedWindow)
{
	// From gentle to strong: stop at the first that clears the window and nothing else.
	static const int candidates[] = {40, 60, 90, 120, 150, 200, 260, 340};
	const double expectedArea = double(expectedWindow.width) * double(expectedWindow.height);
	if (expectedArea <= 0)
		return 0;

	for (int similarity : candidates) {
		if (!setChromaKey(sourceUuid, key, similarity))
			return 0;
		const RgbaImage picture = captureSource(sourceUuid);
		if (picture.empty())
			continue;
		const ImageView view{picture.width, picture.height, picture.width * 4, picture.pixels.data()};
		DetectOptions options;
		options.kind = WindowKind::SeeThrough;
		const DetectResult found = detectWindow(view, options);
		if (!found.found || found.lowConfidence)
			continue;
		const double area = double(found.window.width) * double(found.window.height);
		const double imageArea = double(picture.width) * double(picture.height);
		if (std::fabs(area - expectedArea) / expectedArea < 0.03 && area < 0.95 * imageArea)
			return similarity;
	}

	// Nothing worked: take the key off again rather than leave a filter that damages the picture.
	OBSSourceAutoRelease source = obs_get_source_by_uuid(sourceUuid.c_str());
	if (source) {
		OBSSourceAutoRelease filter = obs_source_get_filter_by_name(source, kKeyName);
		if (filter)
			obs_source_filter_remove(source, filter);
	}
	return 0;
}

} // namespace laycue
