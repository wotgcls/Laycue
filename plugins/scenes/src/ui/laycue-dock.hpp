// The Laycue panel shown inside OBS (Docks → Laycue).
//
// One stage (Live, Up next, Cut, Go Live) on top, tabs underneath (Shots, Overlays), and
// full-panel pages for setup (Set up overlays, Settings…). Only features that work are shown.
#pragma once

#include "core/cue-state.hpp"
#include "obs/frame-grabber.hpp"
#include "features/hotkeys.hpp"
#include "features/overlay-engine.hpp"
#include "obs/scene-watcher.hpp"

#include <obs-frontend-api.h>

#include <QTimer>
#include <QWidget>

#include <map>
#include <memory>
#include <string>
#include <vector>

class QAbstractButton;
class QGridLayout;
class QLabel;
class QPushButton;
class QScrollArea;
class QStackedWidget;
class QVBoxLayout;

namespace laycue {

class App;
struct Pushback;
struct Layout;
struct Snapshot;

namespace ui {
class OverlaysTab;
class PictureView;
class Segmented;
class ViewTakeover;
class StageButton;
class ShotCard;
class Surface;
} // namespace ui

class LaycueDock : public QWidget {
	Q_OBJECT

public:
	LaycueDock(App &app, QWidget *parent = nullptr);
	~LaycueDock() override;

	// Called on the UI thread for every OBS frontend event.
	void handleFrontendEvent(enum obs_frontend_event event);

	// A Laycue hotkey was pressed (UI thread).
	void runHotkey(HotkeyAction action);

protected:
	void resizeEvent(QResizeEvent *event) override;
	bool eventFilter(QObject *watched, QEvent *event) override;

private:
	struct Shot {
		std::string uuid;
		std::string name;
	};
	enum class Tab { Shots, Pushbacks, Overlays };

	void buildUi();
	QWidget *buildHome();
	void showTab(Tab tab);
	void openPage(QWidget *page);
	void closePage();
	void scheduleRefresh(); // thread-safe; coalesces bursts of changes
	void refreshNow();      // UI thread only
	void onConfigChanged();
	void rebuildCards();
	void layoutCards();
	void layoutGrid(QGridLayout *grid, const std::vector<Shot> &shots, int available);
	void updateLayoutsTab();
	void syncOverlays(); // Laycue's own title and clock layers become overlays; pinned sources stay in every scene
	void updateInsideBar();
	void switchInside(const std::string &pushbackId, const std::string &feedUuid);
	void unpin();
	void stepCamera(int direction);
	void pushbackMenu(const std::string &pushbackId, const QPoint &globalPos);
	void openAddPushback();
	void openNewLayout(int shape = -1, const Layout *existing = nullptr);
	void layoutMenu(const std::string &layoutId, const QPoint &globalPos);
	void repairLayouts();
	void updateGuide();
	void openSettings();
	void applyView(); // Laycue view on/off, to match the setting
	void openOverlaySetup(bool autoLabel = false);
	void repairPushbacks();
	const Pushback *pushbackForScene(const std::string &sceneUuid) const;
	const Layout *layoutForScene(const std::string &sceneUuid) const;
	void arrangePictures(bool wide);
	void updateStateUi();
	void updateTransitionLabel();
	void updatePictureTargets();
	void pullPictures();
	void updateOverlays(const Snapshot &snapshot);
	void onShotClicked(const std::string &uuid);
	void goLive(bool instant);
	void finishCut(); // puts back the operator's transition after a Cut
	void chooseMode(bool studio);
	void onItemVisible(const std::string &sceneUuid, int64_t itemId, bool visible);
	void showToast(const QString &message);
	void placeToast();
	std::string nameOf(const std::string &uuid) const;
	void resetForCollectionChange();

	App &app_;
	int listenerId_ = 0;

