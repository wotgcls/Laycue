#include "ui/laycue-dock.hpp"

#include "obs/scene-reader.hpp"
#include "obs/switcher.hpp"
#include "ui/tokens.hpp"
#include "ui/widgets.hpp"
#include "ui/window-finder.hpp"

#include <obs-module.h>
#include <plugin-support.h>

#include <QGridLayout>
#include <QGuiApplication>
#include <QHBoxLayout>
#include <QLabel>
#include <QPixmap>
#include <QPushButton>
#include <QResizeEvent>
#include <QScrollArea>
#include <QShortcut>
#include <QVBoxLayout>

#include <algorithm>

namespace laycue {

namespace {

QString text(const char *key)
{
	return QString::fromUtf8(obs_module_text(key));
}

QString fromStd(const std::string &value)
{
	return QString::fromStdString(value);
}

constexpr int kRefreshDelayMs = 50;       // coalesce bursts of OBS changes into one refresh
constexpr int kPictureTickMs = 100;       // how often the panel asks for fresh pictures
constexpr int kToastMs = 2600;            // how long a notice stays up
constexpr int kSuppressFallbackMs = 5000; // safety net if OBS never reports the transition ending
constexpr int kCardMinWidth = 190;        // shot cards never get narrower than this (except at 2 across)
constexpr int kWideLayoutWidth = 640;     // from here on, Live and Up next sit side by side
constexpr int kRestoreFallbackMs = 1000;  // same, for putting the transition back after a Cut
constexpr int kLiveIntervalMs = 100;      // Live picture: ~10 fps
constexpr int kNextIntervalMs = 250;      // Up next picture: ~4 fps
constexpr int kThumbIntervalMs = 2500;    // shot card stills
constexpr uint32_t kLiveWidth = 640, kLiveHeight = 360;
constexpr uint32_t kThumbWidth = 384, kThumbHeight = 216;

QImage toImage(const Picture &picture)
{
	QImage image(picture.pixels.data(), int(picture.width), int(picture.height), int(picture.width * 4),
		     QImage::Format_RGBX8888);
	return image.copy(); // own the pixels
}

// The brand mark for the header, picked for the OBS theme (cream screen on dark, ink on light).
QLabel *makeMark(QWidget *parent)
{
	auto *mark = new QLabel(parent);
	const bool darkTheme = parent->palette().color(QPalette::Window).lightness() < 128;
	char *path = obs_module_file(darkTheme ? "brand/laycue-mark-on-dark.png" : "brand/laycue-mark-on-light.png");
	if (!path)
		return mark;
	QPixmap image(QString::fromUtf8(path));
	bfree(path);
	if (image.isNull())
		return mark;
	const qreal ratio = qGuiApp ? qGuiApp->devicePixelRatio() : 1.0;
	QPixmap scaled = image.scaledToHeight(qRound(19 * ratio), Qt::SmoothTransformation);
	scaled.setDevicePixelRatio(ratio);
	mark->setPixmap(scaled);
	mark->setAccessibleName(QStringLiteral("Laycue"));
	return mark;
}

QLabel *makeLabel(QWidget *parent, const QFont &font, const QColor &colorValue, const QString &value = QString())
{
	auto *label = new QLabel(value, parent);
	label->setFont(font);
	label->setStyleSheet(ui::labelStyle(colorValue, font));
	return label;
}

void setLabelColor(QLabel *label, const QColor &colorValue)
{
	label->setStyleSheet(ui::labelStyle(colorValue, label->font()));
}

QString panelStyleSheet()
{
	using namespace ui;
	return QStringLiteral(R"(
#LaycueContent { background: %1; }
#LaycueContent QScrollArea, #LaycueContent QScrollArea > QWidget > QWidget { background: transparent; border: none; }
#LaycueContent QScrollBar:vertical { background: transparent; width: 8px; margin: 0; }
#LaycueContent QScrollBar::handle:vertical { background: %2; border-radius: 4px; min-height: 24px; }
#LaycueContent QScrollBar::add-line, #LaycueContent QScrollBar::sub-line { height: 0; }
QPushButton#laycueGoLive { background: %3; border: none; border-radius: %4px; }
QPushButton#laycueGoLive:hover { background: %5; }
QPushButton#laycueGoLive:pressed { background: %5; }
QPushButton#laycueGoLive:disabled { background: %2; }
QPushButton#laycueGoLive:focus { border: 2px solid %6; }
QPushButton#laycueCut { background: transparent; border: 1.5px solid %7; border-radius: %4px; }
QPushButton#laycueCut:hover { background: %8; }
QPushButton#laycueCut:pressed { background: %2; }
QPushButton#laycueCut:disabled { border-color: %2; }
QPushButton#laycueCut:focus { border: 2px solid %6; }
QPushButton#laycueSetupLink { background: transparent; color: %6; border: none; border-radius: %4px; padding: 0 8px; text-align: left; font-size: 13px; font-weight: 600; }
QPushButton#laycueSetupLink:hover { background: %8; }
QPushButton#laycueSetupLink:focus { border: 2px solid %6; }
)")
		       .arg(hex(color::ink), hex(color::line), hex(color::live), QString::number(size::radius),
			    hex(color::livePressed), hex(color::setup), hex(color::lineStrong), hex(color::graphite)) +
	       (uiFamily().isEmpty()
			? QString()
			: QStringLiteral("#LaycueContent QLabel, #LaycueContent QPushButton { font-family: \"%1\"; }")
				  .arg(uiFamily()));
}

// A button with a title and a small detail line under it (Go Live / Cut).
QPushButton *makeTwoLineButton(QWidget *parent, const char *objectName, QLabel *&title, QLabel *&detail,
			       const QColor &titleColor, const QColor &detailColor)
{
	auto *button = new QPushButton(parent);
	button->setObjectName(objectName);
	button->setMinimumHeight(ui::size::bigButton);
	button->setCursor(Qt::PointingHandCursor);
	auto *layout = new QVBoxLayout(button);
	layout->setContentsMargins(8, 6, 8, 6);
	layout->setSpacing(0);
	title = makeLabel(button, ui::uiFont(16, 700), titleColor);
	detail = makeLabel(button, ui::uiFont(11, 500), detailColor);
	for (QLabel *l : {title, detail}) {
		l->setAlignment(Qt::AlignCenter);
		l->setAttribute(Qt::WA_TransparentForMouseEvents);
		layout->addWidget(l);
	}
	return button;
}

} // namespace

LaycueDock::LaycueDock(QWidget *parent) : QWidget(parent)
{
	setObjectName("LaycueDock");
	setAttribute(Qt::WA_StyledBackground, true);
	setMinimumWidth(240);
	ui::loadBrandFonts();

	watcher_ = std::make_unique<SceneWatcher>([this]() { scheduleRefresh(); });
	grabber_ = std::make_unique<FrameGrabber>();

	buildUi();

	refreshTimer_.setSingleShot(true);
	refreshTimer_.setInterval(kRefreshDelayMs);
	connect(&refreshTimer_, &QTimer::timeout, this, &LaycueDock::refreshNow);

	pictureTimer_.setInterval(kPictureTickMs);
	connect(&pictureTimer_, &QTimer::timeout, this, [this]() {
		grabber_->requestFrame();
		pullPictures();
	});

	toastTimer_.setSingleShot(true);
	toastTimer_.setInterval(kToastMs);
	connect(&toastTimer_, &QTimer::timeout, this, [this]() { toast_->hide(); });

	suppressTimer_.setSingleShot(true);
	suppressTimer_.setInterval(kSuppressFallbackMs);
	connect(&suppressTimer_, &QTimer::timeout, this, [this]() {
		suppressPreview_ = false;
		scheduleRefresh();
	});

	restoreTimer_.setSingleShot(true);
	restoreTimer_.setInterval(kRestoreFallbackMs);
	connect(&restoreTimer_, &QTimer::timeout, this, &LaycueDock::finishCut);

	// Space = Go Live while the Laycue panel has keyboard focus.
	auto *space = new QShortcut(QKeySequence(Qt::Key_Space), this);
	space->setContext(Qt::WidgetWithChildrenShortcut);
	connect(space, &QShortcut::activated, this, [this]() { goLive(false); });

	updateStateUi();
}

LaycueDock::~LaycueDock()
{
	// Stop OBS signals and graphics work first so nothing calls back into a half-destroyed dock.
	pictureTimer_.stop();
	watcher_.reset();
	if (grabber_)
		grabber_->shutdown();
}

void LaycueDock::buildUi()
{
	using namespace ui;

	// OBS may restyle the widget it docks, so all Laycue styling lives on an inner container
	// that only Laycue touches.
	auto *outer = new QVBoxLayout(this);
	outer->setContentsMargins(0, 0, 0, 0);
	outer->setSpacing(0);
	auto *content = new QWidget(this);
	content->setObjectName("LaycueContent");
	content->setAttribute(Qt::WA_StyledBackground, true);
	content->setStyleSheet(panelStyleSheet());
	outer->addWidget(content);

	auto *root = new QVBoxLayout(content);
	root->setContentsMargins(size::margin, size::margin, size::margin, size::margin);
	root->setSpacing(size::gap);

	// Header: the brand mark, and a quiet note when OBS Studio Mode is on.
	auto *header = new QHBoxLayout();
	header->setSpacing(size::gapSmall);
	header->addWidget(makeMark(content));
	header->addStretch(1);
	studioLabel_ = makeLabel(content, uiFont(11, 500), color::textTertiary, text("Laycue.StudioMode"));
	studioLabel_->setToolTip(text("Laycue.StudioModeTip"));
	studioLabel_->hide();
	header->addWidget(studioLabel_);
	root->addLayout(header);

	// Live and Up next pictures. Narrow panel: big Live, with Up next small beside its name.
	// Wide panel: the two side by side at equal size, Up next on the left like OBS's Preview.
	picturesGrid_ = new QGridLayout();
	picturesGrid_->setHorizontalSpacing(size::gap);
	picturesGrid_->setVerticalSpacing(size::gap);
	liveView_ = new PictureView(content);
	liveView_->setRing(color::live, 1.5);
	liveView_->setPlaceholder(text("Laycue.WaitingForPicture"));
	liveView_->setAccessibleName(text("Laycue.LiveAccessible"));
	nextView_ = new PictureView(content);
	nextView_->setAccessibleName(text("Laycue.NextAccessible"));

	nextTextBox_ = new QWidget(content);
	auto *nextText = new QVBoxLayout(nextTextBox_);
	nextText->setContentsMargins(0, 0, 0, 0);
	nextText->setSpacing(3);
	auto *eyebrowRow = new QHBoxLayout();
	eyebrowRow->setSpacing(6);
	nextDot_ = new QLabel(nextTextBox_);
	nextDot_->setFixedSize(7, 7);
	eyebrowRow->addWidget(nextDot_, 0, Qt::AlignVCenter);
	nextEyebrow_ = makeLabel(nextTextBox_, uiFont(11, 700), color::textSecondary, text("Laycue.UpNext"));
	eyebrowRow->addWidget(nextEyebrow_);
	eyebrowRow->addStretch(1);
	nextText->addLayout(eyebrowRow);
	nextName_ = makeLabel(nextTextBox_, uiFont(15, 600), color::cream);
	nextName_->setWordWrap(true);
	nextText->addWidget(nextName_);
	nextHint_ = makeLabel(nextTextBox_, uiFont(12, 400), color::textSecondary);
	nextHint_->setWordWrap(true);
	nextText->addWidget(nextHint_);
	nextText->addStretch(1);
	root->addLayout(picturesGrid_);
	arrangePictures(false);

	// Cut (instant) and Go Live (OBS transition)
	auto *buttons = new QHBoxLayout();
	buttons->setSpacing(size::gapSmall);
	QLabel *cutTitle = nullptr;
	QLabel *cutDetail = nullptr;
	cutButton_ = makeTwoLineButton(content, "laycueCut", cutTitle, cutDetail, color::cream, color::textSecondary);
	cutTitle->setFont(uiFont(15, 600));
	setLabelColor(cutTitle, color::cream); // re-applies the style sheet with the new size
	cutTitle->setText(text("Laycue.Cut"));
	cutDetail->setText(text("Laycue.Instant"));
	cutButton_->setToolTip(text("Laycue.CutTip"));
	cutButton_->setAccessibleName(text("Laycue.Cut"));
	connect(cutButton_, &QPushButton::clicked, this, [this]() { goLive(true); });
	buttons->addWidget(cutButton_, 2);

	goLiveButton_ = makeTwoLineButton(content, "laycueGoLive", goLiveTitle_, goLiveDetail_, color::ink, color::ink);
	goLiveTitle_->setText(text("Laycue.GoLive"));
	goLiveButton_->setToolTip(text("Laycue.GoLiveTip"));
	goLiveButton_->setAccessibleName(text("Laycue.GoLive"));
	connect(goLiveButton_, &QPushButton::clicked, this, [this]() { goLive(false); });
	buttons->addWidget(goLiveButton_, 3);
	root->addLayout(buttons);

	// Shots
	auto *shotsHeader = new QVBoxLayout();
	shotsHeader->setSpacing(2);
	shotsHeader->addWidget(makeLabel(content, uiFont(13, 700), color::cream, text("Laycue.Shots")));
	auto *shotsHint = makeLabel(content, uiFont(12, 400), color::textSecondary, text("Laycue.ShotsHint"));
	shotsHint->setWordWrap(true);
	shotsHeader->addWidget(shotsHint);
	root->addLayout(shotsHeader);

	shotsScroll_ = new QScrollArea(content);
	shotsScroll_->setWidgetResizable(true);
	shotsScroll_->setFrameShape(QFrame::NoFrame);
	shotsScroll_->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
	shotsHost_ = new QWidget(shotsScroll_);
	shotsGrid_ = new QGridLayout(shotsHost_);
	shotsGrid_->setContentsMargins(0, 2, 0, 2);
	shotsGrid_->setHorizontalSpacing(size::gapSmall + 2);
	shotsGrid_->setVerticalSpacing(size::gapSmall + 2);
	shotsGrid_->setAlignment(Qt::AlignTop);
	shotsScroll_->setWidget(shotsHost_);
	shotsScroll_->viewport()->installEventFilter(this); // re-size cards when the list width changes
	root->addWidget(shotsScroll_, 1);

	emptyLabel_ = makeLabel(content, uiFont(12, 400), color::textSecondary, text("Laycue.NoScenes"));
	emptyLabel_->setWordWrap(true);
	emptyLabel_->hide();
	root->addWidget(emptyLabel_);

	// Setup tool (Signal Blue = setup): check where Laycue finds a pushback's window.
	auto *findWindow = new QPushButton(text("Laycue.FindWindow"), content);
	findWindow->setObjectName("laycueSetupLink");
	findWindow->setToolTip(text("Laycue.FindWindowTip"));
	findWindow->setCursor(Qt::PointingHandCursor);
	findWindow->setFont(uiFont(13, 600));
	findWindow->setMinimumHeight(size::touch);
	connect(findWindow, &QPushButton::clicked, this, [this]() {
		auto *dialog = new ui::WindowFinderDialog(this);
		dialog->show();
	});
	root->addWidget(findWindow, 0, Qt::AlignLeft);

	// Notice (amber marker + one line), shown briefly after something happens.
	toast_ = new QWidget(content);
	toast_->setObjectName("laycueToast");
	toast_->setAttribute(Qt::WA_StyledBackground, true);
	toast_->setStyleSheet(QStringLiteral("#laycueToast { background: %1; border-radius: %2px; }")
				      .arg(hex(color::graphite), QString::number(size::radius)));
	auto *toastLayout = new QHBoxLayout(toast_);
	toastLayout->setContentsMargins(12, 8, 12, 8);
	toastLayout->setSpacing(8);
	auto *marker = new QLabel(toast_);
	marker->setFixedSize(8, 8);
	marker->setStyleSheet(QStringLiteral("background: %1; border-radius: 2px;").arg(hex(color::overlay)));
	toastLayout->addWidget(marker, 0, Qt::AlignVCenter);
	toastText_ = makeLabel(toast_, uiFont(12, 500), color::cream);
	toastText_->setWordWrap(true);
	toastLayout->addWidget(toastText_, 1);
	toast_->hide();
	root->addWidget(toast_);
}

void LaycueDock::resizeEvent(QResizeEvent *event)
{
	QWidget::resizeEvent(event);
	const int contentWidth = width() - 2 * ui::size::margin;
	const bool wide = contentWidth >= kWideLayoutWidth;
	arrangePictures(wide);
	if (wide) {
		const int half = (contentWidth - ui::size::gap) / 2;
		liveView_->setFixedSize(half, half * 9 / 16);
		nextView_->setFixedSize(half, half * 9 / 16);
	} else {
		liveView_->setFixedSize(contentWidth, contentWidth * 9 / 16);
		const int nextWidth = (contentWidth - ui::size::gap) * 45 / 100;
		nextView_->setFixedSize(nextWidth, nextWidth * 9 / 16);
	}
	layoutCards();
}

void LaycueDock::arrangePictures(bool wide)
{
	if (picturesArranged_ && wide == wideLayout_)
		return;
	picturesArranged_ = true;
	wideLayout_ = wide;
	for (QWidget *w : {static_cast<QWidget *>(liveView_), static_cast<QWidget *>(nextView_), nextTextBox_})
		picturesGrid_->removeWidget(w);
	if (wide) {
		picturesGrid_->addWidget(nextView_, 0, 0, Qt::AlignTop);
		picturesGrid_->addWidget(liveView_, 0, 1, Qt::AlignTop);
		picturesGrid_->addWidget(nextTextBox_, 1, 0, Qt::AlignTop);
	} else {
		picturesGrid_->addWidget(liveView_, 0, 0, 1, 2, Qt::AlignTop);
		picturesGrid_->addWidget(nextView_, 1, 0, Qt::AlignTop);
		picturesGrid_->addWidget(nextTextBox_, 1, 1, Qt::AlignTop);
	}
}

bool LaycueDock::eventFilter(QObject *watched, QEvent *event)
{
	if (shotsScroll_ && watched == shotsScroll_->viewport() && event->type() == QEvent::Resize)
		layoutCards();
	return QWidget::eventFilter(watched, event);
}

void LaycueDock::handleFrontendEvent(enum obs_frontend_event event)
{
	switch (event) {
	case OBS_FRONTEND_EVENT_FINISHED_LOADING:
	case OBS_FRONTEND_EVENT_SCENE_COLLECTION_CHANGED:
		obsReady_ = true;
		pictureTimer_.start();
		scheduleRefresh();
		break;

	case OBS_FRONTEND_EVENT_SCENE_LIST_CHANGED:
	case OBS_FRONTEND_EVENT_SCENE_CHANGED:
	case OBS_FRONTEND_EVENT_PREVIEW_SCENE_CHANGED:
	case OBS_FRONTEND_EVENT_STUDIO_MODE_ENABLED:
	case OBS_FRONTEND_EVENT_STUDIO_MODE_DISABLED:
	case OBS_FRONTEND_EVENT_PROFILE_CHANGED:
		scheduleRefresh();
		break;

	case OBS_FRONTEND_EVENT_TRANSITION_STOPPED:
		suppressPreview_ = false;
		suppressTimer_.stop();
		// Let OBS finish its own end-of-transition work before changing the transition.
		if (!transitionToRestore_.empty())
			QTimer::singleShot(0, this, &LaycueDock::finishCut);
		scheduleRefresh();
		break;

	case OBS_FRONTEND_EVENT_TRANSITION_CHANGED:
	case OBS_FRONTEND_EVENT_TRANSITION_DURATION_CHANGED:
		if (transitionToRestore_.empty()) // don't flash "Cut" on Go Live during a Cut
			updateTransitionLabel();
		break;

	case OBS_FRONTEND_EVENT_SCENE_COLLECTION_CLEANUP:
		finishCut(); // so the collection is saved with the operator's transition
		// OBS is about to free every scene. Let go of them before it does.
		obsReady_ = false;
		refreshTimer_.stop();
		watcher_->detachAll();
		resetForCollectionChange();
		break;

	case OBS_FRONTEND_EVENT_EXIT:
		finishCut();
		shuttingDown_ = true;
		obsReady_ = false;
		refreshTimer_.stop();
		pictureTimer_.stop();
		watcher_->detachAll();
		grabber_->shutdown();
		break;

	default:
		break;
	}
}

void LaycueDock::scheduleRefresh()
{
	// May be called from any thread: hop onto the UI thread before touching the timer.
	QMetaObject::invokeMethod(
		this,
		[this]() {
			if (obsReady_ && !shuttingDown_)
				refreshTimer_.start();
		},
		Qt::QueuedConnection);
}

void LaycueDock::refreshNow()
{
	if (!obsReady_ || shuttingDown_)
		return;

	try {
		watcher_->attachAll(); // new scenes or groups may have appeared
		const Snapshot snapshot = readSnapshot();

		std::vector<Shot> shots;
		for (const SceneInfo &scene : snapshot.scenes)
			shots.push_back({scene.uuid, scene.name});
		const bool changed =
			shots.size() != shots_.size() ||
			!std::equal(shots.begin(), shots.end(), shots_.begin(),
				    [](const Shot &a, const Shot &b) { return a.uuid == b.uuid && a.name == b.name; });
		if (changed) {
			for (const Shot &old : shots_) {
				const bool stillThere = std::any_of(shots.begin(), shots.end(),
								    [&](const Shot &s) { return s.uuid == old.uuid; });
				if (!stillThere)
					shotRemoved(cue_, old.uuid);
			}
			shots_ = std::move(shots);
			rebuildCards();
		}

		liveChanged(cue_, snapshot.programSceneUuid);

		// Studio mode: when the operator changes OBS's Preview themselves, follow it as Up next.
		// Preview changes caused by our own Go Live (OBS swapping scenes) are ignored.
		if (snapshot.studioMode) {
			if (snapshot.previewSceneUuid != lastPreview_) {
				if (!suppressPreview_ && !snapshot.previewSceneUuid.empty() &&
				    snapshot.previewSceneUuid != cue_.live)
					cue_.next = snapshot.previewSceneUuid;
				lastPreview_ = snapshot.previewSceneUuid;
			}
		} else {
			lastPreview_.clear();
		}
		studioLabel_->setVisible(snapshot.studioMode);

		updatePictureTargets();
		updateStateUi();
	} catch (const std::exception &error) {
		obs_log(LOG_ERROR, "Failed to refresh the panel: %s", error.what());
		showToast(text("Laycue.ReadError"));
	} catch (...) {
		obs_log(LOG_ERROR, "Failed to refresh the panel: unknown error");
		showToast(text("Laycue.ReadError"));
	}
}

void LaycueDock::rebuildCards()
{
	for (auto &entry : cards_)
		entry.second->deleteLater();
	cards_.clear();
	pictureSerials_.clear();

	for (const Shot &shot : shots_) {
		auto *card = new ui::ShotCard(shotsHost_);
		card->setName(fromStd(shot.name));
		card->setToolTip(text("Laycue.ShotTip").arg(fromStd(shot.name)));
		const std::string uuid = shot.uuid;
		connect(card, &QAbstractButton::clicked, this, [this, uuid]() { onShotClicked(uuid); });
		cards_[shot.uuid] = card;
	}
	emptyLabel_->setVisible(shots_.empty());
	shotsScroll_->setVisible(!shots_.empty());
	layoutCards();
}

void LaycueDock::layoutCards()
{
	if (!shotsGrid_)
		return;
	while (shotsGrid_->count() > 0)
		delete shotsGrid_->takeAt(0); // removes the layout item only; the card stays

	const int available = shotsScroll_->viewport()->width();
	// Two cards across in a normal panel; more when the panel is wide, so cards stay small.
	const int columns = std::clamp(available / kCardMinWidth, 2, 6);
	const int columnWidth = std::max((available - shotsGrid_->horizontalSpacing()) / columns, 80);
	int index = 0;
	for (const Shot &shot : shots_) {
		auto it = cards_.find(shot.uuid);
		if (it == cards_.end())
			continue;
		it->second->setFixedHeight(it->second->heightForWidth(columnWidth));
		shotsGrid_->addWidget(it->second, index / columns, index % columns);
		it->second->show();
		++index;
	}
}

void LaycueDock::updateStateUi()
{
	using namespace ui;

	// Live
	const std::string liveName = nameOf(cue_.live);
	liveView_->setPill(liveName.empty() ? text("Laycue.LivePill")
					    : text("Laycue.LivePill") + QStringLiteral(" · ") + fromStd(liveName),
			   color::live);

	// Up next
	const bool cued = canTake(cue_);
	if (cued) {
		nextView_->setRing(color::next, 1.5);
		nextView_->setPlaceholder(QString());
		nextDot_->setStyleSheet(QStringLiteral("background: %1; border-radius: 3px;").arg(hex(color::next)));
		nextName_->setText(fromStd(nameOf(cue_.next)));
		setLabelColor(nextName_, color::cream);
		nextHint_->setText(text("Laycue.NextReadyHint"));
	} else {
		nextView_->setRing(color::line, 1.5);
		nextView_->clearImage();
		nextView_->setPlaceholder(QString()); // the hint beside it already says what to do
		nextDot_->setStyleSheet(
			QStringLiteral("background: %1; border-radius: 3px;").arg(hex(color::textDisabled)));
		nextName_->setText(text("Laycue.NothingCued"));
		setLabelColor(nextName_, color::textSecondary);
		nextHint_->setText(text("Laycue.NextEmptyHint"));
	}
	cutButton_->setEnabled(cued);
	goLiveButton_->setEnabled(cued);
	const QColor buttonText = cued ? color::ink : color::textDisabled;
	setLabelColor(goLiveTitle_, buttonText);
	setLabelColor(goLiveDetail_, buttonText);
	updateTransitionLabel();

	// Cards
	for (auto &entry : cards_) {
		ShotCard::State state = ShotCard::State::Idle;
		if (entry.first == cue_.live)
			state = ShotCard::State::Live;
		else if (entry.first == cue_.next)
			state = ShotCard::State::Next;
		entry.second->setState(state);
	}
}

void LaycueDock::updateTransitionLabel()
{
	if (!goLiveDetail_)
		return;
	const TransitionInfo transition = currentTransition();
	QString detail = fromStd(transition.name);
	if (transition.durationMs > 0)
		detail += QStringLiteral(" · %1 s").arg(transition.durationMs / 1000.0, 0, 'f', 1);
	goLiveDetail_->setText(detail);
}

void LaycueDock::updatePictureTargets()
{
	std::vector<PictureTarget> targets;
	targets.push_back({FrameGrabber::kProgram, kLiveWidth, kLiveHeight, kLiveIntervalMs});
	for (const Shot &shot : shots_) {
		const bool isNext = shot.uuid == cue_.next;
		targets.push_back({shot.uuid, kThumbWidth, kThumbHeight, isNext ? kNextIntervalMs : kThumbIntervalMs});
	}
	grabber_->setTargets(std::move(targets));
}

void LaycueDock::pullPictures()
{
	Picture picture;
	if (grabber_->takeIfNewer(FrameGrabber::kProgram, pictureSerials_[FrameGrabber::kProgram], picture)) {
		pictureSerials_[FrameGrabber::kProgram] = picture.serial;
		liveView_->setImage(toImage(picture));
	}
	for (auto &entry : cards_) {
		if (!grabber_->takeIfNewer(entry.first, pictureSerials_[entry.first], picture))
			continue;
		pictureSerials_[entry.first] = picture.serial;
		const QImage image = toImage(picture);
		entry.second->setImage(image);
		if (entry.first == cue_.next)
			nextView_->setImage(image);
	}
}

void LaycueDock::onShotClicked(const std::string &uuid)
{
	const PickResult result = pickShot(cue_, uuid);
	if (result == PickResult::AlreadyLive) {
		showToast(text("Laycue.AlreadyLive").arg(fromStd(nameOf(uuid))));
		return;
	}
	if (result == PickResult::Cued) {
		if (studioModeActive())
			showInPreview(uuid);
		// Show the still we already have straight away; faster refreshes follow.
		pictureSerials_[uuid] = 0;
	}
	updatePictureTargets();
	updateStateUi();
}

void LaycueDock::goLive(bool instant)
{
	if (!canTake(cue_) || !transitionToRestore_.empty())
		return; // nothing cued, or a Cut is still finishing (one frame)
	const std::string target = cue_.next;
	const QString name = fromStd(nameOf(target));

	suppressPreview_ = studioModeActive();
	if (suppressPreview_)
		suppressTimer_.start();

	if (!takeLive(target, instant, transitionToRestore_)) {
		shotRemoved(cue_, target);
		showToast(text("Laycue.SceneGone"));
		updateStateUi();
		return;
	}
	if (!transitionToRestore_.empty())
		restoreTimer_.start();
	take(cue_);
	showToast(text(instant ? "Laycue.CutDone" : "Laycue.LiveDone").arg(name));
	updatePictureTargets();
	updateStateUi();
}

void LaycueDock::finishCut()
{
	restoreTimer_.stop();
	if (transitionToRestore_.empty())
		return;
	const std::string name = transitionToRestore_;
	transitionToRestore_.clear();
	try {
		restoreTransition(name);
	} catch (...) {
		obs_log(LOG_ERROR, "Could not put back the OBS transition after a Cut");
	}
	updateTransitionLabel();
}

void LaycueDock::showToast(const QString &message)
{
	toastText_->setText(message);
	toast_->show();
	toastTimer_.start();
}

std::string LaycueDock::nameOf(const std::string &uuid) const
{
	for (const Shot &shot : shots_) {
		if (shot.uuid == uuid)
			return shot.name;
	}
	return std::string();
}

void LaycueDock::resetForCollectionChange()
{
	grabber_->setTargets({});
	shots_.clear();
	cue_ = CueState();
	lastPreview_.clear();
	suppressPreview_ = false;
	rebuildCards();
	liveView_->clearImage();
	nextView_->clearImage();
	updateStateUi();
}

} // namespace laycue
