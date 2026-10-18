// The Lyrics templates, in three packs that match the Scripture ones (Classic, Modern, Sanctuary): a solid black or
// white band, a faded band or the words alone; a side rule and a clean light band; a night sky and parchment. Each
// can be changed by the operator; "Reset" goes back to these. Scripture has its own set. Pure C++17.
#pragma once

#include "slides/slide-style.hpp"

#include <vector>

namespace laycue::lyrics {

struct LyricsTemplate {
	const char *nameKey; // locale key
	int pack;            // 0 Classic, 1 Modern, 2 Sanctuary
	slides::SlideStyle style;
};

constexpr int kLyricsPackCount = 3;
const char *lyricsPackKey(int pack); // locale key of the pack's name

const std::vector<LyricsTemplate> &lyricsTemplates();

// The templates of one pack, as indexes into lyricsTemplates(), in order.
std::vector<int> templatesOfPack(int pack);

} // namespace laycue::lyrics
