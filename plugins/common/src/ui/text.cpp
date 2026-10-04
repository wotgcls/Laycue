#include "ui/text.hpp"

#include <obs-module.h>

namespace laycue::ui {

QString lt(const char *key)
{
	return QString::fromUtf8(obs_module_text(key));
}

} // namespace laycue::ui
