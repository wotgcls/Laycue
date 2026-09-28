// "Add a pushback": choose the graphic (Laycue finds its window), choose which cameras can show
// inside it, then name it. Laycue builds a scene of its own for it.
#pragma once

#include "core/config.hpp"
#include "core/detect.hpp"
#include "obs/pixel-reader.hpp"
#include "ui/pages/page-frame.hpp"

#include <QString>

#include <optional>
#include <string>
#include <vector>

class QLabel;
class QLineEdit;
class QPushButton;
class QStackedWidget;
class QVBoxLayout;

namespace laycue {
class App;
}

namespace laycue::ui {

class Segmented;
class Switch;
class WindowPreview;

class AddPushbackPage : public PageFrame {
	Q_OBJECT

public:
	explicit AddPushbackPage(App &app, QWidget *parent = nullptr);

signals:
	// The pushback was made; `sceneUuid` is its scene.
	void created(const std::string &sceneUuid);

private:
	enum class Step { Graphic, Cameras, Name, Done };
	struct FeedChoice {
		std::string uuid;
		std::string name;
		QString kind;
		bool isScene = false;
		bool chosen = false;
	};

	QWidget *buildGraphicStep();
	QWidget *buildCamerasStep();
	QWidget *buildNameStep();
	QWidget *buildDoneStep();
	void showStep(Step step);
	void primaryClicked();
	void backClicked();

	void chooseFile();
	void chooseSource();
	void setPicture(RgbaImage picture, const QString &origin);
	void detect(bool automatic);
	void showResult();
	void pickColour();
	void updateColourRow();
	void loadFeeds();
	void rebuildFeedRows();
	void refreshNameStep();
	void create();
	void showFailure(const QString &message);
	void setDoneIcon(bool success);

	App &app_;
	QStackedWidget *steps_ = nullptr;
	QLabel *progress_ = nullptr;
	QPushButton *primary_ = nullptr;
	QPushButton *backStep_ = nullptr;
	Step step_ = Step::Graphic;

	// Step 1
	WindowPreview *preview_ = nullptr;
	QLabel *originLabel_ = nullptr;
	Segmented *kindSeg_ = nullptr;
	QWidget *kindCard_ = nullptr;
	QWidget *colourBox_ = nullptr;
	QWidget *removeBox_ = nullptr;
	QLabel *swatch_ = nullptr;
	QLabel *colourText_ = nullptr;
	Switch *removeColour_ = nullptr;
	QLabel *removeColourText_ = nullptr;
	QLabel *resultDot_ = nullptr;
	QLabel *resultLabel_ = nullptr;
	QLabel *detailLabel_ = nullptr;

	// Step 2
	QWidget *feedsHost_ = nullptr;
	Segmented *fitSeg_ = nullptr;

	// Step 3
	QLineEdit *nameEdit_ = nullptr;
	QLabel *summary_ = nullptr;
	QLabel *nameError_ = nullptr;

	// Done
	QLabel *doneIcon_ = nullptr;
	QLabel *doneTitle_ = nullptr;
	QLabel *doneBody_ = nullptr;

	// What has been chosen
	RgbaImage picture_;
	QString filePath_;
	std::string sourceUuid_; // set when the graphic is a source already in OBS
	std::string sourceName_;
	WindowKind kind_ = WindowKind::SeeThrough;
	std::optional<Rgb> customKey_;
	DetectResult result_;
	std::vector<FeedChoice> feeds_;
	std::string createdSceneUuid_;
};

} // namespace laycue::ui
