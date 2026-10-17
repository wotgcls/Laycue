// The Scripture templates: where verses start from. Each can be changed by the operator; "Reset" goes back to
// these. Their own set: songs have different ones (lyrics/src/core/templates). Pure C++17.
#pragma once

#include "slides/slide-style.hpp"

#include <vector>

namespace laycue::scripture {

struct ScriptureTemplate {
	const char *nameKey; // locale key
	slides::SlideStyle style;
};

const std::vector<ScriptureTemplate> &scriptureTemplates();

} // namespace laycue::scripture
