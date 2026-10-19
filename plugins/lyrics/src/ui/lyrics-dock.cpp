#include "ui/lyrics-dock.hpp"

#include "obs/scene-presence.hpp"
#include "slides/slide-control.hpp"
#include "store/online-search.hpp"
#include "core/templates.hpp"
#include "slides/slide-render.hpp"
#include "slides/style-editor.hpp"
#include "ui/brand-mark.hpp"
#include "ui/controls.hpp"
#include "ui/pack-switcher.hpp"
#include "ui/pages/helpers.hpp"
#include "ui/presence-text.hpp"
#include "ui/style.hpp"
#include "ui/text.hpp"
#include "ui/tokens.hpp"
#include "ui/update-card.hpp"

#include <obs-module.h>
#include <plugin-support.h>

#include <QClipboard>
#include <QDesktopServices>
#include <QFileDialog>
#include <QFileInfo>
#include <QGuiApplication>
#include <QHBoxLayout>
#include <QKeyEvent>
#include <QLabel>
#include <QLineEdit>
#include <QMessageBox>
#include <QMouseEvent>
#include <QPainter>
#include <QPlainTextEdit>
#include <QPointer>
#include <QScrollArea>
#include <QScrollBar>
#include <QStackedWidget>
#include <QUrl>
#include <QVBoxLayout>

#include <QKeySequence>
#include <QShortcut>

#include <algorithm>
#include <utility>

