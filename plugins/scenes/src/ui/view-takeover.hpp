// "Laycue view": hides OBS's own Preview and Program area so the Laycue panel can fill the window.
// OBS gives plugins no official way to do this, so it works by hiding one widget of OBS's main
// window. It is opt-in, always undoable, and does nothing if OBS's window doesn't look as expected.
#pragma once

#include <QPointer>

class QDockWidget;
class QWidget;

namespace laycue::ui {

class ViewTakeover {
public:
	// `panel` is the Laycue panel; the dock that holds it is the one that gets made larger.
	explicit ViewTakeover(QWidget *panel);
	~ViewTakeover();

	// Turns Laycue view on or off. Returns false (and changes nothing) if OBS's window can't be
	// handled. Turning it on when it is already on, or off when off, does nothing.
	bool set(bool on);
	bool isOn() const { return on_; }

private:
	QDockWidget *findDock() const;

	QWidget *panel_;
	QPointer<QWidget> hidden_; // OBS's Preview/Program area while it is hidden
	QPointer<QDockWidget> dock_;
	int savedSize_ = 0;
	bool horizontal_ = true;
	bool on_ = false;
};

} // namespace laycue::ui
