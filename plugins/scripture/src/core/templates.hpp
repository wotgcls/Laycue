// The Scripture templates: where verses start from, in four packs that each suit a theme (Classic, Modern,
// Sanctuary, Living Word: lower thirds that move). Each can be changed by the operator; "Reset" goes back to these.
// Their own set: songs have different ones (lyrics/src/core/templates). Pure C++17.
#pragma once

#include "slides/slide-style.hpp"

#include <vector>

namespace laycue::scripture {

struct ScriptureTemplate {
	const char *nameKey; // locale key
	int pack;            // 0 Classic, 1 Modern, 2 Sanctuary, 3 Living Word
	slides::SlideStyle style;
};

constexpr int kScripturePackCount = 4;
const char *scripturePackKey(int pack); // locale key of the pack's name

const std::vector<ScriptureTemplate> &scriptureTemplates();

// The templates of one pack, as indexes into scriptureTemplates(), in order.
std::vector<int> templatesOfPack(int pack);

} // namespace laycue::scripture
