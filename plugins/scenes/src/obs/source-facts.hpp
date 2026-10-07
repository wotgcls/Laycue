// Looks at a source the way Laycue needs to decide whether it is an overlay: what filters it has and what its
// picture is made of (how much is see-through, how much is green). Call on the UI thread; it may wait a few
// milliseconds for OBS's graphics thread.
#pragma once

#include <string>

namespace laycue {

struct PictureFacts {
	std::string typeId;       // OBS's id for this kind of source, e.g. "image_source"
	bool keyFilter = false;   // a Chroma Key or Color Key filter is on it
	double seeThrough = -1.0; // share of the picture that is see-through, filters included; -1 if not measured
	double green = -1.0;      // share of the visible picture that is strongly green; -1 if not measured
};

// `measure`: draw the source and count pixels (skip it for cameras and captures: they are never overlays).
PictureFacts readPictureFacts(const std::string &sourceUuid, bool measure);

} // namespace laycue
