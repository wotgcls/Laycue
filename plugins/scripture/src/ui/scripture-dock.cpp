#include "ui/scripture-dock.hpp"

#include "core/books.hpp"
#include "core/reference.hpp"
#include "core/templates.hpp"
#include "obs/scene-presence.hpp"
#include "slides/slide-control.hpp"
#include "slides/slide-render.hpp"
#include "slides/style-editor.hpp"
#include "ui/brand-mark.hpp"
#include "ui/controls.hpp"
#include "ui/pack-switcher.hpp"
#include "ui/pages/helpers.hpp"
#include "ui/presence-text.hpp"
#include "ui/reference-box.hpp"
#include "ui/style.hpp"
#include "ui/text.hpp"
#include "ui/tokens.hpp"
#include "ui/update-card.hpp"

#include <obs-module.h>
#include <plugin-support.h>

#include <QFileDialog>
#include <QFileInfo>
#include <QHBoxLayout>
#include <QKeyEvent>
#include <QLabel>
#include <QLineEdit>
#include <QMenu>
#include <QScrollArea>
#include <QVBoxLayout>

#include <QKeySequence>
#include <QShortcut>

#include <algorithm>
#include <utility>

namespace laycue::scripture {

using namespace laycue::ui;
using namespace laycue::ui::page;

namespace {

constexpr const char *kSourceId = "laycue_scripture";

QString qs(const std::string &s)
{
	return QString::fromStdString(s);
}

} // namespace

// What the operator has typed, in whichever box the chosen mode shows.
std::string ScriptureDock::phrase() const
{
	return searchMode_ ? searchInput_->text().toStdString() : input_->text().toStdString();
}

ScriptureDock::ScriptureDock(QWidget *parent) : QWidget(parent)
{
	setObjectName("LaycueScriptureDock");
	setMinimumWidth(340);
	// Left and Right step through the verses or lines while the panel has focus (text boxes keep them for typing).
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
	build();

	// The chosen version, or the first one found (the bundled King James Version).
	const std::vector<VersionFile> versions = listVersions();
	bool loaded = false;
	for (const VersionFile &v : versions) {
		if (v.file == settings_.version)
			loaded = useVersion(v);
	}
	if (!loaded && !versions.empty())
		useVersion(versions.front());
	if (!settings_.lastTyped.empty()) {
		if (searchMode_)
			searchInput_->setText(qs(settings_.lastTyped));
		else
			input_->setText(qs(settings_.lastTyped));
		typed();
	}

	toastTimer_.setSingleShot(true);
	toastTimer_.setInterval(2600);
	connect(&toastTimer_, &QTimer::timeout, this, [this]() { toast_->hide(); });
	searchTimer_.setSingleShot(true);
	searchTimer_.setInterval(350); // a moment after the last key, so typing feels light
	connect(&searchTimer_, &QTimer::timeout, this, &ScriptureDock::search);
	searchMode_ = settings_.searchMode;
	pollTimer_.setInterval(1000);
	connect(&pollTimer_, &QTimer::timeout, this, &ScriptureDock::updateStatus);
	pollTimer_.start();
	updateStatus();
}

ScriptureDock::~ScriptureDock() = default;

void ScriptureDock::build()
{
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
	contentLayout->addWidget(packHeader(content)); // stays on top while the page scrolls
	auto *scroll = new QScrollArea(content);
	scroll->setWidgetResizable(true);
	scroll->setFrameShape(QFrame::NoFrame);
	scroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
	contentLayout->addWidget(scroll);
	toast_ = innerLabel(content, QString(), 12, 500, color::cream);
	toast_->setStyleSheet(QStringLiteral("background: %1; color: %2; border-radius: 16px; padding: 8px 14px;")
				      .arg(hex(color::control), hex(color::cream)));
	toast_->hide();
	contentLayout->addWidget(toast_, 0, Qt::AlignHCenter);

	auto *page = new QWidget(scroll);
	scroll->setWidget(page);
	auto *v = new QVBoxLayout(page);
	v->setContentsMargins(8, 8, 8, 12);
	v->setSpacing(size::gapSmall + 2);

	auto *header = new QHBoxLayout();
	header->setSpacing(size::gapSmall);
	header->addWidget(innerLabel(page, lt("Laycue.Scripture.Heading"), 17, 700, color::cream));
	header->addStretch(1);
	status_ = innerLabel(page, QString(), 12, 600, color::textTertiary);
	status_->setWordWrap(false);
	header->addWidget(status_);
	v->addLayout(header);
	presence_ = innerLabel(page, QString(), 12, 500, color::textTertiary);
	v->addWidget(presence_);

	// The working card: version, the type box, the verse and its neighbours, the main buttons.
	auto *stage = new Surface(2, page);
	auto *s = new QVBoxLayout(stage);
	s->setSpacing(size::gapSmall);
	stage->setPadding(12, 10, 12, 12);
	versionRow_ = new RowButton(stage);
	versionRow_->setTitle(lt("Laycue.Scripture.Version"));
	versionRow_->setOpen(true);
	connect(versionRow_, &RowButton::clicked, this, &ScriptureDock::chooseVersion);
	s->addWidget(versionRow_);

	// The type box works in either of two ways: a quick reference ("jn 3 16") or a word search.
	mode_ = new Segmented(stage);
	mode_->setItems({lt("Laycue.Scripture.ModeReference"), lt("Laycue.Scripture.ModeSearch")});
	mode_->setCurrent(settings_.searchMode ? 1 : 0);
	mode_->setAccessibleName(lt("Laycue.Scripture.Mode.Accessible"));
	mode_->setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Fixed);
	mode_->setFixedWidth(176);
	connect(mode_, &Segmented::changed, this, &ScriptureDock::chooseMode);
	s->addWidget(mode_);

