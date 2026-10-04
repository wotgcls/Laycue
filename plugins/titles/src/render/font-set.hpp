// Loads the fonts bundled with the plugin and hands out QFont objects for them by FontFace.
// Qt only: no libobs, so the same code runs in the preview tool and inside OBS.
#pragma once

#include "core/title-templates.hpp"

#include <QFont>
#include <QString>

#include <array>

namespace laycue::titles {

class FontSet {
public:
	// `folder` is the plugin's data/fonts directory. Missing files are skipped: text then falls back
	// to the system font, which is ugly but never fails.
	explicit FontSet(const QString &folder);

	QFont font(FontFace face, double pixelSize) const;
	int loadedCount() const { return loaded_; }

private:
	struct Entry {
		QString family;
		QString style;
	};
	std::array<Entry, 8> entries_;
	int loaded_ = 0;
};

// The one FontSet of this plugin, set when the module loads. Null before that and after unload.
const FontSet *sharedFonts();
void setSharedFonts(const FontSet *fonts);

} // namespace laycue::titles
