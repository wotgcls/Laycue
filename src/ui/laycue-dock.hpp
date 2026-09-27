// The Laycue panel shown inside OBS (Docks → Laycue).
// M0: shows the canvas size and every scene with its sources, kept up to date.
#pragma once

#include "core/scene-model.hpp"
#include "obs/scene-watcher.hpp"

#include <obs-frontend-api.h>

#include <QTimer>
#include <QWidget>

#include <memory>
#include <set>
#include <string>

class QLabel;
class QTreeWidget;
class QTreeWidgetItem;

namespace laycue {

class LaycueDock : public QWidget {
	Q_OBJECT

public:
	explicit LaycueDock(QWidget *parent = nullptr);
	~LaycueDock() override;

	// Called on the UI thread for every OBS frontend event.
	void handleFrontendEvent(enum obs_frontend_event event);

private:
	void scheduleRefresh(); // thread-safe; coalesces bursts of changes
	void refreshNow();      // UI thread only
	void showMessage(const QString &message);
	void populate(const Snapshot &snapshot);
	QTreeWidgetItem *makeItemRow(const ItemInfo &item);

	QLabel *summaryLabel_ = nullptr;
	QLabel *messageLabel_ = nullptr;
	QTreeWidget *tree_ = nullptr;
	QTimer refreshTimer_;

	std::unique_ptr<SceneWatcher> watcher_;
	bool obsReady_ = false; // true once OBS has finished loading a scene collection
	bool shuttingDown_ = false;

	// Scenes the operator has expanded or collapsed, so refreshes keep their choice.
	std::set<std::string> expandedScenes_;
	std::set<std::string> collapsedScenes_;
};

} // namespace laycue