	// Search mode's own plain box: words or a sentence, nothing about chapters and verses.
	searchInput_ = new QLineEdit(stage);
	searchInput_->setPlaceholderText(lt("Laycue.Scripture.SearchPlaceholder"));
	searchInput_->setAccessibleName(lt("Laycue.Scripture.SearchPlaceholder"));
	searchInput_->setClearButtonEnabled(true);
	{
		QFont f = searchInput_->font();
		f.setPixelSize(17);
		f.setWeight(QFont::DemiBold);
		searchInput_->setFont(f);
		searchInput_->setStyleSheet(
			QStringLiteral("QLineEdit { background: %1; color: %2; border: 1px solid %3;"
				       " border-radius: %4px; padding: 0 12px; font-size: 17px; }"
				       "QLineEdit:focus { border: 2px solid %5; padding: 0 11px; }")
				.arg(hex(color::recess), hex(color::cream), hex(color::lineStrong))
				.arg(size::radius)
				.arg(hex(color::setup)));
	}
	searchInput_->setMinimumHeight(size::touch);
	connect(searchInput_, &QLineEdit::textEdited, this, &ScriptureDock::typed);
	connect(searchInput_, &QLineEdit::returnPressed, this, [this]() {
		searchTimer_.stop();
		search();
	});
	searchInput_->setVisible(settings_.searchMode);
	input_->setVisible(!settings_.searchMode);
	s->addWidget(searchInput_);

	input_ = new ReferenceBox(stage);
	connect(input_, &ReferenceBox::edited, this, &ScriptureDock::typed);
	connect(input_, &ReferenceBox::submitted, this, &ScriptureDock::go);
	connect(input_, &ReferenceBox::stepped, this, &ScriptureDock::step);
	s->addWidget(input_);
	hint_ = innerLabel(stage, QString(), 12, 500, color::textTertiary);
	s->addWidget(hint_);

