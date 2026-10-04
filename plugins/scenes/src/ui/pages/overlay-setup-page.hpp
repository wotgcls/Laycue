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

private:
	void rebuildSources();
	void rebuildRules();
	void autoTag();

	App &app_;
	QWidget *sourcesHost_ = nullptr;
	QWidget *rulesHost_ = nullptr;
	QLabel *message_ = nullptr;
};

} // namespace laycue::ui
