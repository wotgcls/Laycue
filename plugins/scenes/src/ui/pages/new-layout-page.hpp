// "New layout": pick a shape (split screen, picture in picture...), choose which source goes in
// each part, name it. Laycue builds a scene of its own for it; nothing else in OBS is touched.
#pragma once

#include "core/config.hpp"
#include "ui/pages/page-frame.hpp"

#include <string>
#include <vector>

class QAbstractButton;
class QGridLayout;
class QLabel;
class QLineEdit;
class QVBoxLayout;

namespace laycue {
class App;
}

namespace laycue::ui {

class Segmented;
class TemplateCard;

class NewLayoutPage : public PageFrame {
	Q_OBJECT

public:
	// `initialTemplate` is the shape already picked on the Layouts tab (-1: show the shapes to choose from).
	// `existing` opens a layout that was made before, to change it (same scene, same name unless edited).
	explicit NewLayoutPage(App &app, int initialTemplate = -1, const Layout *existing = nullptr,
			       QWidget *parent = nullptr);

signals:
	// The layout was made; `sceneUuid` is its scene.
	void created(const std::string &sceneUuid, const QString &name);

private:
	struct Choice {
		std::string uuid;
		std::string name;
		QString kind;
	};

	void selectTemplate(int index);
	void rebuildSlotRows();
	void chooseFor(int slot, QAbstractButton *button);
	void updateCreate();
	void moveSlot(int from, int to);
	void showSplit();
	void chooseBackground();
	void showBackground();
	void create();

	App &app_;
	int template_ = 1; // side by side is the most common first choice
	std::vector<TemplateCard *> cards_;
	QWidget *slotsHost_ = nullptr;
	Segmented *fitSeg_ = nullptr;
	QLineEdit *nameEdit_ = nullptr;
	QLabel *shapeLabel_ = nullptr;
	QAbstractButton *createButton_ = nullptr;
	QLabel *errorLabel_ = nullptr;
	QAbstractButton *backgroundButton_ = nullptr;
	QAbstractButton *backgroundRemove_ = nullptr;
	QLabel *backgroundLabel_ = nullptr;
	QWidget *splitBox_ = nullptr;
	QLabel *splitValue_ = nullptr;
	int split_ = 0;     // first part's share in percent for a two-part split
	int splitFor_ = -1; // the template split_ belongs to
	bool editing_ = false;
	Layout existing_;        // the layout being changed, when editing_
	std::string background_; // the chosen picture, or empty
	bool silver_ = false;    // the metal picked for it
	bool nameEdited_ = false;
	bool settingName_ = false;
	std::vector<Choice> available_;
	std::vector<Choice> chosen_; // one per slot of the selected template (uuid empty = nothing yet)
};

} // namespace laycue::ui
