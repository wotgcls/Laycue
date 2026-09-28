// Settings: how Laycue switches, how the panel looks, saved setups (profiles), and Lock.
#pragma once

#include "ui/pages/page-frame.hpp"

class QLabel;
class QLineEdit;

namespace laycue {
class App;
}

namespace laycue::ui {

class SettingsPage : public PageFrame {
	Q_OBJECT

public:
	explicit SettingsPage(App &app, QWidget *parent = nullptr);

signals:
	void overlaySetupRequested();
	void refitAllRequested();

private:
	void build();
	void rebuildProfiles();
	void say(const QString &message);

	App &app_;
	QWidget *editHost_ = nullptr; // everything that changes setup: hidden while locked
	QWidget *profilesHost_ = nullptr;
	QLineEdit *profileName_ = nullptr;
	QLabel *message_ = nullptr;
};

} // namespace laycue::ui