	// Structure
	QWidget *content_ = nullptr;
	QStackedWidget *pages_ = nullptr;
	QWidget *home_ = nullptr;
	QWidget *openedPage_ = nullptr;
	ui::Segmented *tabs_ = nullptr;
	QAbstractButton *lockPill_ = nullptr;
	QAbstractButton *addPushbackButton_ = nullptr;
	QAbstractButton *emptyAddButton_ = nullptr;
	QWidget *shapesHost_ = nullptr; // the layout shapes, in plain sight on the Layouts tab
	Tab tab_ = Tab::Shots;

	// Stage
	ui::Surface *stage_ = nullptr;
	ui::PictureView *liveView_ = nullptr;
	ui::PictureView *nextView_ = nullptr;
	QGridLayout *picturesGrid_ = nullptr;
	QWidget *nextTextBox_ = nullptr;
	bool wideLayout_ = false;
	bool picturesArranged_ = false;
	QLabel *nextEyebrow_ = nullptr;
	QLabel *nextDot_ = nullptr;
	QLabel *nextName_ = nullptr;
	QLabel *nextHint_ = nullptr;
	ui::StageButton *cutButton_ = nullptr;
	ui::StageButton *goLiveButton_ = nullptr;

	// The live pushback's "camera inside" controls
	QWidget *insideBar_ = nullptr;
	QVBoxLayout *insideLayout_ = nullptr;
	std::string insideKey_; // what the bar currently shows, so it is only rebuilt when it changes

	// Tabs
	QScrollArea *tabScroll_ = nullptr;
	QWidget *shotsTab_ = nullptr;
	QVBoxLayout *shotsTabLayout_ = nullptr;
	QWidget *shotsGuideHost_ = nullptr;
	QWidget *shotsGridHost_ = nullptr;
	QGridLayout *shotsGrid_ = nullptr;
	QLabel *emptyLabel_ = nullptr;
	QWidget *pushbacksTab_ = nullptr;
	QWidget *pushbacksHeaderHost_ = nullptr;
	QLabel *lyLabel_ = nullptr;
	QWidget *lyGridHost_ = nullptr;
	QGridLayout *lyGrid_ = nullptr;
	QLabel *pbLabel_ = nullptr;
	QWidget *pbGridHost_ = nullptr;
	QGridLayout *pbGrid_ = nullptr;
	QWidget *pbEmpty_ = nullptr;
	ui::OverlaysTab *overlaysTab_ = nullptr;
	std::map<std::string, ui::ShotCard *> cards_;

	// Notice
	QWidget *toast_ = nullptr;
	QLabel *toastText_ = nullptr;

	QTimer refreshTimer_;
	QTimer pictureTimer_;
	QTimer toastTimer_;
	QTimer suppressTimer_;
	QTimer restoreTimer_; // fallback in case OBS never reports the Cut finishing
	QTimer saveTimer_;    // saves Laycue's setup shortly after it changes

	OverlayEngine overlayEngine_;
	std::unique_ptr<SceneWatcher> watcher_;
	std::unique_ptr<FrameGrabber> grabber_;
	std::unique_ptr<ui::ViewTakeover> view_;
	std::map<std::string, uint64_t> pictureSerials_;

	std::vector<Shot> scenes_;        // every OBS scene, for names and pictures
	std::vector<Shot> shots_;         // the scenes shown in the Shots tab (not Laycue's pushback scenes)
	std::vector<Shot> pushbackShots_; // the pushbacks, by their scene
	std::vector<Shot> layoutShots_;   // the layouts, by their scene
	CueState cue_;
	std::string lastLive_;            // the scene that was live at the last refresh
	std::string previousLive_;        // what was live before the current pushback (for Unpin)
	bool repairChecked_ = false;      // pushback scenes have been checked since the collection loaded
	std::string lastPreview_;         // OBS's preview scene as last seen (studio mode)
	bool suppressPreview_ = false;    // ignore preview changes caused by our own Go Live
	bool pushNextToPreview_ = false;  // studio mode just turned on: show Up next in OBS's Preview
	std::string transitionToRestore_; // the operator's transition while a Cut is running
	bool obsReady_ = false;           // true once OBS has finished loading a scene collection
	bool shuttingDown_ = false;
};

} // namespace laycue
