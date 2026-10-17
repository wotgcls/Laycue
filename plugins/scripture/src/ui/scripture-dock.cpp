#include "ui/scripture-dock.hpp"

#include "core/books.hpp"
#include "core/reference.hpp"
#include "obs/scene-presence.hpp"
#include "slides/slide-control.hpp"
#include "slides/slide-render.hpp"
#include "ui/brand-mark.hpp"
#include "ui/controls.hpp"
#include "ui/pack-switcher.hpp"
#include "ui/pages/helpers.hpp"
#include "ui/presence-text.hpp"
#include "ui/style.hpp"
#include "ui/text.hpp"
#include "ui/tokens.hpp"

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

#include <algorithm>

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

ScriptureDock::ScriptureDock(QWidget *parent) : QWidget(parent)
{
	setObjectName("LaycueScriptureDock");
	setMinimumWidth(280);
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
		input_->setText(qs(settings_.lastTyped));
		typed();
	}

	toastTimer_.setSingleShot(true);
	toastTimer_.setInterval(2600);
	connect(&toastTimer_, &QTimer::timeout, this, [this]() { toast_->hide(); });
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

	v->addWidget(new PackSwitcher(page));
	auto *header = new QHBoxLayout();
	header->setSpacing(size::gapSmall);
	header->addWidget(brandMark(page));
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

	input_ = new QLineEdit(stage);
	input_->setPlaceholderText(lt("Laycue.Scripture.TypeHint"));
	input_->setAccessibleName(lt("Laycue.Scripture.TypeHint"));
	QFont big = input_->font();
	big.setPixelSize(18);
	input_->setFont(big);
	input_->setMinimumHeight(size::touch);
	input_->installEventFilter(this);
	connect(input_, &QLineEdit::textEdited, this, &ScriptureDock::typed);
	connect(input_, &QLineEdit::returnPressed, this, &ScriptureDock::go);
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
	look_ = new Segmented(page);
	look_->setItems({lt("Laycue.Slides.Bar"), lt("Laycue.Slides.Card"), lt("Laycue.Slides.Clean"),
			 lt("Laycue.Slides.Full")});
	look_->setCurrent(std::clamp(settings_.look, 0, slides::kLookCount - 1));
	connect(look_, &Segmented::changed, this, [this](int index) {
		settings_.look = index;
		backgroundRow_->setVisible(index == int(slides::Look::Full));
		save();
		push();
	});
	v->addWidget(look_);
	backgroundRow_ = new QWidget(page);
	auto *bg = new QHBoxLayout(backgroundRow_);
	bg->setContentsMargins(0, 0, 0, 0);
	bg->setSpacing(size::gapSmall);
	auto *chooseBg = button(backgroundRow_, lt("Laycue.Slides.ChooseBackground"), "laycueSecondary");
	connect(chooseBg, &QAbstractButton::clicked, this, &ScriptureDock::chooseBackground);
	bg->addWidget(chooseBg);
	backgroundName_ = innerLabel(backgroundRow_, QString(), 11, 400, color::textTertiary);
	bg->addWidget(backgroundName_, 1);
	backgroundRow_->setVisible(settings_.look == int(slides::Look::Full));
	backgroundName_->setText(settings_.background.empty() ? lt("Laycue.Slides.NoBackground")
							      : QFileInfo(qs(settings_.background)).fileName());
	v->addWidget(backgroundRow_);

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
	v->addStretch(1);
}

// ---- Typing ------------------------------------------------------------------------------------------------

bool ScriptureDock::eventFilter(QObject *watched, QEvent *event)
{
	if (watched == input_ && event->type() == QEvent::KeyPress) {
		auto *key = static_cast<QKeyEvent *>(event);
		if (key->key() == Qt::Key_Space && input_->cursorPosition() == input_->text().length()) {
			// Space finishes the book's name, then puts the colon after the chapter.
			input_->setText(qs(afterSpace(input_->text().toStdString())));
			typed();
			return true;
		}
		if (key->key() == Qt::Key_Down) {
			step(1);
			return true;
		}
		if (key->key() == Qt::Key_Up) {
			step(-1);
			return true;
		}
	}
	return QWidget::eventFilter(watched, event);
}

void ScriptureDock::typed()
{
	const std::string text = input_->text().toStdString();
	const std::string book = bookHint(text);
	Reference ref;
	if (!book.empty()) {
		hint_->setText(lt("Laycue.Scripture.HintBook").arg(qs(book)));
	} else if (parseReference(text, ref) && ref.valid()) {
		hint_->setText(lt("Laycue.Scripture.HintGo").arg(qs(referenceLabel(ref))));
	} else {
		hint_->setText(text.empty() ? QString() : lt("Laycue.Scripture.HintUnknown"));
	}
}

void ScriptureDock::go()
{
	Reference ref;
	if (!parseReference(input_->text().toStdString(), ref) || !ref.valid() || !bible_) {
		toast(lt("Laycue.Scripture.HintUnknown"));
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
}

// ---- Showing -----------------------------------------------------------------------------------------------

void ScriptureDock::showVerse(const Reference &ref)
{
	const bool sameChapter = ref.book == current_.book && ref.chapter == current_.chapter;
	current_ = ref;
	current_.verseTo = 0;
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
	slide.look = settings_.look;
	slide.background = settings_.background;
	slide.shown = shown_;
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
	if (current_.valid())
		showVerse(current_); // the same verse in the new version
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

void ScriptureDock::chooseBackground()
{
	const QString file = QFileDialog::getOpenFileName(this, lt("Laycue.Slides.ChooseBackground"), QString(),
							  lt("Laycue.Slides.PictureFiles"));
	if (file.isEmpty())
		return;
	settings_.background = file.toStdString();
	backgroundName_->setText(QFileInfo(file).fileName());
	save();
	push();
}

void ScriptureDock::updateStatus()
{
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