	referenceLabel_ = innerLabel(stage, QString(), 12, 700, color::overlay);
	s->addWidget(referenceLabel_);
	versesBox_ = new QWidget(stage);
	versesLayout_ = new QVBoxLayout(versesBox_);
	versesLayout_->setContentsMargins(0, 0, 0, 0);
	versesLayout_->setSpacing(4);
	s->addWidget(versesBox_);

	auto *buttons = new QHBoxLayout();
	buttons->setSpacing(size::gapSmall);
	auto *previous = button(stage, QStringLiteral("◀"), "laycueSecondary");
	previous->setAccessibleName(lt("Laycue.Scripture.Previous"));
	previous->setToolTip(lt("Laycue.Scripture.Previous"));
	connect(previous, &QAbstractButton::clicked, this, [this]() { step(-1); });
	buttons->addWidget(previous);
	showButton_ = button(stage, lt("Laycue.Scripture.Show"), "laycuePrimary");
	showButton_->setMinimumHeight(size::touch);
	connect(showButton_, &QAbstractButton::clicked, this, &ScriptureDock::toggleShown);
	buttons->addWidget(showButton_, 1);
	auto *next = button(stage, QStringLiteral("▶"), "laycueSecondary");
	next->setAccessibleName(lt("Laycue.Scripture.Next"));
	next->setToolTip(lt("Laycue.Scripture.Next"));
	connect(next, &QAbstractButton::clicked, this, [this]() { step(1); });
	buttons->addWidget(next);
	s->addLayout(buttons);
	v->addWidget(stage);

	// How it looks on screen.
	v->addWidget(label(page, lt("Laycue.Scripture.Look"), 11, 600, color::textTertiary));
	// Packs: sets of templates that suit one theme. Choose a pack, then one of its templates.
	packs_ = new Segmented(page);
	QStringList packNames;
	for (int p = 0; p < kScripturePackCount; ++p)
		packNames << lt(scripturePackKey(p));
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
	// Every template can be changed: background (colour, gradient or picture), colours and text size.
	editRow_ = new RowButton(page);
	editRow_->setTitle(lt("Laycue.Slides.EditLook"));
	editRow_->setOpen(true);
	v->addWidget(editRow_);
	slides::StyleEditor::Options options;
	options.accentLabel = lt("Laycue.Scripture.ReferenceColour");
	using slides::Decor;
	options.decors = {int(Decor::None), int(Decor::OpenBible), int(Decor::GlowBible), int(Decor::Scroll),
			  int(Decor::Dove), int(Decor::Cross),     int(Decor::Sunrise)};
	editor_ = new slides::StyleEditor(options, page);
	editor_->setStyle(settings_.styles[size_t(settings_.templateIndex)]);
	connect(editor_, &slides::StyleEditor::changed, this, [this](const slides::SlideStyle &style) {
		settings_.styles[size_t(settings_.templateIndex)] = style;
		save();
		push();
	});
	connect(editor_, &slides::StyleEditor::resetRequested, this, [this]() {
		settings_.styles[size_t(settings_.templateIndex)] =
			scriptureTemplates()[size_t(settings_.templateIndex)].style;
		editor_->setStyle(settings_.styles[size_t(settings_.templateIndex)]);
		save();
		push();
	});
	connect(editRow_, &RowButton::clicked, this, [this]() {
		editor_->setVisible(!editor_->isVisible());
		editRow_->setOpen(editor_->isVisible());
	});
	v->addWidget(editor_);

