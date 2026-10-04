// "Customize": make your own template from a built-in one. Change colours, fonts, motion and timing
// with a live preview. Only what you change is saved, so the template still follows the original
// design for everything else.
#pragma once

#include "core/library.hpp"
#include "ui/pages/page-frame.hpp"

#include <string>

namespace laycue::titles::ui {

class TitlePreview;

class CustomizePage : public laycue::ui::PageFrame {
	Q_OBJECT

public:
	CustomizePage(const Overrides &start, const Library &library, QWidget *parent = nullptr);

signals:
	// `previousName` is the name it had when the page opened ("" for a new template), so a rename replaces it.
	void saved(const Overrides &overrides, const std::string &previousName);

private:
	void rebuild();
	void refreshPreview();
	void save();

	Overrides o_;
	std::string previousName_;
	const Library &library_;
	TitlePreview *preview_ = nullptr;
	QWidget *controls_ = nullptr;
};

} // namespace laycue::titles::ui
