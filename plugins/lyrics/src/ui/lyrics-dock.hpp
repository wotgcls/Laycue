// The Laycue Lyrics panel: type a song's name, pick it, and its lines go on screen two at a time. Songs can be
// written, pasted, edited and imported (OpenLyrics XML or text).
#pragma once

#include "core/online.hpp"
#include "core/song.hpp"
#include "store/lyrics-io.hpp"
#include "update/updater.hpp"

#include <QTimer>
#include <QWidget>

#include <string>
#include <vector>

// The plugin version string (defined in plugin-support.c). Declared here rather than including plugin-support.h,
// which clashes with OBS's own logging declarations when included before them.
extern "C" const char *PLUGIN_VERSION;

class QLabel;
class QLineEdit;
class QPlainTextEdit;
class QScrollArea;
class QStackedWidget;
class QVBoxLayout;

namespace laycue::ui {
class ActionButton;
class RowButton;
class Segmented;
} // namespace laycue::ui

namespace laycue::slides {
class StyleEditor;
}

namespace laycue::lyrics {

class OnlineSearch;

class LyricsDock : public QWidget {
	Q_OBJECT

public:
	explicit LyricsDock(QWidget *parent = nullptr);
	~LyricsDock() override;

	void runHotkey(int index); // 0 show/hide, 1 next, 2 previous
	// The source clicked in the Sources list: bring this panel forward, ready with its words box.
	void bringForward();

protected:
	bool eventFilter(QObject *watched, QEvent *event) override;

private:
	QWidget *buildMain();
	QWidget *buildEditor();
	void searched();
	void openSong(size_t index);
	void showSlide(int index);
	void toggleShown();
	void step(int direction);
	void push();
	void rebuildResults();
	void rebuildSlides();
	void updateStatus();
	void edit(bool newSong);
	void saveEdit();
	void deleteSong();
	void importSong();
	void searchOnline();
	void addWebRow(const QString &query);  // Google, Genius, Musixmatch, AZLyrics in the browser, then Paste
	void pasteWords(const QString &title); // a new song from the words on the clipboard
	void addOnline(size_t index);
	void addFound(const OnlineSong &found);
	void useTemplate(int index);
	void showTemplates();
	void toast(const QString &text);
	int songIndex() const; // the open song in songs_, or -1

	std::vector<Song> songs_;
	LyricsSettings settings_;
	std::string songId_; // the open song
	std::vector<LyricSlide> slides_;
	int slide_ = -1; // the slide on screen (or ready)
	bool shown_ = false;
	std::string sourceName_;
	bool editingNew_ = false;
	OnlineSearch *online_ = nullptr;
	QString onlineQuery_; // the query the online results belong to
	bool onlineBusy_ = false;
	QString onlineError_;
	std::vector<OnlineSong> onlineResults_;

	QStackedWidget *stack_ = nullptr;
	QLabel *status_ = nullptr;
	QLabel *presence_ = nullptr;
	QLineEdit *search_ = nullptr;
	QVBoxLayout *resultsLayout_ = nullptr;
	QWidget *resultsBox_ = nullptr;
	QLabel *songTitle_ = nullptr;
	QVBoxLayout *slidesLayout_ = nullptr;
	QScrollArea *slidesScroll_ = nullptr; // the song's lines: a set height with its own scrolling
	ui::ActionButton *showButton_ = nullptr;
	ui::Switch *songNameSwitch_ = nullptr; // the song's name rides with the lyrics, or it does not
	ui::Segmented *packs_ = nullptr;     // Classic / Modern / Sanctuary
	ui::Segmented *templates_ = nullptr; // the templates of the chosen pack
	std::vector<int> packMembers_;       // their positions in lyricsTemplates()
	slides::StyleEditor *editor_ = nullptr;
	ui::RowButton *editRow_ = nullptr;
	QLineEdit *editTitle_ = nullptr;
	QLineEdit *editAuthor_ = nullptr;
	QPlainTextEdit *editText_ = nullptr;
	ui::ActionButton *deleteButton_ = nullptr;
	QLabel *toast_ = nullptr;
	QTimer toastTimer_;
	laycue::update::Updater updater_{QString::fromUtf8(PLUGIN_VERSION)}; // "Check for updates" card
	QTimer pollTimer_;
};

} // namespace laycue::lyrics