	// Saved scriptures: the ones read every week.
	auto *savedHeader = new QHBoxLayout();
	savedHeader->addWidget(label(page, lt("Laycue.Scripture.Saved"), 11, 600, color::textTertiary), 1);
	auto *saveThis = button(page, lt("Laycue.Scripture.SaveThis"), "laycueSecondary");
	connect(saveThis, &QAbstractButton::clicked, this, [this]() {
		if (!current_.valid()) {
			toast(lt("Laycue.Scripture.NothingToSave"));
			return;
		}
		Reference whole = current_;
		const std::string label = referenceLabel(whole);
		if (std::find(settings_.saved.begin(), settings_.saved.end(), label) == settings_.saved.end()) {
			settings_.saved.push_back(label);
			save();
			rebuildSaved();
		}
		toast(lt("Laycue.Scripture.SavedToast").arg(qs(label)));
	});
	savedHeader->addWidget(saveThis);
	v->addLayout(savedHeader);
	auto *savedBox = new QWidget(page);
	savedLayout_ = new QVBoxLayout(savedBox);
	savedLayout_->setContentsMargins(0, 0, 0, 0);
	savedLayout_->setSpacing(4);
	v->addWidget(savedBox);
	rebuildSaved();
	v->addWidget(new UpdateCard(updater_, page));
	v->addStretch(1);
}

// ---- Typing ------------------------------------------------------------------------------------------------

void ScriptureDock::chooseMode(int index)
{
	const bool search = index == 1;
	settings_.searchMode = search;
	save();
	searchMode_ = search;
	input_->setVisible(!searchMode_); // the framed reference box, or the plain words box
	searchInput_->setVisible(searchMode_);
	(searchMode_ ? static_cast<QWidget *>(searchInput_) : static_cast<QWidget *>(input_))->setFocus();
	searchTimer_.stop();
	searching_ = false; // the list below changes role: chapters again, or ready for results
	rebuildVerses();
	typed();            // the hint line follows the mode
}

void ScriptureDock::typed()
{
	if (searchMode_) {
		const bool empty = searchInput_->text().trimmed().isEmpty();
		hint_->setText(empty ? QString() : lt("Laycue.Scripture.HintTypedSearch"));
		if (empty)
			searchTimer_.stop();
		else
			searchTimer_.start(); // the moment the words stop arriving, it looks
		return;
	}
	std::string text = input_->text().toStdString();
	const std::string book = bookHint(text);
	Reference ref;
	if (!book.empty() && !input_->text().contains(QLatin1Char(' '))) {
		hint_->setText(lt("Laycue.Scripture.HintBook").arg(qs(book)));
	} else if (parseReference(text, ref) && ref.valid()) {
		hint_->setText(lt("Laycue.Scripture.HintGo").arg(qs(referenceLabel(ref))));
	} else {
		hint_->setText(text.empty() ? QString() : lt("Laycue.Scripture.HintSearch").arg(qs(text)));
	}
}

void ScriptureDock::go()
{
	if (searchMode_) {
		searchTimer_.stop();
		search();
		return;
	}
	Reference ref;
	if (!parseReference(input_->text().toStdString(), ref) || !ref.valid() || !bible_) {
		// Not a reference: what is typed might be a phrase to look for. Then word search is the mode,
		// and the words carry over to its plain box.
		searchInput_->setText(input_->text());
		chooseMode(1);
		searchTimer_.stop();
		search();
		return;
	}
	if (ref.verseFrom < 1)
		ref.verseFrom = 1;
	if (bible_->verse(ref.book, ref.chapter, ref.verseFrom).empty()) {
		toast(lt("Laycue.Scripture.NotInVersion").arg(qs(referenceLabel(ref)), qs(bible_->abbrev)));
		return;
	}
	settings_.lastTyped = input_->text().toStdString();
	save();
	shown_ = true; // Enter means "put it up"
	showVerse(ref);
	setFocus(); // so Left and Right step through the verses straight away
}

// ---- Showing -----------------------------------------------------------------------------------------------

// Every verse in the chosen version that holds the phrase, shown in place of the chapter list until one is picked.
void ScriptureDock::search()
{
	if (!bible_)
		return;
	if (searchInput_->text().trimmed().isEmpty() && input_->text().trimmed().isEmpty())
		return;
	searchQuery_ = phrase();
	settings_.lastTyped = searchQuery_;
	save();
	searchResults_ = searchBible(*bible_, searchQuery_, 9);
	searching_ = true;
	hint_->setText(QString());
	rebuildVerses();
	if (searchResults_.empty()) {
		toast(lt("Laycue.Scripture.SearchNone").arg(qs(searchQuery_), qs(bible_->abbrev)));
	} else {
		toast(lt("Laycue.Scripture.SearchFound").arg(int(searchResults_.size())).arg(qs(bible_->abbrev)));
	}
}

