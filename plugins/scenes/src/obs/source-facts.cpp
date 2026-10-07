#include "obs/source-facts.hpp"

#include "obs/pixel-reader.hpp"

#include <obs.hpp>
#include <plugin-support.h>

#include <algorithm>
#include <cstring>

namespace laycue {

namespace {

bool isKeyFilterId(const char *id)
{
	return id && (std::strstr(id, "chroma_key") || std::strstr(id, "color_key"));
}

} // namespace

PictureFacts readPictureFacts(const std::string &sourceUuid, bool measure)
{
	PictureFacts facts;
	try {
		OBSSourceAutoRelease source = obs_get_source_by_uuid(sourceUuid.c_str());
		if (!source)
			return facts;
		if (const char *id = obs_source_get_id(source))
			facts.typeId = id;
		obs_source_enum_filters(
			source,
			[](obs_source_t *, obs_source_t *filter, void *param) {
				if (isKeyFilterId(obs_source_get_id(filter)))
					*static_cast<bool *>(param) = true;
			},
			&facts.keyFilter);
		if (!measure)
			return facts;

		const RgbaImage image = captureSource(sourceUuid);
		if (image.empty())
			return facts;
		// Look at a sample of the pixels, not all of them: a few thousand are plenty for a share.
		const long total = long(image.width) * long(image.height);
		const long step = std::max(1L, total / 40000L);
		long seen = 0, clear = 0, visible = 0, green = 0;
		for (long i = 0; i < total; i += step) {
			const uint8_t *p = &image.pixels[size_t(i) * 4];
			++seen;
			if (p[3] < 16) {
				++clear;
				continue;
			}
			++visible;
			if (int(p[1]) > int(p[0]) + 60 && int(p[1]) > int(p[2]) + 60)
				++green;
		}
		if (seen > 0)
			facts.seeThrough = double(clear) / double(seen);
		if (visible > 0)
			facts.green = double(green) / double(visible);
	} catch (...) {
		obs_log(LOG_ERROR, "Laycue: could not look at a source's picture");
	}
	return facts;
}

} // namespace laycue
