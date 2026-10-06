#include "ui/laycue-menu.hpp"

#include <obs-frontend-api.h>
#include <obs-module.h>

#include <QAction>
#include <QDockWidget>
#include <QMainWindow>
#include <QMenu>
#include <QMenuBar>

namespace laycue::ui {

namespace {
constexpr const char *kMenuName = "laycueMenu";
constexpr const char *kHelpMenuName = "menuBasic_MainMenu_Help"; // OBS's own Help menu
} // namespace

void addToLaycueMenu(QWidget *dockContent, const QString &menuTitle, const QString &itemText)
{
	try {
		auto *dockWidget = dockContent ? qobject_cast<QDockWidget *>(dockContent->parentWidget()) : nullptr;
		auto *window = qobject_cast<QMainWindow *>(static_cast<QWidget *>(obs_frontend_get_main_window()));
		if (!dockWidget || !window)
			return;
		QMenuBar *bar = window->menuBar();
		if (!bar)
			return;

		QMenu *menu = bar->findChild<QMenu *>(kMenuName, Qt::FindDirectChildrenOnly);
		if (!menu) {
			menu = new QMenu(menuTitle, bar);
			menu->setObjectName(kMenuName);
			QAction *before = nullptr;
			for (QAction *a : bar->actions()) {
				if (a->menu() && a->menu()->objectName() == QLatin1String(kHelpMenuName))
					before = a;
			}
			bar->insertMenu(before, menu); // before Help; at the end if OBS has no Help menu
		}

		// Owned by the dock, so the entry goes away with it.
		auto *action = new QAction(itemText, dockWidget);
		action->setCheckable(true);
		action->setChecked(dockWidget->isVisible());
		QAction *toggle = dockWidget->toggleViewAction(); // the same switch OBS's Docks menu uses
		QObject::connect(action, &QAction::triggered, toggle, [toggle]() { toggle->trigger(); });
		QObject::connect(toggle, &QAction::toggled, action, &QAction::setChecked);

		// Entries stay in alphabetical order whichever plugin loads first.
		QAction *insertBefore = nullptr;
		for (QAction *a : menu->actions()) {
			if (a->text().compare(itemText, Qt::CaseInsensitive) > 0) {
				insertBefore = a;
				break;
			}
		}
		menu->insertAction(insertBefore, action);
	} catch (...) {
		blog(LOG_ERROR, "Laycue: could not add the Laycue menu");
	}
}

} // namespace laycue::ui
