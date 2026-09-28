#include "ui/labels.hpp"

#include "core/autotag.hpp"

#include <obs-module.h>

#include <algorithm>

namespace laycue::ui {

QString lt(const char *key)
{
	return QString::fromUtf8(obs_module_text(key));
}

QString tagDisplayName(const std::string &tagId)
{
	const auto &builtIn = builtInTags();
	if (std::find(builtIn.begin(), builtIn.end(), tagId) != builtIn.end()) {
		const std::string key = "Laycue.TagName." + tagId;
		return lt(key.c_str());
	}
	return QString::fromStdString(tagId);
}

QString kindDisplayName(SourceKind kind)
{
	return lt(kindLocaleKey(kind));
}

} // namespace laycue::ui
