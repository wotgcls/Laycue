// The Lyrics templates: a solid black or white band, a faded (see-through) band, or the words alone. Each can be
// changed by the operator; "Reset" goes back to these. Scripture has its own set. Pure C++17.
#pragma once

#include "slides/slide-style.hpp"

#include <vector>

namespace laycue::lyrics {

struct LyricsTemplate {
	const char *nameKey; // locale key
	slides::SlideStyle style;
};

const std::vector<LyricsTemplate> &lyricsTemplates();

} // namespace laycue::lyrics
