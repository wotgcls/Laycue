// Keeps small pictures of the live output and of scenes fresh, for the panel's
// Live / Up next pictures and shot thumbnails.
//
// Rendering happens on OBS's graphics thread (via obs_queue_task); the UI thread only
// says which pictures it wants and picks up the newest ones. Nothing here uses Qt.
#pragma once

#include <atomic>
#include <chrono>
#include <cstdint>
#include <map>
#include <mutex>
#include <string>
#include <vector>

struct gs_texture_render;
struct gs_stage_surface;

namespace laycue {

// A picture as tightly packed RGBX rows (4 bytes per pixel; alpha is ignored).
struct Picture {
	uint32_t width = 0;
	uint32_t height = 0;
	uint64_t serial = 0; // increases every time the picture is refreshed
	std::vector<uint8_t> pixels;
};

struct PictureTarget {
	std::string key; // FrameGrabber::kProgram, or a scene's UUID
	uint32_t width;  // picture size in pixels
	uint32_t height;
	int intervalMs; // how often to refresh it
};

class FrameGrabber {
public:
	static constexpr const char *kProgram = "laycue:program";

	FrameGrabber() = default;
	~FrameGrabber();

	FrameGrabber(const FrameGrabber &) = delete;
	FrameGrabber &operator=(const FrameGrabber &) = delete;

	// UI thread: which pictures to keep fresh. Pictures no longer listed are dropped.
	void setTargets(std::vector<PictureTarget> targets);

	// UI thread: ask the graphics thread to refresh whatever is due. Cheap to call often;
	// does nothing while a previous request is still running.
	void requestFrame();

	// UI thread: copy of the newest picture for `key` (empty if none yet).
	// Returns false if nothing newer than `knownSerial` exists.
	bool takeIfNewer(const std::string &key, uint64_t knownSerial, Picture &out) const;

	// UI thread: stop and free GPU objects. Waits for graphics work in flight.
	// Safe to call more than once. Must be called while OBS is still running.
	void shutdown();

private:
	struct Slot {
		gs_texture_render *texrender = nullptr;
		gs_stage_surface *stage = nullptr;
		uint32_t width = 0;
		uint32_t height = 0;
	};

	static void grabTask(void *param);
	static void destroyTask(void *param);
	void grab();                                          // graphics thread
	void destroySlots();                                  // graphics thread
	static void freeSlot(Slot &slot);                     // graphics thread
	bool render(Slot &slot, const PictureTarget &target); // graphics thread

	mutable std::mutex mutex_;
	std::vector<PictureTarget> targets_;
	std::map<std::string, std::chrono::steady_clock::time_point> lastGrab_;
	std::map<std::string, Picture> pictures_;
	uint64_t nextSerial_ = 1;

	std::map<std::string, Slot> slots_; // graphics thread only
	std::atomic<bool> busy_{false};
	std::atomic<bool> stopped_{false};
};

} // namespace laycue
