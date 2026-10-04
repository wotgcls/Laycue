#include "ui/view-takeover.hpp"

#include <obs-frontend-api.h>
#include <plugin-support.h>

#include <QDockWidget>
#include <QMainWindow>
#include <QString>

namespace laycue::ui {

namespace {

QMainWindow *mainWindow()
{
	return static_cast<QMainWindow *>(obs_frontend_get_main_window());
}

} // namespace

ViewTakeover::ViewTakeover(QWidget *panel) : panel_(panel) {}

ViewTakeover::~ViewTakeover()
{
	set(false);
}

QDockWidget *ViewTakeover::findDock() const
{
	for (QWidget *w = panel_; w; w = w->parentWidget()) {
		if (auto *dock = qobject_cast<QDockWidget *>(w))
			return dock;
	}
	return nullptr;
}

bool ViewTakeover::set(bool on)
{
	if (on == on_)
		return true;
	try {
		QMainWindow *main = mainWindow();
		if (!main)
			return false;

		if (!on) {
			if (hidden_)
				hidden_->show();
			hidden_.clear();
			if (dock_ && !dock_->isFloating() && savedSize_ > 0)
				main->resizeDocks({dock_}, {savedSize_}, horizontal_ ? Qt::Horizontal : Qt::Vertical);
			dock_.clear();
			on_ = false;
			return true;
		}

		// OBS's Preview, Program and transition buttons are the window's central widget. OBS never
		// hides or shows that widget itself, so hiding it (and nothing else) is safe to undo.
		QWidget *center = main->centralWidget();
		if (!center) {
			obs_log(LOG_WARNING, "Laycue view: OBS's main window has no central widget");
			return false;
		}
		hidden_ = center;
		center->hide();

		dock_ = findDock();
		if (dock_ && !dock_->isFloating()) {
			const Qt::DockWidgetArea area = main->dockWidgetArea(dock_);
			horizontal_ = area == Qt::LeftDockWidgetArea || area == Qt::RightDockWidgetArea;
			savedSize_ = horizontal_ ? dock_->width() : dock_->height();
			const int available = horizontal_ ? main->width() : main->height();
			main->resizeDocks({dock_}, {available * 65 / 100}, horizontal_ ? Qt::Horizontal : Qt::Vertical);
		}
		on_ = true;
		return true;
	} catch (...) {
		obs_log(LOG_ERROR, "Laycue view: unexpected error");
		return false;
	}
}

} // namespace laycue::ui