void ScriptureDock::showVerse(const Reference &ref)
{
	if (!searchMode_)
		searching_ = false; // in reference mode, a picked result is the ordinary chapter list again
	const bool sameChapter = ref.book == current_.book && ref.chapter == current_.chapter;
	current_ = ref;
	current_.verseTo = 0;
	input_->setReference(current_); // the box shows what is on screen
	push();
	if (!sameChapter)
		rebuildVerses();
	else
		rebuildVerses(); // the "On screen" mark moves
	updateStatus();
}

void ScriptureDock::push()
{
	if (!bible_ || !current_.valid() || current_.verseFrom < 1)
		return;
	if (shown_ && sourceName_.empty()) {
		std::string error;
		sourceName_ = slides::ensureSource(kSourceId, obs_module_text("Laycue.Scripture.SourceName"), error);
		if (sourceName_.empty()) {
			toast(qs(error));
			shown_ = false;
			return;
		}
	}
	if (sourceName_.empty())
		return;
	slides::Slide slide;
	slide.text = bible_->verse(current_.book, current_.chapter, current_.verseFrom);
	slide.caption = referenceLabel(current_) + " (" + bible_->abbrev + ")";
	slide.style = settings_.styles[size_t(settings_.templateIndex)];
	slide.shown = shown_;
	if (shown_)
		slides::hideOthers(kSourceId); // a verse and a song never stack
	slides::send(sourceName_, slide);
}

void ScriptureDock::toggleShown()
{
	if (!current_.valid()) {
		go();
		return;
	}
	shown_ = !shown_;
	push();
	rebuildVerses();
	updateStatus();
}

void ScriptureDock::step(int direction)
{
	if (searching_) {
		// Left and Right walk the search results.
		if (searchResults_.empty())
			return;
		int at = -1;
		for (size_t i = 0; i < searchResults_.size(); ++i) {
			if (searchResults_[i].book == current_.book && searchResults_[i].chapter == current_.chapter &&
			    searchResults_[i].verseFrom == current_.verseFrom)
				at = int(i);
		}
		const int next = at < 0 ? (direction > 0 ? 0 : int(searchResults_.size()) - 1)
					: std::clamp(at + direction, 0, int(searchResults_.size()) - 1);
		if (next != at)
			showVerse(searchResults_[size_t(next)]);
		return;
	}
	if (!bible_ || !current_.valid())
		return;
	Reference ref = current_;
	if (ref.verseFrom < 1)
		ref.verseFrom = 1;
	const bool moved = direction > 0 ? nextVerse(*bible_, ref) : previousVerse(*bible_, ref);
	if (!moved)
		return;
	showVerse(ref);
}

void ScriptureDock::runHotkey(int index)
{
	try {
		if (index == 0)
			toggleShown();
		else if (index == 1)
			step(1);
		else if (index == 2)
			step(-1);
	} catch (...) {
		obs_log(LOG_ERROR, "Laycue Scripture: error running a hotkey");
	}
}

// ---- Lists -------------------------------------------------------------------------------------------------

