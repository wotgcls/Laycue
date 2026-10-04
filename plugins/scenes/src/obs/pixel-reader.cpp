#include "obs/pixel-reader.hpp"

#include <obs.hpp>
#include <graphics/graphics.h>
#include <graphics/image-file.h>
#include <graphics/vec4.h>
#include <plugin-support.h>

#include <algorithm>
#include <cstring>

namespace laycue {

namespace {

constexpr uint32_t kMaxSide = 8192; // refuse absurd sizes rather than allocate gigabytes

struct GraphicsGuard {
	GraphicsGuard() { obs_enter_graphics(); }
	~GraphicsGuard() { obs_leave_graphics(); }
	GraphicsGuard(const GraphicsGuard &) = delete;
	GraphicsGuard &operator=(const GraphicsGuard &) = delete;
};

// Copies rows of 4-byte pixels into `out`, swapping red and blue if the source is BGRA.
void copyPixels(const uint8_t *data, uint32_t linesize, int width, int height, bool bgra, bool opaque, RgbaImage &out)
{
	out.width = width;
	out.height = height;
	out.pixels.resize(static_cast<size_t>(width) * height * 4);
	for (int y = 0; y < height; ++y) {
		const uint8_t *src = data + static_cast<size_t>(y) * linesize;
		uint8_t *dst = out.pixels.data() + static_cast<size_t>(y) * width * 4;
		std::memcpy(dst, src, static_cast<size_t>(width) * 4);
		for (int x = 0; x < width; ++x) {
			uint8_t *p = dst + x * 4;
			if (bgra)
				std::swap(p[0], p[2]);
			if (opaque)
				p[3] = 255;
		}
	}
}

struct CaptureJob {
	std::string uuid;
	RgbaImage result;
};

void renderSource(CaptureJob &job)
{
	OBSSourceAutoRelease source = obs_get_source_by_uuid(job.uuid.c_str());
	if (!source)
		return;
	const uint32_t width = obs_source_get_width(source);
	const uint32_t height = obs_source_get_height(source);
	if (width == 0 || height == 0 || width > kMaxSide || height > kMaxSide)
		return;

	gs_texrender_t *texrender = gs_texrender_create(GS_RGBA, GS_ZS_NONE);
	gs_stagesurf_t *stage = gs_stagesurface_create(width, height, GS_RGBA);
	if (texrender && stage && gs_texrender_begin(texrender, width, height)) {
		vec4 clear;
		vec4_zero(&clear); // fully transparent, so see-through areas stay see-through
		gs_clear(GS_CLEAR_COLOR, &clear, 0.0f, 0);
		gs_ortho(0.0f, static_cast<float>(width), 0.0f, static_cast<float>(height), -100.0f, 100.0f);
		gs_blend_state_push();
		gs_blend_function_separate(GS_BLEND_SRCALPHA, GS_BLEND_INVSRCALPHA, GS_BLEND_ONE, GS_BLEND_INVSRCALPHA);
		obs_source_video_render(source); // includes the source's filters
		gs_blend_state_pop();
		gs_texrender_end(texrender);

		gs_stage_texture(stage, gs_texrender_get_texture(texrender));
		uint8_t *data = nullptr;
		uint32_t linesize = 0;
		if (gs_stagesurface_map(stage, &data, &linesize) && data) {
			copyPixels(data, linesize, static_cast<int>(width), static_cast<int>(height), false, false,
				   job.result);
			gs_stagesurface_unmap(stage);
		}
	}
	if (stage)
		gs_stagesurface_destroy(stage);
	if (texrender)
		gs_texrender_destroy(texrender);
}

void captureTask(void *param)
{
	auto *job = static_cast<CaptureJob *>(param);
	try {
		GraphicsGuard graphics;
		renderSource(*job);
	} catch (...) {
		obs_log(LOG_ERROR, "Could not read the source's picture");
	}
}

} // namespace

RgbaImage loadImageFile(const std::string &path)
{
	RgbaImage out;
	gs_image_file_t image = {};
	gs_image_file_init(&image, path.c_str()); // decodes on the CPU; no graphics needed
	if (image.loaded && image.texture_data && image.cx > 0 && image.cy > 0 && image.cx <= kMaxSide &&
	    image.cy <= kMaxSide) {
		const bool bgra = image.format == GS_BGRA || image.format == GS_BGRX;
		const bool rgba = image.format == GS_RGBA;
		if (bgra || rgba)
			copyPixels(image.texture_data, image.cx * 4, static_cast<int>(image.cx),
				   static_cast<int>(image.cy), bgra, image.format == GS_BGRX, out);
		else
			obs_log(LOG_WARNING, "Unsupported image format %d in %s", static_cast<int>(image.format),
				path.c_str());
	}
	GraphicsGuard graphics;
	gs_image_file_free(&image);
	return out;
}

RgbaImage captureSource(const std::string &sourceUuid)
{
	CaptureJob job;
	job.uuid = sourceUuid;
	obs_queue_task(OBS_TASK_GRAPHICS, captureTask, &job, true); // waits for the result
	return job.result;
}

std::vector<VideoSourceInfo> listVideoSources()
{
	std::vector<VideoSourceInfo> sources;
	obs_enum_sources(
		[](void *param, obs_source_t *source) {
			auto *list = static_cast<std::vector<VideoSourceInfo> *>(param);
			if (!source || obs_source_is_group(source) ||
			    !(obs_source_get_output_flags(source) & OBS_SOURCE_VIDEO))
				return true;
			const char *uuid = obs_source_get_uuid(source);
			const char *name = obs_source_get_name(source);
			if (uuid && name)
				list->push_back({uuid, name});
			return true;
		},
		&sources);
	std::sort(sources.begin(), sources.end(),
		  [](const VideoSourceInfo &a, const VideoSourceInfo &b) { return a.name < b.name; });
	return sources;
}

} // namespace laycue
