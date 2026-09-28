// The Laycue panel shown inside OBS (Docks → Laycue).
//
// Release 1 Shots view: every OBS scene is a shot. Tap a shot to put it in Up next,
// then Go Live (OBS transition) or Cut (instant). Only features that work are shown;
// overlays, pushbacks and the Mixer view appear when they are built.
#pragma once

#include "core/cue-state.hpp"
#include "obs/frame-grabber.hpp"
#include "obs/scene-watcher.hpp"

#include <obs-frontend-api.h>

#include <QTimer>
#include <QWidget>

#include <map>
#include <memory>
#include <string>
#include <vector>

class QGridLayout;
class QLabel;
class QPushButton;
class QScrollArea;

namespace laycue {

namespace ui {
class PictureView;
class ShotCard;
} // namespace ui

class LaycueDock : public QWidget {
	Q_OBJECT

public:
	explicit LaycueDock(QWidget *parent = nullptr);
	~LaycueDock() override;

	// Called on the UI thread for every OBS frontend event.
	void handleFrontendEvent(enum obs_frontend_event event);

protected:
	void resizeEvent(QResizeEvent *event) override;
	bool eventFilter(QObject *watched, QEvent *event) override;

private:
	struct Shot {
		std::string uuid;
		std::string name;
	};

	void buildUi();
	void scheduleRefresh(); // thread-safe; coalesces bursts of changes
	void refreshNow();      // UI thread only
	void rebuildCards();
	void layoutCards();
	void updateStateUi();
	void updateTransitionLabel();
	void updatePictureTargets();
	void pullPictures();
	void onShotClicked(const std::string &uuid);
	void goLive(bool instant);
	void finishCut(); // puts back the operator's transition after a Cut
	void showToast(const QString &message);
	std::string nameOf(const std::string &uuid) const;
	void resetForCollectionChange();

	// Widgets (owned by Qt through parenting)
	QLabel *studioLabel_ = nullptr;
	ui::PictureView *liveView_ = nullptr;
	ui::PictureView *nextView_ = nullptr;
	QLabel *nextEyebrow_ = nullptr;
	QLabel *nextDot_ = nullptr;
	QLabel *nextName_ = nullptr;
	QLabel *nextHint_ = nullptr;
	QPushButton *cutButton_ = nullptr;
	QPushButton *goLiveButton_ = nullptr;
	QLabel *goLiveTitle_ = nullptr;
	QLabel *goLiveDetail_ = nullptr;
	QScrollArea *shotsScroll_ = nullptr;
	QWidget *shotsHost_ = nullptr;
	QGridLayout *shotsGrid_ = nullptr;
	QLabel *emptyLabel_ = nullptr;
	QWidget *toast_ = nullptr;
	QLabel *toastText_ = nullptr;
	std::map<std::string, ui::ShotCard *> cards_;

	QTimer refreshTimer_;
	QTimer pictureTimer_;
	QTimer toastTimer_;
	QTimer suppressTimer_;
	QTimer restoreTimer_; // fallback in case OBS never reports the Cut finishing

	std::unique_ptr<SceneWatcher> watcher_;
	std::unique_ptr<FrameGrabber> grabber_;
	std::map<std::string, uint64_t> pictureSerials_;

	std::vector<Shot> shots_;
	CueState cue_;
	std::string lastPreview_;         // OBS's preview scene as last seen (studio mode)
	bool suppressPreview_ = false;    // ignore preview changes caused by our own Go Live
	std::string transitionToRestore_; // the operator's transition while a Cut is running
	bool obsReady_ = false;           // true once OBS has finished loading a scene collection
	bool shuttingDown_ = false;
};

} // namespace laycue