void ScriptureDock::rebuildVerses()
{
	clear(versesLayout_);
	if (searching_) {
		// What the phrase found, the verse itself as the row's title.
		referenceLabel_->setText(lt("Laycue.Scripture.SearchResults").arg(qs(searchQuery_),
										 qs(bible_ ? bible_->abbrev : "")));
		if (!bible_)
			return;
		if (searchResults_.empty())
			versesLayout_->addWidget(label(versesBox_, lt("Laycue.Scripture.SearchEmpty"), 11, 400,
						       color::textTertiary));
		for (const Reference &hit : searchResults_) {
			const QString text = qs(bible_->verse(hit.book, hit.chapter, hit.verseFrom));
			auto *row = new RowButton(versesBox_);
			row->setTitle(qs(referenceLabel(hit)) + "  " + text);
			row->setToolTip(text);
			connect(row, &RowButton::clicked, this, [this, hit]() { showVerse(hit); });
			versesLayout_->addWidget(row);
		}
		return;
	}
	if (!bible_ || !current_.valid()) {
		referenceLabel_->setText(QString());
		return;
	}
	Reference chapter = current_;
	chapter.verseFrom = 0;
	referenceLabel_->setText(qs(referenceLabel(chapter) + " \xC2\xB7 " + bible_->name));
	// The verse on screen with a few around it, so the list stays short in a small panel.
	const int count = bible_->verseCount(current_.book, current_.chapter);
	const int first = std::max(1, current_.verseFrom - 2);
	const int last = std::min(count, first + 6);
	for (int verse = first; verse <= last; ++verse) {
		auto *row = new RowButton(versesBox_);
		const QString text = qs(bible_->verse(current_.book, current_.chapter, verse));
		// The verse on screen is marked in front, where a long verse can't push the mark out of sight.
		const bool here = verse == current_.verseFrom;
		const QString mark = here ? (shown_ ? QStringLiteral("\u25CF ") : QStringLiteral("\u25CB "))
					  : QString();
		row->setTitle(QStringLiteral("%1%2  %3").arg(mark).arg(verse).arg(text));
		if (here)
			row->setAccessibleName(shown_ ? lt("Laycue.Scripture.OnScreen") : lt("Laycue.Scripture.Ready"));
		row->setToolTip(text);
		connect(row, &RowButton::clicked, this, [this, verse]() {
			Reference ref = current_;
			ref.verseFrom = verse;
			showVerse(ref);
		});
		versesLayout_->addWidget(row);
	}
}

void ScriptureDock::rebuildSaved()
{
	clear(savedLayout_);
	if (settings_.saved.empty()) {
		savedLayout_->addWidget(label(savedLayout_->parentWidget(), lt("Laycue.Scripture.SavedEmpty"), 11, 400,
					      color::textTertiary));
		return;
	}
	for (size_t i = 0; i < settings_.saved.size(); ++i) {
		auto *line = new QWidget(savedLayout_->parentWidget());
		auto *h = new QHBoxLayout(line);
		h->setContentsMargins(0, 0, 0, 0);
		h->setSpacing(size::gapSmall);
		auto *row = new RowButton(line);
		row->setTitle(qs(settings_.saved[i]));
		row->setValue(lt("Laycue.Scripture.Show"));
		connect(row, &RowButton::clicked, this, [this, i]() {
			if (i >= settings_.saved.size())
				return;
			input_->setText(qs(settings_.saved[i]));
			typed();
			go();
		});
		h->addWidget(row, 1);
		auto *remove = button(line, QStringLiteral("✕"), "laycueSecondary");
		remove->setAccessibleName(lt("Laycue.Scripture.Remove"));
		connect(remove, &QAbstractButton::clicked, this, [this, i]() {
			if (i >= settings_.saved.size())
				return;
			settings_.saved.erase(settings_.saved.begin() + std::ptrdiff_t(i));
			save();
			rebuildSaved();
		});
		h->addWidget(remove);
		savedLayout_->addWidget(line);
	}
}

// ---- Versions and settings ---------------------------------------------------------------------------------

bool ScriptureDock::useVersion(const VersionFile &version)
{
	std::string error;
	auto bible = loadVersion(version, error);
	if (!bible) {
		toast(lt("Laycue.Scripture.VersionFailed").arg(QFileInfo(qs(version.path)).fileName(), qs(error)));
		return false;
	}
	bible_ = bible;
	version_ = version;
	settings_.version = version.file;
	versionRow_->setValue(qs(bible_->abbrev + " \xC2\xB7 " + bible_->name));
	save();
	if (searchMode_ && searchInput_->text().trimmed().isEmpty())
		searching_ = false; // nothing typed to search for in the new version
	if (current_.valid())
		showVerse(current_); // the same verse in the new version
	if (searchMode_ && !searchInput_->text().trimmed().isEmpty())
		search(); // the results follow the new version
	return true;
}

