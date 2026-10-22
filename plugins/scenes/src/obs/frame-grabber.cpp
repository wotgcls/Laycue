#include "obs/frame-grabber.hpp"

#include <obs.hpp>
#include <graphics/graphics.h>
#include <graphics/vec4.h>
#include <plugin-support.h>

#include <cstring>

namespace laycue {

namespace {

// Enters OBS's graphics context for the lifetime of the guard, and always leaves it again,
// even if the work inside throws. Entering is safe when the context is already entered.
struct GraphicsGuard {
	GraphicsGuard() { obs_enter_graphics(); }
	~GraphicsGuard() { obs_leave_graphics(); }
	GraphicsGuard(const GraphicsGuard &) = delete;
	GraphicsGuard &operator=(const GraphicsGuard &) = delete;
};

} // namespace

FrameGrabber::~FrameGrabber()
{
	shutdown();
}

void FrameGrabber::setTargets(std::vector<PictureTarget> targets)
{
	std::lock_guard<std::mutex> lock(mutex_);
	targets_ = std::move(targets);

	// Forget pictures nobody asked for any more.
	for (auto it = pictures_.begin(); it != pictures_.end();) {
		bool wanted = false;
		for (const PictureTarget &t : targets_)
			wanted = wanted || t.key == it->first;
		it = wanted ? std::next(it) : pictures_.erase(it);
	}
}

void FrameGrabber::requestFrame()
{
	if (stopped_)
		return;
	bool expected = false;
	if (!busy_.compare_exchange_strong(expected, true))
		return; // previous request still running
	obs_queue_task(OBS_TASK_GRAPHICS, grabTask, this, false);
}

bool FrameGrabber::takeIfNewer(const std::string &key, uint64_t knownSerial, Picture &out) const
{
	std::lock_guard<std::mutex> lock(mutex_);
	auto it = pictures_.find(key);
	if (it == pictures_.end() || it->second.serial <= knownSerial)
		return false;
	out = it->second;
	return true;
}

void FrameGrabber::shutdown()
{
	if (stopped_.exchange(true))
		return;
	{
		std::lock_guard<std::mutex> lock(mutex_);
		targets_.clear();
	}
	// Runs after every grab already queued, so nothing touches `this` afterwards.
	obs_queue_task(OBS_TASK_GRAPHICS, destroyTask, this, true);
}

void FrameGrabber::grabTask(void *param)
{
	auto *self = static_cast<FrameGrabber *>(param);
	try {
		if (!self->stopped_) {
			// Graphics tasks don't always run with the graphics context entered.
			GraphicsGuard graphics;
			self->grab();
		}
	} catch (...) {
		obs_log(LOG_ERROR, "FrameGrabber: error while grabbing pictures");
	}
	self->busy_ = false;
}

void FrameGrabber::destroyTask(void *param)
{
	try {
		GraphicsGuard graphics;
		static_cast<FrameGrabber *>(param)->destroySlots();
	} catch (...) {
		obs_log(LOG_ERROR, "FrameGrabber: error while freeing pictures");
	}
}

void FrameGrabber::freeSlot(Slot &slot)
{
	if (slot.texrender)
		gs_texrender_destroy(slot.texrender);
	if (slot.stage)
		gs_stagesurface_destroy(slot.stage);
	slot = Slot();
}

void FrameGrabber::destroySlots()
{
	for (auto &entry : slots_)
		freeSlot(entry.second);
	slots_.clear();
}

void FrameGrabber::grab()
{

	const auto now = std::chrono::steady_clock::now();

	std::vector<PictureTarget> due;
	{
		std::lock_guard<std::mutex> lock(mutex_);

		// Free GPU objects for pictures nobody wants any more (for example, a deleted scene).
		for (auto it = slots_.begin(); it != slots_.end();) {
			bool wanted = false;
			for (const PictureTarget &t : targets_)
				wanted = wanted || t.key == it->first;
			if (wanted) {
				++it;
			} else {
				freeSlot(it->second);
				it = slots_.erase(it);
			}
		}

		for (const PictureTarget &t : targets_) {
			auto last = lastGrab_.find(t.key);
			if (last == lastGrab_.end() || now - last->second >= std::chrono::milliseconds(t.intervalMs))
				due.push_back(t);
		}
	}

	for (const PictureTarget &target : due) {
		if (target.width == 0 || target.height == 0)
			continue;

		Slot &slot = slots_[target.key];
		if (slot.width != target.width || slot.height != target.height) {
			freeSlot(slot);
			slot.texrender = gs_texrender_create(GS_RGBA, GS_ZS_NONE);
			slot.stage = gs_stagesurface_create(target.width, target.height, GS_RGBA);
			slot.width = target.width;
			slot.height = target.height;
		}
		if (!slot.texrender || !slot.stage)
			continue;

		render(slot, target);
		std::lock_guard<std::mutex> lock(mutex_);
		lastGrab_[target.key] = now;
	}
}

bool FrameGrabber::render(Slot &slot, const PictureTarget &target)
{
	obs_video_info video = {};
	if (!obs_get_video_info(&video) || video.base_width == 0 || video.base_height == 0)
		return false;

	const bool program = target.key == kProgram;
	OBSSourceAutoRelease scene;
	if (!program) {
		scene = obs_get_source_by_uuid(target.key.c_str());
		if (!scene)
			return false; // scene was deleted; the UI will drop it on its next refresh
	}

	gs_texrender_reset(slot.texrender);
	if (!gs_texrender_begin(slot.texrender, slot.width, slot.height))
		return false;

	vec4 black;
	vec4_set(&black, 0.0f, 0.0f, 0.0f, 1.0f);
	gs_clear(GS_CLEAR_COLOR, &black, 0.0f, 0);
	gs_ortho(0.0f, static_cast<float>(video.base_width), 0.0f, static_cast<float>(video.base_height), -100.0f,
		 100.0f);

	gs_blend_state_push();
	gs_blend_function(GS_BLEND_ONE, GS_BLEND_INVSRCALPHA);
	if (program)
		obs_render_main_texture(); // the last frame sent to the stream
	else
		obs_source_video_render(scene);
	gs_blend_state_pop();
	gs_texrender_end(slot.texrender);

	gs_stage_texture(slot.stage, gs_texrender_get_texture(slot.texrender));

	uint8_t *data = nullptr;
	uint32_t linesize = 0;
	if (!gs_stagesurface_map(slot.stage, &data, &linesize) || !data)
		return false;

	Picture picture;
	picture.width = slot.width;
	picture.height = slot.height;
	const size_t rowBytes = static_cast<size_t>(slot.width) * 4;
	picture.pixels.resize(rowBytes * slot.height);
	for (uint32_t y = 0; y < slot.height; ++y)
		std::memcpy(picture.pixels.data() + rowBytes * y, data + static_cast<size_t>(linesize) * y, rowBytes);
	gs_stagesurface_unmap(slot.stage);

	std::lock_guard<std::mutex> lock(mutex_);
	picture.serial = nextSerial_++;
	pictures_[target.key] = std::move(picture);
	return true;
}

} // namespace laycue
