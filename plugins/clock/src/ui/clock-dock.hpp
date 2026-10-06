// The Laycue Clock panel: start, pause and reset the clock, choose what it counts and how it looks.
#pragma once

#include "core/clock-settings.hpp"
#include "obs/clock-control.hpp"
#include "update/updater.hpp"

#include <QTimer>
#include <QWidget>

// The plugin version string (defined in plugin-support.c). Declared here rather than including plugin-support.h,
// which clashes with OBS's own logging declarations when included before them.
extern "C" const char *PLUGIN_VERSION;

class QLabel;
class QLineEdit;
class QScrollArea;
class QVBoxLayout;

namespace laycue::ui {
class ActionButton;
class RowButton;
class Segmented;
} // namespace laycue::ui

namespace laycue::clock::ui {

class Readout;

class ClockDock : public QWidget {
	Q_OBJECT

public:
	explicit ClockDock(QWidget *parent = nullptr);
	~ClockDock() override;

	void runHotkey(int index); // 0 start or pause, 1 reset

private:
	void build();
	void rebuild(); // after the mode or the layer changes
	void poll();
	void changed(); // a setting was edited: apply, save, redraw
	void applyNow();
	void scheduleSave();
	void saveNow();
	void refreshOptions();
	void startPause();
	void reset();
	void addLayer(bool everywhere);
	void pickDesign();
	void pickColour(uint32_t ClockSettings::*which, const QString &title);
	void toast(const QString &text);

	ClockSettings settings_;
	std::string target_;
	ClockStatus status_;
	laycue::update::Updater updater_{QString::fromUtf8(PLUGIN_VERSION)};
	bool optionsOpen_ = false;
	bool building_ = false;
	QTimer pollTimer_;
	QTimer saveTimer_;
	QTimer toastTimer_;

	QScrollArea *scroll_ = nullptr;
	QWidget *page_ = nullptr;
	QVBoxLayout *pageLayout_ = nullptr;
	QLabel *statusLabel_ = nullptr;
	Readout *readout_ = nullptr;
	laycue::ui::ActionButton *startButton_ = nullptr;
	laycue::ui::RowButton *optionsRow_ = nullptr;
	QWidget *optionsBox_ = nullptr;
	QLabel *entryHint_ = nullptr;
	QLabel *toastLabel_ = nullptr;
};

} // namespace laycue::clock::ui
