// The Laycue Scripture panel: pick a version, type a reference the EasyWorship way ("jn" Space "3" Space
// "16" Enter), and the verse goes on screen. Next and Previous walk the verses; saved scriptures come back with
// one tap.
#pragma once

#include "core/bible.hpp"
#include "store/scripture-io.hpp"
#include "update/updater.hpp"

#include <QTimer>
#include <QWidget>

#include <memory>
#include <string>
#include <vector>

// The plugin version string (defined in plugin-support.c). Declared here rather than including plugin-support.h,
// which clashes with OBS's own logging declarations when included before them.
extern "C" const char *PLUGIN_VERSION;

class QLabel;
class QLineEdit;
class QVBoxLayout;

namespace laycue::ui {
class RowButton;
class Segmented;
class ActionButton;
} // namespace laycue::ui

namespace laycue::slides {
class StyleEditor;
}

namespace laycue::scripture {

class ReferenceBox;

class ScriptureDock : public QWidget {
	Q_OBJECT

public:
	explicit ScriptureDock(QWidget *parent = nullptr);
	~ScriptureDock() override;

	void runHotkey(int index); // 0 show/hide, 1 next, 2 previous

private:
	void build();
	void chooseVersion();
	bool useVersion(const VersionFile &version);
	void typed();
	void go(); // Enter: show what is typed, or search for it when it isn't a reference
	void search();
	void showVerse(const Reference &ref);
	void toggleShown();
	void step(int direction);
	void push(); // writes the current verse into the layer
	void rebuildVerses();
	void rebuildSaved();
	void updateStatus();
	void useTemplate(int index);
	void showTemplates();
	void save();
	void toast(const QString &text);

	ScriptureSettings settings_;
	VersionFile version_;
	std::shared_ptr<const Bible> bible_;
	Reference current_; // the verse on screen (or ready)
	bool shown_ = false;
	std::string sourceName_;
	// A phrase typed in the box, shown as a list of the verses that hold it until one is picked.
	bool searching_ = false;
	std::string searchQuery_;
	std::vector<Reference> searchResults_;

	ui::RowButton *versionRow_ = nullptr;
	QLabel *presence_ = nullptr;
	QLabel *status_ = nullptr;
	ReferenceBox *input_ = nullptr;
	QLabel *hint_ = nullptr;
	QLabel *referenceLabel_ = nullptr; // "John 3:16 (KJV)" above the verses
	QWidget *versesBox_ = nullptr;
	QVBoxLayout *versesLayout_ = nullptr;
	ui::ActionButton *showButton_ = nullptr;
	ui::Segmented *packs_ = nullptr;     // Classic / Modern / Sanctuary
	ui::Segmented *templates_ = nullptr; // the templates of the chosen pack
	std::vector<int> packMembers_;       // their positions in scriptureTemplates()
	slides::StyleEditor *editor_ = nullptr;
	ui::RowButton *editRow_ = nullptr;
	QVBoxLayout *savedLayout_ = nullptr;
	QLabel *toast_ = nullptr;
	QTimer toastTimer_;
	laycue::update::Updater updater_{QString::fromUtf8(PLUGIN_VERSION)}; // "Check for updates" card
	QTimer pollTimer_;
};

} // namespace laycue::scripture
