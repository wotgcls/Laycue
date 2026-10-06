// The "Updates" card shown in each plugin's settings: a button that checks for a newer Laycue, downloads it
// and installs it once OBS has closed. All the work is in update::Updater; this only shows its state.
#pragma once

#include "ui/controls.hpp"

class QLabel;

namespace laycue::update {
class Updater;
}

namespace laycue::ui {

class UpdateBar;

class UpdateCard : public Surface {
	Q_OBJECT

public:
	UpdateCard(update::Updater &updater, QWidget *parent = nullptr);

private:
	void refresh();
	void primaryClicked();

	update::Updater &updater_;
	QLabel *status_ = nullptr;
	QLabel *notes_ = nullptr;
	UpdateBar *bar_ = nullptr;
	ActionButton *primary_ = nullptr;
	ActionButton *secondary_ = nullptr;
};

} // namespace laycue::ui
