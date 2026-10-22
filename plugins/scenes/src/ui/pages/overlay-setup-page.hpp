// "Set up overlays": label the sources that are overlays (Scripture, Lyrics, Lower third,
// Logo or your own), and choose which overlays replace each other.
#pragma once

#include "ui/pages/page-frame.hpp"

class QLabel;

namespace laycue {
class App;
}

namespace laycue::ui {

class OverlaySetupPage : public PageFrame {
	Q_OBJECT

public:
	explicit OverlaySetupPage(App &app, QWidget *parent = nullptr);

	// Labels every unlabelled source it is sure about, and says why under each name.
	void autoTag();

private:
	std::map<std::string, QString> reasons_; // why Auto-label chose a label, shown under the source's name
	void rebuildSources();
	void rebuildRules();

	App &app_;
	QWidget *sourcesHost_ = nullptr;
	QWidget *rulesHost_ = nullptr;
	QLabel *message_ = nullptr;
};

} // namespace laycue::ui