namespace laycue::lyrics {

using namespace laycue::ui;
using namespace laycue::ui::page;

namespace {

constexpr const char *kSourceId = "laycue_lyrics";

QString qs(const std::string &s)
{
	return QString::fromStdString(s);
}

std::vector<std::string> idsOf(const std::vector<Song> &songs)
{
	std::vector<std::string> ids;
	for (const Song &s : songs)
		ids.push_back(s.id);
	return ids;
}

// A thin bar under a scroll area: drag it up or down to change that area's height.
class HeightGrip : public QWidget {
public:
	HeightGrip(QScrollArea *target, QWidget *parent) : QWidget(parent), target_(target)
	{
		setFixedHeight(14);
		setCursor(Qt::SizeVerCursor);
		setToolTip(QStringLiteral("Drag to make the song taller or shorter"));
	}

protected:
	void mousePressEvent(QMouseEvent *event) override
	{
		startY_ = event->globalPosition().y();
		startHeight_ = target_->height();
	}
	void mouseMoveEvent(QMouseEvent *event) override
	{
		const int height = startHeight_ + int(event->globalPosition().y() - startY_);
		target_->setFixedHeight(std::clamp(height, 90, 900));
	}
	void paintEvent(QPaintEvent *) override
	{
		QPainter p(this);
		p.setRenderHint(QPainter::Antialiasing, true);
		p.setPen(QPen(color::textTertiary, 3, Qt::SolidLine, Qt::RoundCap));
		p.drawLine(width() / 2 - 18, height() / 2, width() / 2 + 18, height() / 2);
	}

private:
	QScrollArea *target_;
	double startY_ = 0;
	int startHeight_ = 0;
};

QScrollArea *scrolling(QWidget *parent, QWidget *&page)
{
	auto *scroll = new QScrollArea(parent);
	scroll->setWidgetResizable(true);
	scroll->setFrameShape(QFrame::NoFrame);
	scroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
	page = new QWidget(scroll);
	scroll->setWidget(page);
	return scroll;
}

} // namespace

LyricsDock::LyricsDock(QWidget *parent) : QWidget(parent)
{
	setObjectName("LaycueLyricsDock");
	setMinimumWidth(280);
	// Left and Right step through the lines of the song while the panel has focus (text boxes keep them for typing).
	setFocusPolicy(Qt::StrongFocus);
	for (const auto &[key, direction] : {std::make_pair(Qt::Key_Right, 1), std::make_pair(Qt::Key_Left, -1)}) {
		auto *shortcut = new QShortcut(QKeySequence(key), this);
		shortcut->setContext(Qt::WidgetWithChildrenShortcut);
		const int d = direction;
		connect(shortcut, &QShortcut::activated, this, [this, d]() { step(d); });
	}
	loadBrandFonts();
	slides::loadSlideFonts();
	settings_ = loadSettings();
	songs_ = loadLibrary();

	auto *outer = new QVBoxLayout(this);
	outer->setContentsMargins(0, 0, 0, 0);
	auto *content = new QWidget(this);
	content->setObjectName("LaycueContent");
	content->setAttribute(Qt::WA_StyledBackground, true);
	content->setStyleSheet(panelStyleSheet());
	outer->addWidget(content);
	auto *contentLayout = new QVBoxLayout(content);
	contentLayout->setContentsMargins(0, 0, 0, 0);
	contentLayout->setSpacing(0);
	stack_ = new QStackedWidget(content);
	stack_->addWidget(buildMain());
	stack_->addWidget(buildEditor());
	contentLayout->addWidget(stack_);
	toast_ = innerLabel(content, QString(), 12, 500, color::cream);
	toast_->setStyleSheet(
		QStringLiteral("background: %1; color: %2; border-radius: 12px; padding: 8px 14px; margin: 4px 8px;")
			.arg(hex(color::control), hex(color::cream)));
	toast_->setWordWrap(true); // a long message wraps instead of being cut off
	toast_->setAlignment(Qt::AlignCenter);
	toast_->hide();
	contentLayout->addWidget(toast_);

	online_ = new OnlineSearch(this);
	connect(online_, &OnlineSearch::found, this,
		[this](const QString &query, const std::vector<OnlineSong> &songs) {
			if (query != onlineQuery_)
				return;
			onlineBusy_ = false;
			onlineError_.clear();
			onlineResults_ = songs;
			rebuildResults();
		});
	connect(online_, &OnlineSearch::wordsReady, this, [this](const OnlineSong &song) { addFound(song); });
	connect(online_, &OnlineSearch::wordsFailed, this, [this](const OnlineSong &song, const QString &) {
		toast(lt("Laycue.Lyrics.OnlineNoWords").arg(qs(song.title)));
	});
	connect(online_, &OnlineSearch::failed, this, [this](const QString &query, const QString &reason) {
		if (query != onlineQuery_)
			return;
		onlineBusy_ = false;
		onlineResults_.clear();
		onlineError_ = reason == QStringLiteral("secure") ? lt("Laycue.Lyrics.OnlineNoSecure")
								  : lt("Laycue.Lyrics.OnlineFailed").arg(reason);
		rebuildResults();
	});

	for (size_t i = 0; i < songs_.size(); ++i) {
		if (songs_[i].id == settings_.lastSong)
			openSong(i);
	}
	rebuildResults();

	toastTimer_.setSingleShot(true);
	toastTimer_.setInterval(2600);
	connect(&toastTimer_, &QTimer::timeout, this, [this]() { toast_->hide(); });
	pollTimer_.setInterval(1000);
	connect(&pollTimer_, &QTimer::timeout, this, &LyricsDock::updateStatus);
	pollTimer_.start();
	updateStatus();
}

LyricsDock::~LyricsDock() = default;

QWidget *LyricsDock::buildMain()
{
	QWidget *page = nullptr;
	QScrollArea *scroll = scrolling(this, page);
	auto *v = new QVBoxLayout(page);
	v->setContentsMargins(8, 8, 8, 12);
	v->setSpacing(size::gapSmall + 2);

	v->addWidget(new PackSwitcher(page));
	auto *header = new QHBoxLayout();
	header->setSpacing(size::gapSmall);
	header->addWidget(brandMark(page));
	header->addWidget(innerLabel(page, lt("Laycue.Lyrics.Heading"), 17, 700, color::cream));
	header->addStretch(1);
	status_ = innerLabel(page, QString(), 12, 600, color::textTertiary);
	status_->setWordWrap(false);
	header->addWidget(status_);
	v->addLayout(header);
	presence_ = innerLabel(page, QString(), 12, 500, color::textTertiary);
	v->addWidget(presence_);

	auto *stage = new Surface(2, page);
	auto *s = new QVBoxLayout(stage);
	s->setSpacing(size::gapSmall);
	stage->setPadding(12, 10, 12, 12);
	search_ = new QLineEdit(stage);
	search_->setPlaceholderText(lt("Laycue.Lyrics.SearchHint"));
	search_->setAccessibleName(lt("Laycue.Lyrics.SearchHint"));
	QFont big = search_->font();
	big.setPixelSize(17);
	search_->setFont(big);
	search_->setMinimumHeight(size::touch);
	search_->installEventFilter(this);
	connect(search_, &QLineEdit::textEdited, this, &LyricsDock::searched);
	connect(search_, &QLineEdit::returnPressed, this, [this]() {
		const std::vector<size_t> found = searchSongs(songs_, search_->text().toStdString(), 1);
		if (!found.empty())
			openSong(found.front());
		else if (!search_->text().trimmed().isEmpty())
			searchOnline(); // not in your songs: look online
	});
	s->addWidget(search_);
	resultsBox_ = new QWidget(stage);
	resultsLayout_ = new QVBoxLayout(resultsBox_);
	resultsLayout_->setContentsMargins(0, 0, 0, 0);
	resultsLayout_->setSpacing(4);
	s->addWidget(resultsBox_);

	songTitle_ = innerLabel(stage, QString(), 12, 700, color::overlay);
	s->addWidget(songTitle_);
	// The song's lines sit in a box of their own with its own scrolling, so the buttons and the look editor
	// stay in reach however long the song is. Drag the bar under it to make it taller or shorter.
	auto *slidesBox = new QWidget();
	slidesLayout_ = new QVBoxLayout(slidesBox);
	slidesLayout_->setContentsMargins(0, 0, 4, 0);
	slidesLayout_->setSpacing(4);
	slidesScroll_ = new QScrollArea(stage);
	slidesScroll_->setWidgetResizable(true);
	slidesScroll_->setFrameShape(QFrame::NoFrame);
	slidesScroll_->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
	slidesScroll_->setWidget(slidesBox);
	slidesScroll_->setFixedHeight(260);
	slidesScroll_->viewport()->setAutoFillBackground(false);
	slidesBox->setAutoFillBackground(false);
	s->addWidget(slidesScroll_);
	s->addWidget(new HeightGrip(slidesScroll_, stage));

	auto *buttons = new QHBoxLayout();
	buttons->setSpacing(size::gapSmall);
	auto *previous = button(stage, QStringLiteral("◀"), "laycueSecondary");
	previous->setAccessibleName(lt("Laycue.Lyrics.Previous"));
	previous->setToolTip(lt("Laycue.Lyrics.Previous"));
	connect(previous, &QAbstractButton::clicked, this, [this]() { step(-1); });
	buttons->addWidget(previous);
	showButton_ = button(stage, lt("Laycue.Lyrics.Show"), "laycuePrimary");
	showButton_->setMinimumHeight(size::touch);
	connect(showButton_, &QAbstractButton::clicked, this, &LyricsDock::toggleShown);
	buttons->addWidget(showButton_, 1);
	auto *next = button(stage, QStringLiteral("▶"), "laycueSecondary");
	next->setAccessibleName(lt("Laycue.Lyrics.Next"));
	next->setToolTip(lt("Laycue.Lyrics.Next"));
	connect(next, &QAbstractButton::clicked, this, [this]() { step(1); });
	buttons->addWidget(next);
	s->addLayout(buttons);
	v->addWidget(stage);

	// The song itself: write a new one, change this one, or bring one in from a file.
	auto *songRow = new QHBoxLayout();
	songRow->setSpacing(size::gapSmall);
	auto *newSong = button(page, lt("Laycue.Lyrics.New"), "laycueSecondary");
	connect(newSong, &QAbstractButton::clicked, this, [this]() { edit(true); });
	songRow->addWidget(newSong);
	auto *editSong = button(page, lt("Laycue.Lyrics.Edit"), "laycueSecondary");
	connect(editSong, &QAbstractButton::clicked, this, [this]() { edit(false); });
	songRow->addWidget(editSong);
	auto *import = button(page, lt("Laycue.Lyrics.Import"), "laycueSecondary");
	connect(import, &QAbstractButton::clicked, this, &LyricsDock::importSong);
	songRow->addWidget(import);
	v->addLayout(songRow);

	v->addWidget(label(page, lt("Laycue.Lyrics.Look"), 11, 600, color::textTertiary));
	// Packs: sets of templates that suit one theme (the same themes as Scripture's).
	packs_ = new Segmented(page);
	QStringList packNames;
	for (int p = 0; p < kLyricsPackCount; ++p)
		packNames << lt(lyricsPackKey(p));
	packs_->setItems(packNames);
	connect(packs_, &Segmented::changed, this, [this](int pack) {
		const std::vector<int> members = templatesOfPack(pack);
		if (!members.empty())
			useTemplate(members.front());
	});
	v->addWidget(packs_);
	templates_ = new Segmented(page);
	connect(templates_, &Segmented::changed, this, [this](int position) {
		if (position >= 0 && position < int(packMembers_.size()))
			useTemplate(packMembers_[size_t(position)]);
	});
	v->addWidget(templates_);
	showTemplates();
	editRow_ = new RowButton(page);
	editRow_->setTitle(lt("Laycue.Slides.EditLook"));
	editRow_->setOpen(true);
	v->addWidget(editRow_);
	slides::StyleEditor::Options options;
	options.showCase = true;
	options.decors = {int(slides::Decor::None), int(slides::Decor::Notes), int(slides::Decor::Equalizer),
			  int(slides::Decor::Vinyl)};
	options.accentLabel = lt("Laycue.Lyrics.SongNameColour");
	editor_ = new slides::StyleEditor(options, page);
	editor_->setStyle(settings_.styles[size_t(settings_.templateIndex)]);
	connect(editor_, &slides::StyleEditor::changed, this, [this](const slides::SlideStyle &style) {
		settings_.styles[size_t(settings_.templateIndex)] = style;
		saveSettings(settings_);
		push();
	});
	connect(editor_, &slides::StyleEditor::resetRequested, this, [this]() {
		settings_.styles[size_t(settings_.templateIndex)] =
			lyricsTemplates()[size_t(settings_.templateIndex)].style;
		editor_->setStyle(settings_.styles[size_t(settings_.templateIndex)]);
		saveSettings(settings_);
		push();
	});
	connect(editRow_, &RowButton::clicked, this, [this]() {
		editor_->setVisible(!editor_->isVisible());
		editRow_->setOpen(editor_->isVisible());
	});
	v->addWidget(editor_);
	// Songs can sit on the same background as the verses.
	auto *matchScripture = button(page, lt("Laycue.Lyrics.UseScriptureBackground"), "laycueSecondary");
	connect(matchScripture, &QAbstractButton::clicked, this, [this]() {
		slides::SlideStyle scripture;
		if (!scriptureBackground(scripture)) {
			toast(lt("Laycue.Lyrics.NoScriptureBackground"));
			return;
		}
		slides::SlideStyle &mine = settings_.styles[size_t(settings_.templateIndex)];
		slides::copyBackground(scripture, mine);
		mine.layout = scripture.layout;
		editor_->setStyle(mine);
		saveSettings(settings_);
		push();
		toast(lt("Laycue.Lyrics.ScriptureBackgroundUsed"));
	});
	v->addWidget(matchScripture);
	v->addWidget(new UpdateCard(updater_, page));
	v->addStretch(1);
	return scroll;
}

QWidget *LyricsDock::buildEditor()
{
	QWidget *page = nullptr;
	QScrollArea *scroll = scrolling(this, page);
	auto *v = new QVBoxLayout(page);
	v->setContentsMargins(12, 12, 12, 12);
	v->setSpacing(size::gapSmall + 2);
	auto *back = button(page, lt("Laycue.Lyrics.Back"), "laycueLink");
	connect(back, &QAbstractButton::clicked, this, [this]() { stack_->setCurrentIndex(0); });
	v->addWidget(back, 0, Qt::AlignLeft);
	v->addWidget(innerLabel(page, lt("Laycue.Lyrics.SongTitle"), 12, 600, color::textSecondary));
	editTitle_ = new QLineEdit(page);
	editTitle_->setAccessibleName(lt("Laycue.Lyrics.SongTitle"));
	v->addWidget(editTitle_);
	v->addWidget(innerLabel(page, lt("Laycue.Lyrics.SongAuthor"), 12, 600, color::textSecondary));
	editAuthor_ = new QLineEdit(page);
	editAuthor_->setAccessibleName(lt("Laycue.Lyrics.SongAuthor"));
	v->addWidget(editAuthor_);
	v->addWidget(innerLabel(page, lt("Laycue.Lyrics.SongWords"), 12, 600, color::textSecondary));
	editText_ = new QPlainTextEdit(page);
	editText_->setAccessibleName(lt("Laycue.Lyrics.SongWords"));
	editText_->setMinimumHeight(320);
	editText_->setPlaceholderText(
		lt("Laycue.Lyrics.WordsHint").replace(QStringLiteral("\\n"), QStringLiteral("\n")));
	v->addWidget(editText_, 1);
	v->addWidget(innerLabel(page, lt("Laycue.Lyrics.WordsHelp"), 11, 400, color::textTertiary));
	auto *row = new QHBoxLayout();
	row->setSpacing(size::gapSmall);
	auto *saveButton = button(page, lt("Laycue.Lyrics.Save"), "laycuePrimary");
	saveButton->setMinimumHeight(size::touch);
	connect(saveButton, &QAbstractButton::clicked, this, &LyricsDock::saveEdit);
	row->addWidget(saveButton, 1);
	deleteButton_ = button(page, lt("Laycue.Lyrics.Delete"), "laycueDanger");
	connect(deleteButton_, &QAbstractButton::clicked, this, &LyricsDock::deleteSong);
	row->addWidget(deleteButton_);
	v->addLayout(row);
	return scroll;
}

// ---- Finding a song ----------------------------------------------------------------------------------------

bool LyricsDock::eventFilter(QObject *watched, QEvent *event)
{
	if (watched == search_ && event->type() == QEvent::KeyPress) {
		const int key = static_cast<QKeyEvent *>(event)->key();
		if (key == Qt::Key_Down) {
			step(1);
			return true;
		}
		if (key == Qt::Key_Up) {
			step(-1);
			return true;
		}
	}
	return QWidget::eventFilter(watched, event);
}

void LyricsDock::searched()
{
	rebuildResults();
}

void LyricsDock::rebuildResults()
{
	clear(resultsLayout_);
	const std::string query = search_->text().toStdString();
	// While nothing is typed and a song is open, the panel shows the song, not a list.
	if (query.empty() && songIndex() >= 0) {
		resultsBox_->hide();
		return;
	}
	resultsBox_->show();
	const std::vector<size_t> found = searchSongs(songs_, query, 6);
	if (found.empty() && query.empty())
		resultsLayout_->addWidget(
			innerLabel(resultsBox_, lt("Laycue.Lyrics.NoMatch"), 11, 400, color::textTertiary));
	for (size_t index : found) {
		auto *row = new RowButton(resultsBox_);
		row->setTitle(qs(songs_[index].title));
		row->setValue(qs(songs_[index].author));
		connect(row, &RowButton::clicked, this, [this, index]() { openSong(index); });
		resultsLayout_->addWidget(row);
	}
	if (query.empty())
		return;

	// Online: a button to look, then what came back.
	const QString typed = search_->text().trimmed();
	if (onlineQuery_ != typed || (!onlineBusy_ && onlineResults_.empty() && onlineError_.isEmpty())) {
		auto *look = new RowButton(resultsBox_);
		look->setTitle(lt("Laycue.Lyrics.SearchOnline").arg(typed));
		look->setValue(lt("Laycue.Lyrics.Online"));
		connect(look, &RowButton::clicked, this, &LyricsDock::searchOnline);
		resultsLayout_->addWidget(look);
		return;
	}
	resultsLayout_->addWidget(
		innerLabel(resultsBox_, lt("Laycue.Lyrics.OnlineHeader"), 10, 700, color::textTertiary));
	if (onlineBusy_) {
		resultsLayout_->addWidget(
			innerLabel(resultsBox_, lt("Laycue.Lyrics.OnlineSearching"), 11, 400, color::textTertiary));
		return;
	}
	if (!onlineError_.isEmpty()) {
		resultsLayout_->addWidget(innerLabel(resultsBox_, onlineError_, 11, 500, color::overlay));
		addWebRow(typed);
		return;
	}
	for (size_t i = 0; i < onlineResults_.size(); ++i) {
		auto *row = new RowButton(resultsBox_);
		row->setTitle(qs(onlineResults_[i].title));
		row->setValue(qs(onlineResults_[i].artist));
		row->setToolTip(lt("Laycue.Lyrics.OnlineAddTip") + QStringLiteral(" (") +
				(onlineResults_[i].source == OnlineSource::LyricsOvh ? QStringLiteral("lyrics.ovh")
										     : QStringLiteral("LRCLIB")) +
				QStringLiteral(")"));
		connect(row, &RowButton::clicked, this, [this, i]() { addOnline(i); });
		resultsLayout_->addWidget(row);
	}
	if (onlineResults_.empty())
		resultsLayout_->addWidget(
			innerLabel(resultsBox_, lt("Laycue.Lyrics.OnlineNone"), 11, 400, color::textTertiary));
	resultsLayout_->addWidget(
		innerLabel(resultsBox_, lt("Laycue.Lyrics.OnlineLicence"), 10, 400, color::textTertiary));
	addWebRow(typed);
}

// The big lyrics sites have no free way for a program to read their words, so they open in the browser: copy the
// words there and press Paste, which makes a new song from them.
void LyricsDock::addWebRow(const QString &query)
{
	resultsLayout_->addWidget(innerLabel(resultsBox_, lt("Laycue.Lyrics.WebHint"), 10, 600, color::textTertiary));
	auto *row = new QWidget(resultsBox_);
	auto *h = new QHBoxLayout(row);
	h->setContentsMargins(0, 0, 0, 0);
	h->setSpacing(4);
	const QByteArray words = QUrl::toPercentEncoding(query);
	const QByteArray withLyrics = QUrl::toPercentEncoding(query + QStringLiteral(" lyrics"));
	const std::vector<std::pair<QString, QString>> sites = {
		{QStringLiteral("Google"),
		 QStringLiteral("https://www.google.com/search?q=") + QString::fromLatin1(withLyrics)},
		{QStringLiteral("Genius"), QStringLiteral("https://genius.com/search?q=") + QString::fromLatin1(words)},
		{QStringLiteral("Musixmatch"),
		 QStringLiteral("https://www.musixmatch.com/search/") + QString::fromLatin1(words)},
		{QStringLiteral("AZLyrics"),
		 QStringLiteral("https://search.azlyrics.com/search.php?q=") + QString::fromLatin1(words)},
	};
	for (const auto &site : sites) {
		auto *open = button(row, site.first, "laycueSecondary");
		const QString address = site.second;
		connect(open, &QAbstractButton::clicked, this,
			[address]() { QDesktopServices::openUrl(QUrl(address)); });
		h->addWidget(open);
	}
	resultsLayout_->addWidget(row);
	auto *paste = button(resultsBox_, lt("Laycue.Lyrics.PasteWords"), "laycuePrimary");
	connect(paste, &QAbstractButton::clicked, this, [this, query]() { pasteWords(query); });
	resultsLayout_->addWidget(paste);
}

void LyricsDock::pasteWords(const QString &title)
{
	const QString words = QGuiApplication::clipboard()->text().trimmed();
	if (words.isEmpty()) {
		toast(lt("Laycue.Lyrics.PasteNothing"));
		return;
	}
	edit(true);
	editTitle_->setText(title);
	editText_->setPlainText(words);
}

void LyricsDock::searchOnline()
{
	const QString query = search_->text().trimmed();
	if (query.isEmpty())
		return;
	onlineQuery_ = query;
	onlineBusy_ = true;
	onlineError_.clear();
	onlineResults_.clear();
	rebuildResults();
	online_->search(query);
}

void LyricsDock::addOnline(size_t index)
{
	if (index >= onlineResults_.size())
		return;
	const OnlineSong found = onlineResults_[index];
	if (found.needsWords) {
		toast(lt("Laycue.Lyrics.OnlineGettingWords").arg(qs(found.title)));
		online_->fetchWords(found); // addFound runs when they arrive
		return;
	}
	addFound(found);
}

void LyricsDock::addFound(const OnlineSong &found)
{
	Song song = songFromOnline(found);
	if (song.sections.empty()) {
		toast(lt("Laycue.Lyrics.OnlineNone"));
		return;
	}
	// Already in your songs (same name and singer): open that one, with any changes you made.
	for (size_t i = 0; i < songs_.size(); ++i) {
		if (songs_[i].title == song.title && songs_[i].author == song.author) {
			openSong(i);
			return;
		}
	}
	song.id = makeSongId(song.title, idsOf(songs_));
	songs_.push_back(song);
	saveLibrary(songs_);
	onlineQuery_.clear();
	onlineResults_.clear();
	openSong(songs_.size() - 1);
	toast(lt("Laycue.Lyrics.OnlineAdded").arg(qs(song.title)));
}

void LyricsDock::openSong(size_t index)
{
	if (index >= songs_.size())
		return;
	const Song &song = songs_[index];
	songId_ = song.id;
	slides_ = slidesFor(song, 2);
	slide_ = slides_.empty() ? -1 : 0;
	settings_.lastSong = song.id;
	saveSettings(settings_);
	songTitle_->setText(song.author.empty() ? qs(song.title) : qs(song.title + " \xC2\xB7 " + song.author));
	search_->clear();
	rebuildResults();
	rebuildSlides();
	push(); // a song opened while lyrics are up replaces what is on screen
	updateStatus();
}

int LyricsDock::songIndex() const
{
	for (size_t i = 0; i < songs_.size(); ++i) {
		if (songs_[i].id == songId_)
			return int(i);
	}
	return -1;
}

// ---- Showing -----------------------------------------------------------------------------------------------

void LyricsDock::rebuildSlides()
{
	const int keepScroll = slidesScroll_->verticalScrollBar()->value();
	QPointer<QWidget> current;
	clear(slidesLayout_);
	auto *parent = slidesLayout_->parentWidget();
	if (slides_.empty()) {
		songTitle_->setText(songIndex() < 0 ? lt("Laycue.Lyrics.PickASong") : songTitle_->text());
		return;
	}
	std::string lastLabel;
	for (size_t i = 0; i < slides_.size(); ++i) {
		const LyricSlide &slide = slides_[i];
		if (slide.label != lastLabel && !slide.label.empty())
			slidesLayout_->addWidget(
				innerLabel(parent, qs(slide.label).toUpper(), 10, 700, color::textTertiary));
		lastLabel = slide.label;
		auto *row = new RowButton(parent);
		const bool here = int(i) == slide_;
		const QString mark = here ? (shown_ ? QStringLiteral("● ") : QStringLiteral("○ ")) : QString();
		QString text = qs(slide.text);
		text.replace(QLatin1Char('\n'), QStringLiteral(" / "));
		row->setTitle(mark + text);
		row->setToolTip(qs(slide.text));
		if (here)
			current = row;
		connect(row, &RowButton::clicked, this, [this, i]() { showSlide(int(i)); });
		slidesLayout_->addWidget(row);
	}
	// Keep the list where it was, and bring the line on screen into view (Next can move past the edge).
	QTimer::singleShot(0, slidesScroll_, [this, keepScroll, current]() {
		slidesScroll_->verticalScrollBar()->setValue(keepScroll);
		if (current)
			slidesScroll_->ensureWidgetVisible(current, 0, 24);
	});
}

void LyricsDock::showSlide(int index)
{
	if (index < 0 || index >= int(slides_.size()))
		return;
	slide_ = index;
	push();
	rebuildSlides();
	updateStatus();
	if (shown_)
		setFocus(); // so Left and Right step through the lines straight away
}

void LyricsDock::push()
{
	if (slide_ < 0 || slide_ >= int(slides_.size()))
		return;
	if (shown_ && sourceName_.empty()) {
		std::string error;
		sourceName_ = slides::ensureSource(kSourceId, obs_module_text("Laycue.Lyrics.SourceName"), error);
		if (sourceName_.empty()) {
			toast(qs(error));
			shown_ = false;
			return;
		}
	}
	if (sourceName_.empty())
		return;
	const int index = songIndex();
	slides::Slide slide;
	slide.text = slides_[size_t(slide_)].text;
	slide.style = settings_.styles[size_t(settings_.templateIndex)];
	// The song's name goes with a background; words alone stay words alone.
	slide.caption = index >= 0 && slide.style.fill != int(slides::Fill::None) ? songs_[size_t(index)].title
										  : std::string();
	slide.shown = shown_;
	if (shown_)
		slides::hideOthers(kSourceId); // a verse and a song never stack
	slides::send(sourceName_, slide);
}

void LyricsDock::toggleShown()
{
	if (slide_ < 0) {
		toast(lt("Laycue.Lyrics.PickASong"));
		return;
	}
	shown_ = !shown_;
	push();
	rebuildSlides();
	updateStatus();
	if (shown_)
		setFocus();
}

void LyricsDock::step(int direction)
{
	if (slides_.empty())
		return;
	const int next = std::clamp(slide_ + direction, 0, int(slides_.size()) - 1);
	if (next != slide_)
		showSlide(next);
}

void LyricsDock::runHotkey(int index)
{
	try {
		if (index == 0)
			toggleShown();
		else if (index == 1)
			step(1);
		else if (index == 2)
			step(-1);
	} catch (...) {
		obs_log(LOG_ERROR, "Laycue Lyrics: error running a hotkey");
	}
}

// ---- Writing and changing songs ----------------------------------------------------------------------------

void LyricsDock::edit(bool newSong)
{
	const int index = songIndex();
	editingNew_ = newSong || index < 0;
	if (editingNew_) {
		editTitle_->clear();
		editAuthor_->clear();
		editText_->clear();
	} else {
		const Song &song = songs_[size_t(index)];
		editTitle_->setText(qs(song.title));
		editAuthor_->setText(qs(song.author));
		editText_->setPlainText(qs(songText(song)));
	}
	deleteButton_->setVisible(!editingNew_);
	stack_->setCurrentIndex(1);
	editTitle_->setFocus();
}

void LyricsDock::saveEdit()
{
	const std::string title = editTitle_->text().trimmed().toStdString();
	if (title.empty()) {
		toast(lt("Laycue.Lyrics.NeedTitle"));
		return;
	}
	Song song = parseSongText(title, editAuthor_->text().toStdString(), editText_->toPlainText().toStdString());
	if (song.sections.empty()) {
		toast(lt("Laycue.Lyrics.NeedWords"));
		return;
	}
	const int index = songIndex();
	if (editingNew_ || index < 0) {
		song.id = makeSongId(song.title, idsOf(songs_));
		songs_.push_back(song);
	} else {
		song.id = songs_[size_t(index)].id;
		songs_[size_t(index)] = song;
	}
	saveLibrary(songs_);
	stack_->setCurrentIndex(0);
	for (size_t i = 0; i < songs_.size(); ++i) {
		if (songs_[i].id == song.id)
			openSong(i);
	}
	toast(lt("Laycue.Lyrics.Saved").arg(qs(song.title)));
}

void LyricsDock::deleteSong()
{
	const int index = songIndex();
	if (index < 0)
		return;
	const QString title = qs(songs_[size_t(index)].title);
	if (QMessageBox::question(this, lt("Laycue.Lyrics.Delete"), lt("Laycue.Lyrics.DeleteAsk").arg(title)) !=
	    QMessageBox::Yes)
		return;
	songs_.erase(songs_.begin() + index);
	saveLibrary(songs_);
	songId_.clear();
	slides_.clear();
	slide_ = -1;
	songTitle_->setText(lt("Laycue.Lyrics.PickASong"));
	stack_->setCurrentIndex(0);
	rebuildSlides();
	rebuildResults();
}

void LyricsDock::importSong()
{
	const QStringList files = QFileDialog::getOpenFileNames(this, lt("Laycue.Lyrics.Import"), QString(),
								lt("Laycue.Lyrics.SongFiles"));
	int added = 0;
	size_t last = 0;
	for (const QString &file : files) {
		Song song;
		std::string error;
		if (!importSongFile(file.toStdString(), song, error)) {
			toast(lt("Laycue.Lyrics.ImportFailed").arg(QFileInfo(file).fileName(), qs(error)));
			continue;
		}
		song.id = makeSongId(song.title, idsOf(songs_));
		songs_.push_back(song);
		last = songs_.size() - 1;
		++added;
	}
	if (added == 0)
		return;
	saveLibrary(songs_);
	openSong(last);
	toast(lt("Laycue.Lyrics.Imported").arg(added));
}

void LyricsDock::useTemplate(int index)
{
	if (index < 0 || index >= int(settings_.styles.size()))
		return;
	settings_.templateIndex = index;
	editor_->setStyle(settings_.styles[size_t(index)]);
	showTemplates();
	saveSettings(settings_);
	push();
}

// Shows the pack the chosen template belongs to, and that pack's templates.
void LyricsDock::showTemplates()
{
	const int index = std::clamp(settings_.templateIndex, 0, int(lyricsTemplates().size()) - 1);
	const int pack = lyricsTemplates()[size_t(index)].pack;
	packMembers_ = templatesOfPack(pack);
	QStringList names;
	int position = 0;
	for (size_t i = 0; i < packMembers_.size(); ++i) {
		names << lt(lyricsTemplates()[size_t(packMembers_[i])].nameKey);
		if (packMembers_[i] == index)
			position = int(i);
	}
	packs_->blockSignals(true);
	templates_->blockSignals(true);
	packs_->setCurrent(pack);
	templates_->setItems(names);
	templates_->setCurrent(position);
	packs_->blockSignals(false);
	templates_->blockSignals(false);
}

void LyricsDock::updateStatus()
{
	// The other panel (or the layer's Properties) may have hidden it: the layer says what is true.
	if (!sourceName_.empty() && shown_ && !slides::isShown(sourceName_)) {
		shown_ = false;
		rebuildSlides();
	}
	if (!sourceName_.empty()) {
		bool live = false;
		presence_->setText(presenceText(presenceOf(sourceName_), live));
		presence_->setStyleSheet(labelStyle(live ? color::next : color::textTertiary, presence_->font()));
	} else {
		const std::vector<std::string> existing = slides::sourcesOf(kSourceId);
		if (!existing.empty())
			sourceName_ = existing.front();
		presence_->setText(sourceName_.empty() ? lt("Laycue.Lyrics.NoLayerYet") : QString());
	}
	status_->setText(shown_ ? lt("Laycue.Lyrics.OnScreen") : lt("Laycue.Lyrics.NotShowing"));
	status_->setStyleSheet(labelStyle(shown_ ? color::live : color::textTertiary, status_->font()));
	showButton_->setText(shown_ ? lt("Laycue.Lyrics.Hide") : lt("Laycue.Lyrics.Show"));
}

void LyricsDock::toast(const QString &text)
{
	toast_->setText(text);
	toast_->show();
	toastTimer_.start(2600 + 45 * int(text.length())); // longer messages stay up longer
}

} // namespace laycue::lyrics
