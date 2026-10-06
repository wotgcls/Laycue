// The Laycue Titles panel: preview, text, one big Show button, the template gallery, a queue for the
// next titles, and the small amount of setup (which title layer, brand colour, auto-hide).
#pragma once

#include "core/library.hpp"
#include "obs/source-control.hpp"

#include <obs-frontend-api.h>

#include "update/updater.hpp"

#include <plugin-support.h>
#include <QTimer>
#include <QWidget>

#include <map>
#include <string>
#include <vector>

class QGridLayout;
class QLabel;
class QLineEdit;
class QPlainTextEdit;
class QScrollArea;
class QStackedWidget;
class QVBoxLayout;

namespace laycue::ui {
class ActionButton;
class Segmented;
class RowButton;
class Surface;
} // namespace laycue::ui

namespace laycue::titles::ui {

class TemplateCard;
class TitlePreview;

class TitlesDock : public QWidget {
	Q_OBJECT

public:
	explicit TitlesDock(QWidget *parent = nullptr);
	~TitlesDock() override;

	void handleFrontendEvent(enum obs_frontend_event event);
	// 0 = show or hide, 1 = play the next title in the queue, 2 = hide.
	void runHotkey(int index);

protected:
	void resizeEvent(QResizeEvent *event) override;
	void showEvent(QShowEvent *event) override;
	void hideEvent(QHideEvent *event) override;
	bool event(QEvent *event) override;
	bool eventFilter(QObject *watched, QEvent *event) override;

private:
	enum class Tab { Templates, Queue, Setup };

	void buildUi();
	QWidget *buildStage();
	QWidget *buildTabs();
	void showTab(Tab tab);

	void rebuildTemplates();
	void layoutTemplates();
	void rebuildQueue();
	void rebuildSetup();
	void refreshFields();
	void updatePreview();
	void updateButtons();
	void updateStatus();

	void selectStyle(const std::string &styleId);
	void fieldsEdited();
	TitleContent currentContent() const;
	TitleContent sampleFor(const std::string &styleId) const;
	QImage thumbnailFor(const std::string &styleId);

	void primaryClicked();
	void hideClicked();
	void addToQueueClicked();
	void playNextClicked();
	void playQueueItem(size_t index);
	bool applyAndShow();
	void pollStatus();
	void addToThisScene();
	void addToAllScenesClicked();
	void openCustomize(bool editSelected);
	void deleteSelectedCustom();
	void pickBrandColour();
	void changeHold(int delta);
	void changeSpeed(int delta);
	void chooseLogoParts();
	void refreshParts();
	void changeShape(int fit, int widthDelta);
	void refreshShape();
	void pickCategory();
	void refreshCategory();
	void refreshOptions();
	void rebuildRows();
	void chooseLogo();
	void clearLogo();
	void draftChanged(); // the draft was changed by something other than the text boxes
	void refreshHold();
	void changePlacement(int align, int percentDelta); // align -1 keeps it; percentDelta in steps of 10
	void refreshPlacement();

	void toast(const QString &message);
	void scheduleSave();
	void saveNow();
	void refreshSources();
	std::string target() const { return library_.targetSource; }

	Library library_;
	std::vector<std::string> sources_;
	TitleStatus status_;
	TitleContent onAirContent_;
	std::string onAirStyle_;
	laycue::titles::Placement onAirPlacement_; // the position and size of the title on air
	std::string
		lastSampleStyle_; // the style whose sample text is in the boxes (to know if the operator typed their own)
	bool pendingPlay_ = false;
	bool buildingFields_ = false;

	QTimer pollTimer_;
	QTimer saveTimer_;
	QTimer pendingTimer_;
	QTimer toastTimer_;

	std::map<std::string, QImage> thumbs_;
	std::map<std::string, TemplateCard *> cards_;

	// widgets
	QWidget *content_ = nullptr;
	QStackedWidget *pages_ = nullptr;
	QWidget *home_ = nullptr;
	QWidget *openedPage_ = nullptr;
	laycue::ui::Segmented *tabs_ = nullptr;
	laycue::ui::RowButton *categoryRow_ = nullptr; // the category menu
	laycue::ui::RowButton *optionsRow_ = nullptr;  // folds out position, size and time on screen
	QWidget *optionsBox_ = nullptr;
	QWidget *shapeBox_ = nullptr; // name shape width: own / fit to text / manual
	laycue::ui::Segmented *fitSeg_ = nullptr;
	QWidget *widthRow_ = nullptr;
	QLabel *widthValue_ = nullptr;
	QWidget *fieldRow1_ = nullptr;
	QWidget *fieldRow2_ = nullptr;
	QWidget *fieldRow3_ = nullptr;
	QWidget *fieldRow4_ = nullptr;
	QWidget *holdBox_ = nullptr; // how long it stays on screen
	QLabel *holdValue_ = nullptr;
	bool prevLive_ = false;
	QWidget *placementBox_ = nullptr; // Position and Size, for titles that do not span the picture
	laycue::ui::Segmented *alignSeg_ = nullptr;
	QLabel *sizeValue_ = nullptr;
	QLabel *statusLabel_ = nullptr;
	QWidget *statusDot_ = nullptr;
	TitlePreview *preview_ = nullptr;
	QLabel *label1_ = nullptr;
	QLabel *label2_ = nullptr;
	QLabel *label3_ = nullptr;
	QLabel *label4_ = nullptr; // the note under the title (sort code)
	QLineEdit *edit4_ = nullptr;
	QWidget *logoBox_ = nullptr;  // choose a logo picture
	QWidget *partsBox_ = nullptr; // set which part of the logo is the mark and which the wordmark
	QLabel *partsLabel_ = nullptr;
	QLabel *logoName_ = nullptr;
	QWidget *rowsBox_ = nullptr; // the accounts: label and number pairs, as many as the operator adds
	QVBoxLayout *rowsLayout_ = nullptr;
	QLineEdit *edit1_ = nullptr;
	QPlainTextEdit *long1_ = nullptr;
	QLineEdit *edit2_ = nullptr;
	QLineEdit *edit3_ = nullptr;
	laycue::ui::ActionButton *primary_ = nullptr;
	laycue::ui::ActionButton *hide_ = nullptr;
	laycue::ui::ActionButton *queueAdd_ = nullptr;
	QLabel *hint_ = nullptr;
	QScrollArea *tabScroll_ = nullptr;
	QWidget *templatesTab_ = nullptr;
	QWidget *gridHost_ = nullptr;
	QGridLayout *grid_ = nullptr;
	QWidget *templateActions_ = nullptr;
	QVBoxLayout *templateActionsLayout_ = nullptr;
	QWidget *queueTab_ = nullptr;
	QVBoxLayout *queueLayout_ = nullptr;
	QWidget *setupTab_ = nullptr;
	QVBoxLayout *setupLayout_ = nullptr;
	QWidget *toast_ = nullptr;
	QLabel *toastText_ = nullptr;

	laycue::update::Updater updater_{QString::fromUtf8(PLUGIN_VERSION)}; // "Check for updates" in Setup
	bool optionsOpen_ = false; // position, size and time on screen are unfolded
	int categoryFilter_ = 0;   // 0 = all, then the categories in order (People = 1 ...)
	Tab tab_ = Tab::Templates;
	bool shuttingDown_ = false;
};

} // namespace laycue::titles::ui
