// The Laycue Network panel: a recommended bitrate from a quick upload test, a live view of the stream's
// health, optional auto-adjust, and a report after each stream.
#pragma once

#include "core/network-settings.hpp"
#include "core/recommend.hpp"
#include "features/session.hpp"
#include "obs/speed-test.hpp"

#include <obs-frontend-api.h>

#include "update/updater.hpp"

// The plugin version string (defined in plugin-support.c). Declared here rather than including plugin-support.h,
// which clashes with OBS's own logging declarations when included before them.
extern "C" const char *PLUGIN_VERSION;
#include <QTimer>
#include <QWidget>

class QLabel;
class QScrollArea;
class QStackedWidget;
class QVBoxLayout;

namespace laycue::ui {
class Segmented;
class Surface;
} // namespace laycue::ui

namespace laycue::network::ui {

class LiveChart;
class HealthPill;
class ProgressBar;

class NetworkDock : public QWidget {
	Q_OBJECT

public:
	explicit NetworkDock(QWidget *parent = nullptr);
	~NetworkDock() override;

	void handleFrontendEvent(enum obs_frontend_event event);
	// 0 = turn auto-adjust on or off, 1 = try full bitrate, 2 = run the connection test.
	void runHotkey(int index);

protected:
	void resizeEvent(QResizeEvent *event) override;
	bool event(QEvent *event) override;

private:
	enum class Tab { Test, Auto, Report };

	void buildUi();
	void showTab(Tab tab);
	void rebuildHero();
	void rebuildTest();
	void rebuildAuto();
	void rebuildReport();
	void refreshLive();
	void updateHealth();

	void runTest();
	void testProgress(double mbps, double fraction);
	void testFinished(const laycue::network::UploadMeasure &measure);
	void testFailed(laycue::network::SpeedTest::Failure reason);
	void applyRecommendation();
	void setAdaptive(bool on);
	void onBitrateChanged(int fromKbps, int toKbps, int reason);
	void onStreamStopped(const laycue::network::StreamReport &report);
	Recommendation recommendation() const;
	Platform currentPlatform() const;

	void toast(const QString &message);
	void changed();
	void saveNow();

	Settings settings_;
	Session session_;
	SpeedTest test_;
	QTimer saveTimer_;
	QTimer toastTimer_;

	bool testing_ = false;
	double testMbps_ = 0.0;
	double testFraction_ = 0.0;
	QString testError_;
	QString lastChangeText_;
	QString noticeText_; // a standing note under the hero (e.g. the encoder ignored a change)
	bool shuttingDown_ = false;
	Tab tab_ = Tab::Test;

	laycue::update::Updater updater_{QString::fromUtf8(PLUGIN_VERSION)}; // "Check for updates" in the Test tab
	QWidget *content_ = nullptr;
	QStackedWidget *pages_ = nullptr;
	QWidget *home_ = nullptr;
	HealthPill *pill_ = nullptr;
	laycue::ui::Surface *hero_ = nullptr;
	QVBoxLayout *heroLayout_ = nullptr;
	LiveChart *chart_ = nullptr;
	ProgressBar *progress_ = nullptr;
	QLabel *liveSending_ = nullptr;
	QLabel *liveTarget_ = nullptr;
	QLabel *liveDropped_ = nullptr;
	QLabel *liveNote_ = nullptr;
	laycue::ui::Segmented *tabs_ = nullptr;
	QScrollArea *scroll_ = nullptr;
	QWidget *testTab_ = nullptr;
	QVBoxLayout *testLayout_ = nullptr;
	QWidget *autoTab_ = nullptr;
	QVBoxLayout *autoLayout_ = nullptr;
	QWidget *reportTab_ = nullptr;
	QVBoxLayout *reportLayout_ = nullptr;
	QWidget *toast_ = nullptr;
	QLabel *toastText_ = nullptr;
};

} // namespace laycue::network::ui