void ScriptureDock::chooseVersion()
{
	QMenu menu(this);
	for (const VersionFile &v : listVersions()) {
		QAction *a = menu.addAction(QFileInfo(qs(v.path)).completeBaseName().toUpper());
		a->setCheckable(true);
		a->setChecked(v.file == version_.file);
		connect(a, &QAction::triggered, this, [this, v]() { useVersion(v); });
	}
	menu.addSeparator();
	connect(menu.addAction(lt("Laycue.Scripture.AddVersion")), &QAction::triggered, this, [this]() {
		const QString file = QFileDialog::getOpenFileName(this, lt("Laycue.Scripture.AddVersion"), QString(),
								  lt("Laycue.Scripture.XmlFiles"));
		if (file.isEmpty())
			return;
		std::string error;
		const VersionFile added = addVersion(file.toStdString(), error);
		if (added.path.empty()) {
			toast(lt("Laycue.Scripture.VersionFailed").arg(QFileInfo(file).fileName(), qs(error)));
			return;
		}
		if (useVersion(added))
			toast(lt("Laycue.Scripture.VersionAdded").arg(qs(bible_->name)));
	});
	menu.exec(versionRow_->mapToGlobal(QPoint(0, versionRow_->height())));
}

void ScriptureDock::useTemplate(int index)
{
	if (index < 0 || index >= int(settings_.styles.size()))
		return;
	settings_.templateIndex = index;
	editor_->setStyle(settings_.styles[size_t(index)]);
	showTemplates();
	save();
	push();
}

// Shows the pack the chosen template belongs to, and that pack's templates.
void ScriptureDock::showTemplates()
{
	const int index = std::clamp(settings_.templateIndex, 0, int(scriptureTemplates().size()) - 1);
	const int pack = scriptureTemplates()[size_t(index)].pack;
	packMembers_ = templatesOfPack(pack);
	QStringList names;
	int position = 0;
	for (size_t i = 0; i < packMembers_.size(); ++i) {
		names << lt(scriptureTemplates()[size_t(packMembers_[i])].nameKey);
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

void ScriptureDock::updateStatus()
{
	// The other panel (or the layer's Properties) may have hidden it: the layer says what is true.
	if (!sourceName_.empty() && shown_ && !slides::isShown(sourceName_)) {
		shown_ = false;
		rebuildVerses();
	}
	if (!sourceName_.empty()) {
		bool live = false;
		presence_->setText(presenceText(presenceOf(sourceName_), live));
		presence_->setStyleSheet(labelStyle(live ? color::next : color::textTertiary, presence_->font()));
	} else {
		const std::vector<std::string> existing = slides::sourcesOf(kSourceId);
		if (!existing.empty())
			sourceName_ = existing.front();
		presence_->setText(sourceName_.empty() ? lt("Laycue.Scripture.NoLayerYet") : QString());
	}
	status_->setText(shown_ ? lt("Laycue.Scripture.OnScreen") : lt("Laycue.Scripture.NotShowing"));
	status_->setStyleSheet(labelStyle(shown_ ? color::live : color::textTertiary, status_->font()));
	showButton_->setText(shown_ ? lt("Laycue.Scripture.Hide") : lt("Laycue.Scripture.Show"));
}

void ScriptureDock::save()
{
	try {
		saveSettings(settings_);
	} catch (...) {
		obs_log(LOG_ERROR, "Laycue Scripture: could not save its settings");
	}
}

void ScriptureDock::toast(const QString &text)
{
	toast_->setText(text);
	toast_->show();
	toastTimer_.start();
}

} // namespace laycue::scripture
