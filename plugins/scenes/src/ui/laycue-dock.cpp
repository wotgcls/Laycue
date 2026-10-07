#include "ui/laycue-dock.hpp"

#include "core/config.hpp"
#include "features/app.hpp"
#include "core/layouts.hpp"
#include "obs/layout-scene.hpp"
#include "obs/overlay-items.hpp"
#include "obs/pushback-scene.hpp"
#include "obs/scene-reader.hpp"
#include "obs/switcher.hpp"
#include "ui/controls.hpp"
#include "ui/labels.hpp"
#include "ui/pages/add-pushback-page.hpp"
#include "ui/pages/helpers.hpp"
#include "ui/pages/new-layout-page.hpp"
#include "ui/template-card.hpp"
#include "ui/pages/overlay-setup-page.hpp"
#include "ui/pages/settings-page.hpp"
#include "ui/style.hpp"
#include "ui/tabs/overlays-tab.hpp"
#include "ui/tokens.hpp"
#include "ui/view-takeover.hpp"
#include "ui/widgets.hpp"

#include <obs-module.h>
#include <plugin-support.h>

#include <QGridLayout>
#include <QGuiApplication>
#include <QHBoxLayout>
#include <QInputDialog>
#include <QLabel>
#include <QMenu>
#include <QMessageBox>
#include <QPixmap>
#include <QPushButton>
#include <QResizeEvent>
#include <QScrollArea>
#include <QShortcut>
#include <QStackedWidget>
#include <QVBoxLayout>

#include <algorithm>

namespace laycue {

namespace {

QString text(const char *key)
{
	return ui::lt(key);
}

QString fromStd(const std::string &value)
{
	return QString::fromStdString(value);
}

constexpr int kRefreshDelayMs = 50;       // coalesce bursts of OBS changes into one refresh
constexpr int kPictureTickMs = 100;       // how often the panel asks for fresh pictures
constexpr int kToastMs = 2800;            // how long a notice stays up
constexpr int kSuppressFallbackMs = 5000; // safety net if OBS never reports the transition ending
constexpr int kSuppressGraceMs = 600;     // after a transition ends, OBS's own Preview swap is still ignored
constexpr int kSaveDelayMs = 1500;        // wait this long after a change before saving
constexpr int kCardMinWidth = 190;        // shot cards never get narrower than this (except at 2 across)
constexpr int kRestoreFallbackMs = 1000;  // safety net for putting the transition back after a Cut
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

} // namespace

LaycueDock::LaycueDock(App &app, QWidget *parent) : QWidget(parent), app_(app), overlayEngine_(app)
{
	setObjectName("LaycueDock");
	setAttribute(Qt::WA_StyledBackground, true);
	setFocusPolicy(Qt::ClickFocus); // the panel itself can hold focus, so Space (Go Live) works after a click
	setMinimumWidth(280);
	ui::loadBrandFonts();

	watcher_ = std::make_unique<SceneWatcher>([this]() { scheduleRefresh(); },
						  [this](const std::string &sceneUuid, int64_t itemId, bool visible) {
							  // May come from any thread: apply the rules on the UI thread.
							  QMetaObject::invokeMethod(
								  this,
								  [this, sceneUuid, itemId, visible]() {
									  onItemVisible(sceneUuid, itemId, visible);
								  },
								  Qt::QueuedConnection);
						  });
	grabber_ = std::make_unique<FrameGrabber>();

	buildUi();
	view_ = std::make_unique<ui::ViewTakeover>(this);

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

	saveTimer_.setSingleShot(true);
	saveTimer_.setInterval(kSaveDelayMs);
	connect(&saveTimer_, &QTimer::timeout, this, [this]() {
		if (obsReady_ && !shuttingDown_)
			obs_frontend_save();
	});

	// Space = Go Live while the Laycue panel has keyboard focus.
	auto *space = new QShortcut(QKeySequence(Qt::Key_Space), this);
	space->setContext(Qt::WidgetWithChildrenShortcut);
	connect(space, &QShortcut::activated, this, [this]() { goLive(false); });

	listenerId_ = app_.addListener([this]() { onConfigChanged(); });
	onConfigChanged();
	updateStateUi();
}

LaycueDock::~LaycueDock()
{
	app_.removeListener(listenerId_);
	// Stop OBS signals and graphics work first so nothing calls back into a half-destroyed dock.
	pictureTimer_.stop();
	watcher_.reset();
	if (grabber_)
		grabber_->shutdown();
}

void LaycueDock::buildUi()
{
	// OBS may restyle the widget it docks, so all Laycue styling lives on an inner container
	// that only Laycue touches.
	auto *outer = new QVBoxLayout(this);
	outer->setContentsMargins(0, 0, 0, 0);
	outer->setSpacing(0);
	content_ = new QWidget(this);
	content_->setObjectName("LaycueContent");
	content_->setAttribute(Qt::WA_StyledBackground, true);
	content_->setStyleSheet(ui::panelStyleSheet());
	outer->addWidget(content_);

	auto *contentLayout = new QVBoxLayout(content_);
	contentLayout->setContentsMargins(0, 0, 0, 0);
	contentLayout->setSpacing(0);
	pages_ = new QStackedWidget(content_);
	contentLayout->addWidget(pages_);
	home_ = buildHome();
	pages_->addWidget(home_);

	// A notice that floats over the panel, near the bottom.
	toast_ = new QWidget(content_);
	toast_->setObjectName("laycueToast");
	toast_->setAttribute(Qt::WA_StyledBackground, true);
	toast_->setStyleSheet(
		QStringLiteral(
			"#laycueToast { background: %1; border: 1px solid rgba(255,255,255,30); border-radius: 18px; }")
			.arg(ui::hex(ui::color::control)));
	auto *toastLayout = new QHBoxLayout(toast_);
	toastLayout->setContentsMargins(14, 8, 16, 8);
	toastLayout->setSpacing(8);
	auto *marker = new QLabel(toast_);
	marker->setFixedSize(8, 8);
	marker->setStyleSheet(QStringLiteral("background: %1; border-radius: 2px;").arg(ui::hex(ui::color::overlay)));
	toastLayout->addWidget(marker, 0, Qt::AlignVCenter);
	toastText_ = makeLabel(toast_, ui::uiFont(12, 500), ui::color::cream);
	toastText_->setWordWrap(false);
	toastLayout->addWidget(toastText_);
	toast_->hide();
}

QWidget *LaycueDock::buildHome()
{
	using namespace ui;

	auto *home = new QWidget();
	auto *root = new QVBoxLayout(home);
	root->setContentsMargins(size::margin - size::shadowMargin, 6, size::margin - size::shadowMargin, 4);
	root->setSpacing(4);

	// Header: the brand mark and the tabs.
	auto *header = new QHBoxLayout();
	header->setContentsMargins(size::shadowMargin, 0, size::shadowMargin, 0);
	header->setSpacing(size::gap - 2);
	header->addWidget(makeMark(home));
	tabs_ = new Segmented(home);
	tabs_->setItems({text("Laycue.Tab.Shots"), text("Laycue.Tab.Pushbacks"), text("Laycue.Tab.Overlays")});
	tabs_->setAccessibleName(text("Laycue.Tab.Accessible"));
	connect(tabs_, &Segmented::changed, this, [this](int index) {
		showTab(index == 0 ? Tab::Shots : index == 1 ? Tab::Pushbacks : Tab::Overlays);
	});
	header->addWidget(tabs_, 1);
	lockPill_ = new ActionButton(text("Laycue.Locked"), ActionButton::Kind::Secondary, home);
	lockPill_->setToolTip(text("Laycue.LockedTip"));
	lockPill_->hide();
	connect(lockPill_, &QAbstractButton::clicked, this, [this]() {
		app_.modify([](Config &c) { c.settings.locked = false; });
		showToast(text("Laycue.Unlocked"));
	});
	header->addWidget(lockPill_);
	auto *gear = new GearButton(home);
	gear->setToolTip(text("Laycue.Settings.Title"));
	gear->setAccessibleName(text("Laycue.Settings.Title"));
	connect(gear, &QAbstractButton::clicked, this, &LaycueDock::openSettings);
	header->addWidget(gear);
	root->addLayout(header);

	// The stage: Live, Up next and the two buttons. The most raised surface.
	stage_ = new Surface(2, home);
	auto *stageLayout = new QVBoxLayout(stage_);
	stageLayout->setSpacing(size::gapSmall + 2);
	stage_->setPadding(12, 10, 12, 12);

	// Live and Up next pictures. Narrow panel: big Live, with Up next small beside its name.
	// Wide panel: the two side by side at equal size, Up next on the left like OBS's Preview.
	picturesGrid_ = new QGridLayout();
	picturesGrid_->setHorizontalSpacing(size::gap);
	picturesGrid_->setVerticalSpacing(size::gap - 4);
	liveView_ = new PictureView(stage_);
	liveView_->setRing(color::live, 1.5);
	liveView_->setPlaceholder(text("Laycue.WaitingForPicture"));
	liveView_->setAccessibleName(text("Laycue.LiveAccessible"));
	nextView_ = new PictureView(stage_);
	nextView_->setAccessibleName(text("Laycue.NextAccessible"));

	nextTextBox_ = new QWidget(stage_);
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
	stageLayout->addLayout(picturesGrid_);
	arrangePictures(false);

	// While a pushback is live: switch the camera inside it, or unpin it.
	insideBar_ = new QWidget(stage_);
	insideLayout_ = new QVBoxLayout(insideBar_);
	insideLayout_->setContentsMargins(0, 0, 0, 0);
	insideLayout_->setSpacing(size::gapSmall);
	insideBar_->hide();
	stageLayout->addWidget(insideBar_);

	// Cut (instant) and Go Live (OBS transition)
	auto *buttons = new QHBoxLayout();
	buttons->setSpacing(size::gapSmall);
	cutButton_ = new StageButton(StageButton::Kind::Cut, stage_);
	cutButton_->setTitle(text("Laycue.Cut"));
	cutButton_->setDetail(text("Laycue.Instant"));
	cutButton_->setToolTip(text("Laycue.CutTip"));
	connect(cutButton_, &QAbstractButton::clicked, this, [this]() { goLive(true); });
	buttons->addWidget(cutButton_, 2);

	goLiveButton_ = new StageButton(StageButton::Kind::GoLive, stage_);
	goLiveButton_->setTitle(text("Laycue.GoLive"));
	goLiveButton_->setToolTip(text("Laycue.GoLiveTip"));
	connect(goLiveButton_, &QAbstractButton::clicked, this, [this]() { goLive(false); });
	buttons->addWidget(goLiveButton_, 3);
	stageLayout->addLayout(buttons);
	root->addWidget(stage_);

	// The tabs: one raised surface holding whichever tab is showing.
	auto *tabSurface = new Surface(1, home);
	auto *tabSurfaceLayout = new QVBoxLayout(tabSurface);
	tabScroll_ = new QScrollArea(tabSurface);
	tabScroll_->setWidgetResizable(true);
	tabScroll_->setFrameShape(QFrame::NoFrame);
	tabScroll_->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
	auto *tabHost = new QWidget(tabScroll_);
	auto *tabHostLayout = new QVBoxLayout(tabHost);
	tabHostLayout->setContentsMargins(0, 0, 4, 0);
	tabHostLayout->setSpacing(0);

	// Shots tab
	shotsTab_ = new QWidget(tabHost);
	shotsTabLayout_ = new QVBoxLayout(shotsTab_);
	shotsTabLayout_->setContentsMargins(0, 0, 0, 0);
	shotsTabLayout_->setSpacing(size::gapSmall + 2);
	auto *shotsHint = makeLabel(shotsTab_, uiFont(12, 400), color::textSecondary, text("Laycue.ShotsHint"));
	shotsHint->setWordWrap(true);
	shotsTabLayout_->addWidget(shotsHint);
	shotsGuideHost_ = new QWidget(shotsTab_);
	auto *guideLayout = new QVBoxLayout(shotsGuideHost_);
	guideLayout->setContentsMargins(0, 0, 0, 0);
	shotsTabLayout_->addWidget(shotsGuideHost_);
	shotsGridHost_ = new QWidget(shotsTab_);
	shotsGrid_ = new QGridLayout(shotsGridHost_);
	shotsGrid_->setContentsMargins(0, 2, 0, 2);
	shotsGrid_->setHorizontalSpacing(size::gapSmall + 2);
	shotsGrid_->setVerticalSpacing(size::gapSmall + 2);
	shotsGrid_->setAlignment(Qt::AlignTop);
	shotsTabLayout_->addWidget(shotsGridHost_);
	emptyLabel_ = makeLabel(shotsTab_, uiFont(12, 400), color::textSecondary, text("Laycue.NoScenes"));
	emptyLabel_->setWordWrap(true);
	emptyLabel_->hide();
	shotsTabLayout_->addWidget(emptyLabel_);
	shotsTabLayout_->addStretch(1);
	tabHostLayout->addWidget(shotsTab_);

	// Layouts tab: what Laycue builds for you (split screens and pushbacks)
	pushbacksTab_ = new QWidget(tabHost);
	auto *layoutsLayout = new QVBoxLayout(pushbacksTab_);
	layoutsLayout->setContentsMargins(0, 0, 0, 0);
	layoutsLayout->setSpacing(size::gapSmall + 2);

	// The shapes a layout can have, in plain sight: tapping one starts that layout.
	shapesHost_ = new QWidget(pushbacksTab_);
	auto *shapesLayout = new QVBoxLayout(shapesHost_);
	shapesLayout->setContentsMargins(0, 0, 0, 0);
	shapesLayout->setSpacing(size::gapSmall);
	shapesLayout->addWidget(makeLabel(shapesHost_, uiFont(11, 700), color::textTertiary,
					  text("Laycue.Layouts.SectionShapes").toUpper()));
	auto *shapesHint =
		makeLabel(shapesHost_, uiFont(11, 400), color::textTertiary, text("Laycue.Layouts.SectionShapesHint"));
	shapesHint->setWordWrap(true);
	shapesLayout->addWidget(shapesHint);
	auto *shapeGridHost = new QWidget(shapesHost_);
	auto *shapeGrid = new QGridLayout(shapeGridHost);
	shapeGrid->setContentsMargins(0, 2, 0, 2);
	shapeGrid->setHorizontalSpacing(size::gapSmall);
	shapeGrid->setVerticalSpacing(size::gapSmall);
	const auto &shapes = layoutTemplates();
	for (size_t i = 0; i < shapes.size(); ++i) {
		auto *card = new ui::TemplateCard(shapes[i], shapeGridHost);
		connect(card, &QAbstractButton::clicked, this, [this, i]() { openNewLayout(int(i)); });
		shapeGrid->addWidget(card, int(i / 3), int(i % 3));
	}
	for (int column = 0; column < 3; ++column)
		shapeGrid->setColumnStretch(column, 1);
	shapesLayout->addWidget(shapeGridHost);
	layoutsLayout->addWidget(shapesHost_);

	pushbacksHeaderHost_ = new QWidget(pushbacksTab_);
	auto *makeRow = new QHBoxLayout(pushbacksHeaderHost_);
	makeRow->setContentsMargins(0, 0, 0, 0);
	makeRow->setSpacing(size::gapSmall);
	addPushbackButton_ =
		new ActionButton(text("Laycue.Pushbacks.Add"), ActionButton::Kind::Secondary, pushbacksHeaderHost_);
	connect(addPushbackButton_, &QAbstractButton::clicked, this, &LaycueDock::openAddPushback);
	makeRow->addWidget(addPushbackButton_);
	makeRow->addStretch(1);
	layoutsLayout->addWidget(pushbacksHeaderHost_);

	const auto gridHost = [&](QWidget **hostOut, QGridLayout **gridOut) {
		*hostOut = new QWidget(pushbacksTab_);
		*gridOut = new QGridLayout(*hostOut);
		(*gridOut)->setContentsMargins(0, 2, 0, 2);
		(*gridOut)->setHorizontalSpacing(size::gapSmall + 2);
		(*gridOut)->setVerticalSpacing(size::gapSmall + 2);
		(*gridOut)->setAlignment(Qt::AlignTop);
	};
	lyLabel_ = makeLabel(pushbacksTab_, uiFont(11, 700), color::textTertiary,
			     text("Laycue.Layouts.SectionLayouts").toUpper());
	layoutsLayout->addWidget(lyLabel_);
	gridHost(&lyGridHost_, &lyGrid_);
	layoutsLayout->addWidget(lyGridHost_);
	pbLabel_ = makeLabel(pushbacksTab_, uiFont(11, 700), color::textTertiary,
			     text("Laycue.Layouts.SectionPushbacks").toUpper());
	layoutsLayout->addWidget(pbLabel_);
	gridHost(&pbGridHost_, &pbGrid_);
	layoutsLayout->addWidget(pbGridHost_);

	// Nothing made yet: two clear choices.
	pbEmpty_ = new QWidget(pushbacksTab_);
	auto *emptyLayout = new QVBoxLayout(pbEmpty_);
	emptyLayout->setContentsMargins(0, 0, 0, 0);
	emptyLayout->setSpacing(size::gapSmall + 2);
	const auto choice = [&](const char *titleKey, const char *bodyKey, const char *buttonKey,
				ActionButton::Kind kind, void (LaycueDock::*open)(), QAbstractButton **buttonOut) {
		auto *card = new Surface(1, pbEmpty_);
		auto *v = new QVBoxLayout(card);
		v->setSpacing(size::gapSmall);
		card->setPadding(14, 12, 14, 12);
		v->addWidget(makeLabel(card, uiFont(14, 700), color::cream, text(titleKey)));
		auto *body = makeLabel(card, uiFont(12, 400), color::textSecondary, text(bodyKey));
		body->setWordWrap(true);
		v->addWidget(body);
		auto *button = new ActionButton(text(buttonKey), kind, card);
		connect(button, &QAbstractButton::clicked, this, open);
		v->addWidget(button, 0, Qt::AlignLeft);
		*buttonOut = button;
		emptyLayout->addWidget(card);
	};
	choice("Laycue.Pushbacks.EmptyTitle", "Laycue.Pushbacks.EmptyBody", "Laycue.Pushbacks.AddFirst",
	       ActionButton::Kind::Secondary, &LaycueDock::openAddPushback, &emptyAddButton_);
	layoutsLayout->addWidget(pbEmpty_);
	layoutsLayout->addStretch(1);
	pushbacksTab_->hide();
	tabHostLayout->addWidget(pushbacksTab_);

	// Overlays tab
	overlaysTab_ = new OverlaysTab(tabHost);
	overlaysTab_->hide();
	connect(overlaysTab_, &OverlaysTab::toggled, this,
		[this](const std::string &sceneUuid, int64_t itemId, bool show) {
			if (!setItemVisible(sceneUuid, itemId, show))
				showToast(text("Laycue.Overlays.Gone"));
		});
	connect(overlaysTab_, &OverlaysTab::setupRequested, this, &LaycueDock::openOverlaySetup);
	tabHostLayout->addWidget(overlaysTab_);
	tabHostLayout->addStretch(1);

	tabScroll_->setWidget(tabHost);
	tabScroll_->viewport()->installEventFilter(this); // re-size cards when the list width changes
	tabSurfaceLayout->addWidget(tabScroll_);
	tabSurface->setPadding(12, 12, 8, 8);
	root->addWidget(tabSurface, 1);

	return home;
}

void LaycueDock::showTab(Tab tab)
{
	tab_ = tab;
	shotsTab_->setVisible(tab == Tab::Shots);
	pushbacksTab_->setVisible(tab == Tab::Pushbacks);
	overlaysTab_->setVisible(tab == Tab::Overlays);
	tabs_->setCurrent(tab == Tab::Shots ? 0 : tab == Tab::Pushbacks ? 1 : 2);
	scheduleRefresh();
}

void LaycueDock::openPage(QWidget *page)
{
	closePage();
	openedPage_ = page;
	pages_->addWidget(page);
	pages_->setCurrentWidget(page);
	ui::setLargeControls(content_, app_.config().settings.largeButtons);
	setFocus(Qt::OtherFocusReason); // no focus ring on the first button just because a page opened
}

void LaycueDock::closePage()
{
	if (openedPage_) {
		pages_->setCurrentWidget(home_);
		pages_->removeWidget(openedPage_);
		openedPage_->deleteLater();
		openedPage_ = nullptr;
		setFocus(Qt::OtherFocusReason);
	}
	scheduleRefresh();
}

void LaycueDock::onConfigChanged()
{
	ui::setLargeControls(content_, app_.config().settings.largeButtons);

	updateLayoutsTab();
	updateGuide();

	if (obsReady_ && !shuttingDown_) {
		applyView();
		saveTimer_.start();
		scheduleRefresh();
	}
}

void LaycueDock::resizeEvent(QResizeEvent *event)
{
	QWidget::resizeEvent(event);
	using namespace ui;
	// Inside the stage: the panel width, less the page margin and the stage's own padding.
	const int contentWidth = width() - 2 * (size::margin - size::shadowMargin) - 2 * (size::shadowMargin + 12);
	// Up next and Live always sit side by side at equal size, like OBS's Preview and Program,
	// so the whole stage is visible at once even in a narrow panel.
	arrangePictures(true);
	const int half = std::max(80, (contentWidth - size::gap) / 2);
	liveView_->setFixedSize(half, half * 9 / 16);
	nextView_->setFixedSize(half, half * 9 / 16);
	layoutCards();
	placeToast();
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
	if (tabScroll_ && watched == tabScroll_->viewport() && event->type() == QEvent::Resize)
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
		applyView();
		scheduleRefresh();
		break;

	case OBS_FRONTEND_EVENT_STUDIO_MODE_ENABLED:
		pushNextToPreview_ = true;
		scheduleRefresh();
		break;

	case OBS_FRONTEND_EVENT_SCENE_LIST_CHANGED:
	case OBS_FRONTEND_EVENT_SCENE_CHANGED:
	case OBS_FRONTEND_EVENT_PREVIEW_SCENE_CHANGED:
	case OBS_FRONTEND_EVENT_STUDIO_MODE_DISABLED:
	case OBS_FRONTEND_EVENT_PROFILE_CHANGED:
		scheduleRefresh();
		break;

	case OBS_FRONTEND_EVENT_TRANSITION_STOPPED:
		// OBS swaps Preview and Program a moment after a transition ends. Keep ignoring Preview
		// changes for a short while, so that swap isn't taken for the operator cueing something.
		if (suppressPreview_)
			suppressTimer_.start(kSuppressGraceMs);
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
		saveTimer_.stop();
		watcher_->detachAll();
		closePage();
		resetForCollectionChange();
		break;

	case OBS_FRONTEND_EVENT_EXIT:
		finishCut();
		view_->set(false); // OBS's own screens come back before OBS saves its window
		shuttingDown_ = true;
		obsReady_ = false;
		refreshTimer_.stop();
		saveTimer_.stop();
		pictureTimer_.stop();
		watcher_->detachAll();
		grabber_->shutdown();
		break;

	default:
		break;
	}
}

void LaycueDock::applyView()
{
	const bool wanted = app_.config().settings.laycueView;
	if (wanted == view_->isOn())
		return;
	if (!view_->set(wanted)) {
		// OBS's window isn't what Laycue expects: leave OBS as it is and switch the setting off.
		app_.modify([](Config &c) { c.settings.laycueView = false; });
		showToast(text("Laycue.View.Unavailable"));
		return;
	}
	showToast(text(wanted ? "Laycue.View.On" : "Laycue.View.Off"));
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

		if (!repairChecked_) {
			repairChecked_ = true;
			repairPushbacks();
			repairLayouts();
		}

		// Every scene, the ones for the Shots tab (not Laycue's pushback scenes), and the pushbacks.
		std::vector<Shot> scenes;
		std::vector<Shot> shots;
		std::vector<Shot> pushbackShots;
		for (const SceneInfo &scene : snapshot.scenes) {
			scenes.push_back({scene.uuid, scene.name});
			if (pushbackForScene(scene.uuid) == nullptr && layoutForScene(scene.uuid) == nullptr)
				shots.push_back({scene.uuid, scene.name});
		}
		for (const Pushback &pushback : app_.config().pushbacks)
			pushbackShots.push_back({pushback.sceneUuid, pushback.name});
		std::vector<Shot> layoutShots;
		for (const Layout &layout : app_.config().layouts)
			layoutShots.push_back({layout.sceneUuid, layout.name});
		const auto same = [](const std::vector<Shot> &a, const std::vector<Shot> &b) {
			return a.size() == b.size() &&
			       std::equal(a.begin(), a.end(), b.begin(), [](const Shot &x, const Shot &y) {
				       return x.uuid == y.uuid && x.name == y.name;
			       });
		};
		if (!same(scenes, scenes_) || !same(pushbackShots, pushbackShots_) ||
		    !same(layoutShots, layoutShots_)) {
			for (const Shot &old : scenes_) {
				const bool stillThere = std::any_of(scenes.begin(), scenes.end(),
								    [&](const Shot &s) { return s.uuid == old.uuid; });
				if (!stillThere)
					shotRemoved(cue_, old.uuid);
			}
			scenes_ = std::move(scenes);
			shots_ = std::move(shots);
			pushbackShots_ = std::move(pushbackShots);
			layoutShots_ = std::move(layoutShots);
			rebuildCards();
		}

		liveChanged(cue_, snapshot.programSceneUuid);

		// Remember what was live before a pushback, so it can be unpinned back to it.
		if (cue_.live != lastLive_) {
			const bool liveIsPushback = pushbackForScene(cue_.live) != nullptr;
			if (liveIsPushback && pushbackForScene(lastLive_) == nullptr && !lastLive_.empty())
				previousLive_ = lastLive_;
			if (!liveIsPushback)
				previousLive_.clear();
			lastLive_ = cue_.live;
		}

		if (snapshot.studioMode) {
			// Studio Mode was just turned on: show what is cued in OBS's Preview.
			if (pushNextToPreview_) {
				pushNextToPreview_ = false;
				if (canTake(cue_) && snapshot.previewSceneUuid != cue_.next) {
					showInPreview(cue_.next);
					lastPreview_ = cue_.next;
				}
			}
			// When the operator changes OBS's Preview themselves, follow it as Up next.
			// Preview changes caused by our own Go Live (OBS swapping scenes) are ignored.
			if (snapshot.previewSceneUuid != lastPreview_) {
				if (!suppressPreview_ && !snapshot.previewSceneUuid.empty() &&
				    snapshot.previewSceneUuid != cue_.live)
					cue_.next = snapshot.previewSceneUuid;
				lastPreview_ = snapshot.previewSceneUuid;
			}
		} else {
			lastPreview_.clear();
			pushNextToPreview_ = false;
		}

		updateOverlays(snapshot);
		updatePictureTargets();
		updateStateUi();
		updateInsideBar();
	} catch (const std::exception &error) {
		obs_log(LOG_ERROR, "Failed to refresh the panel: %s", error.what());
		showToast(text("Laycue.ReadError"));
	} catch (...) {
		obs_log(LOG_ERROR, "Failed to refresh the panel: unknown error");
		showToast(text("Laycue.ReadError"));
	}
}

void LaycueDock::updateOverlays(const Snapshot &snapshot)
{
	const Config &config = app_.config();
	std::vector<ui::OverlayRow> rows;
	const double cw = snapshot.canvasWidth > 0 ? snapshot.canvasWidth : 1920;
	const double ch = snapshot.canvasHeight > 0 ? snapshot.canvasHeight : 1080;
	for (const OverlayItem &item : readSceneItems(snapshot.programSceneUuid)) {
		const std::string tag = tagOf(config, item.sourceUuid);
		if (tag.empty())
			continue;
		ui::OverlayRow row;
		row.sceneUuid = item.sceneUuid;
		row.itemId = item.itemId;
		row.sourceUuid = item.sourceUuid;
		row.name = fromStd(item.sourceName);
		row.tag = ui::tagDisplayName(tag);
		row.visible = item.visible;
		row.spot = {item.box.x / cw, item.box.y / ch, item.box.w / cw, item.box.h / ch};
		rows.push_back(std::move(row));
	}
	overlaysTab_->setRows(rows, fromStd(nameOf(snapshot.programSceneUuid)), !config.tags.empty(),
			      config.settings.locked);
}

void LaycueDock::rebuildCards()
{
	for (auto &entry : cards_)
		entry.second->deleteLater();
	cards_.clear();
	pictureSerials_.clear();

	for (const Shot &shot : shots_) {
		auto *card = new ui::ShotCard(shotsGridHost_);
		card->setName(fromStd(shot.name));
		card->setToolTip(text("Laycue.ShotTip").arg(fromStd(shot.name)));
		const std::string uuid = shot.uuid;
		connect(card, &QAbstractButton::clicked, this, [this, uuid]() { onShotClicked(uuid); });
		cards_[shot.uuid] = card;
	}
	for (const Layout &layout : app_.config().layouts) {
		auto *card = new ui::ShotCard(lyGridHost_);
		card->setName(fromStd(layout.name));
		const LayoutTemplate *shape = findTemplate(layout.templateId);
		card->setToolTip(
			text("Laycue.Layouts.Tip").arg(fromStd(layout.name), shape ? text(shape->nameKey) : QString()));
		card->setContextMenuPolicy(Qt::CustomContextMenu);
		const std::string uuid = layout.sceneUuid;
		const std::string id = layout.id;
		connect(card, &QAbstractButton::clicked, this, [this, uuid]() { onShotClicked(uuid); });
		connect(card, &QWidget::customContextMenuRequested, this,
			[this, id, card](const QPoint &pos) { layoutMenu(id, card->mapToGlobal(pos)); });
		cards_[layout.sceneUuid] = card;
	}
	for (const Pushback &pushback : app_.config().pushbacks) {
		auto *card = new ui::ShotCard(pbGridHost_);
		card->setName(fromStd(pushback.name));
		QStringList feedNames;
		for (const Feed &feed : pushback.feeds)
			feedNames << fromStd(feed.name);
		card->setToolTip(
			text("Laycue.PushbackTip").arg(fromStd(pushback.name), feedNames.join(QStringLiteral(", "))));
		card->setContextMenuPolicy(Qt::CustomContextMenu);
		const std::string uuid = pushback.sceneUuid;
		const std::string id = pushback.id;
		connect(card, &QAbstractButton::clicked, this, [this, uuid]() { onShotClicked(uuid); });
		connect(card, &QWidget::customContextMenuRequested, this,
			[this, id, card](const QPoint &pos) { pushbackMenu(id, card->mapToGlobal(pos)); });
		cards_[pushback.sceneUuid] = card;
	}
	emptyLabel_->setVisible(shots_.empty());
	shotsGridHost_->setVisible(!shots_.empty());
	updateLayoutsTab();
	layoutCards();
}

void LaycueDock::layoutCards()
{
	if (!shotsGrid_)
		return;
	const int available = tabScroll_->viewport()->width() - 4;
	layoutGrid(shotsGrid_, shots_, available);
	layoutGrid(lyGrid_, layoutShots_, available);
	layoutGrid(pbGrid_, pushbackShots_, available);
}

void LaycueDock::layoutGrid(QGridLayout *grid, const std::vector<Shot> &shots, int available)
{
	while (grid->count() > 0)
		delete grid->takeAt(0); // removes the layout item only; the card stays

	// Two cards across in a normal panel; more when the panel is wide, so cards stay small.
	const int columns = std::clamp(available / kCardMinWidth, 2, 6);
	const int columnWidth = std::max((available - grid->horizontalSpacing() * (columns - 1)) / columns, 80);
	// Equal columns, even when a row has one card, so a lone card isn't stretched across the row.
	for (int column = 0; column < 6; ++column)
		grid->setColumnStretch(column, column < columns ? 1 : 0);
	int index = 0;
	for (const Shot &shot : shots) {
		auto it = cards_.find(shot.uuid);
		if (it == cards_.end())
			continue;
		it->second->setFixedHeight(it->second->heightForWidth(columnWidth));
		grid->addWidget(it->second, index / columns, index % columns);
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
	if (!goLiveButton_)
		return;
	const TransitionInfo transition = currentTransition();
	QString detail = fromStd(transition.name);
	if (transition.durationMs > 0)
		detail += QStringLiteral(" · %1 s").arg(transition.durationMs / 1000.0, 0, 'f', 1);
	goLiveButton_->setDetail(detail);
}

void LaycueDock::updatePictureTargets()
{
	std::vector<PictureTarget> targets;
	targets.push_back({FrameGrabber::kProgram, kLiveWidth, kLiveHeight, kLiveIntervalMs});
	for (const Shot &shot : scenes_) {
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
		if (const Pushback *pushback = pushbackForScene(uuid))
			layoutPushback(*pushback); // a camera's real size is only known once OBS has drawn it
		if (const Layout *layout = layoutForScene(uuid))
			fitLayout(*layout);
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

	if (const Pushback *pushback = pushbackForScene(target))
		layoutPushback(*pushback);
	if (const Layout *layout = layoutForScene(target))
		fitLayout(*layout);

	suppressPreview_ = studioModeActive();
	if (suppressPreview_)
		suppressTimer_.start(kSuppressFallbackMs);

	if (!takeLive(target, instant, transitionToRestore_)) {
		shotRemoved(cue_, target);
		showToast(text("Laycue.SceneGone"));
		updateStateUi();
		return;
	}
	if (!transitionToRestore_.empty())
		restoreTimer_.start();
	take(cue_);
	if (!app_.config().wentLive)
		app_.modify([](Config &c) { c.wentLive = true; });
	showToast(text(instant ? "Laycue.CutDone" : "Laycue.LiveDone").arg(name));
	updatePictureTargets();
	updateStateUi();
}

const Layout *LaycueDock::layoutForScene(const std::string &sceneUuid) const
{
	if (sceneUuid.empty())
		return nullptr;
	for (const Layout &layout : app_.config().layouts) {
		if (layout.sceneUuid == sceneUuid)
			return &layout;
	}
	return nullptr;
}

const Pushback *LaycueDock::pushbackForScene(const std::string &sceneUuid) const
{
	if (sceneUuid.empty())
		return nullptr;
	for (const Pushback &pushback : app_.config().pushbacks) {
		if (pushback.sceneUuid == sceneUuid)
			return &pushback;
	}
	return nullptr;
}

void LaycueDock::updateLayoutsTab()
{
	const Config &config = app_.config();
	const bool locked = config.settings.locked;
	lockPill_->setVisible(locked);
	const bool anyLayouts = !config.layouts.empty();
	const bool anyPushbacks = !config.pushbacks.empty();
	shapesHost_->setVisible(!locked);
	pbEmpty_->setVisible(!anyPushbacks && !locked);
	emptyAddButton_->setVisible(!locked);
	pushbacksHeaderHost_->setVisible(anyPushbacks && !locked);
	lyLabel_->setVisible(anyLayouts);
	lyGridHost_->setVisible(anyLayouts);
	pbLabel_->setVisible(anyPushbacks);
	pbGridHost_->setVisible(anyPushbacks);
}

void LaycueDock::updateGuide()
{
	using namespace ui;
	auto *layout = shotsGuideHost_->layout();
	page::clear(layout);

	const Config &config = app_.config();
	if (!config.settings.showTips || config.settings.locked || tipDismissed(config, "guide"))
		return;

	struct Step {
		QString title;
		QString sub;
		bool done;
		QString action;
		std::function<void()> run;
	};
	const std::vector<Step> steps = {
		{text("Laycue.Guide.Overlays"), text("Laycue.Guide.OverlaysSub"), !config.tags.empty(),
		 text("Laycue.Guide.OverlaysAction"),
		 [this]() {
			 openOverlaySetup();
		 }},
		{text("Laycue.Guide.Pushback"), text("Laycue.Guide.PushbackSub"),
		 !config.pushbacks.empty() || !config.layouts.empty(), text("Laycue.Guide.PushbackAction"),
		 [this]() {
			 showTab(Tab::Pushbacks);
		 }},
		{text("Laycue.Guide.GoLive"), text("Laycue.Guide.GoLiveSub"), config.wentLive, QString(), nullptr},
	};
	// One step at a time: the first one not done yet. When all are done the guide goes away.
	const auto next = std::find_if(steps.begin(), steps.end(), [](const Step &step) { return !step.done; });
	if (next == steps.end())
		return;
	const int number = int(next - steps.begin()) + 1;

	auto *card = new Surface(1, shotsGuideHost_);
	auto *row = new QHBoxLayout(card);
	row->setSpacing(size::gapSmall + 2);
	card->setPadding(12, 8, 8, 8);

	auto *textColumn = new QVBoxLayout();
	textColumn->setSpacing(1);
	textColumn->addWidget(makeLabel(card, uiFont(10, 700), color::setup,
					text("Laycue.Guide.Step").arg(number).arg(int(steps.size())).toUpper()));
	textColumn->addWidget(makeLabel(card, uiFont(13, 600), color::cream, next->title));
	auto *sub = makeLabel(card, uiFont(11, 400), color::textSecondary, next->sub);
	sub->setWordWrap(true);
	textColumn->addWidget(sub);
	row->addLayout(textColumn, 1);

	if (next->run) {
		auto *action = new ActionButton(next->action, ActionButton::Kind::Secondary, card);
		const auto run = next->run;
		connect(action, &QAbstractButton::clicked, this, [run]() { run(); });
		row->addWidget(action, 0, Qt::AlignVCenter);
	}
	auto *hide = new ActionButton(text("Laycue.Guide.Hide"), ActionButton::Kind::Link, card);
	connect(hide, &QAbstractButton::clicked, this,
		[this]() { app_.modify([](Config &c) { dismissTip(c, "guide"); }); });
	row->addWidget(hide, 0, Qt::AlignVCenter);
	layout->addWidget(card);
}

void LaycueDock::openOverlaySetup()
{
	auto *page = new ui::OverlaySetupPage(app_);
	connect(page, &ui::PageFrame::backRequested, this, &LaycueDock::closePage);
	openPage(page);
}

void LaycueDock::openSettings()
{
	auto *page = new ui::SettingsPage(app_);
	connect(page, &ui::PageFrame::backRequested, this, &LaycueDock::closePage);
	connect(page, &ui::SettingsPage::overlaySetupRequested, this, [this]() { openOverlaySetup(); });
	connect(page, &ui::SettingsPage::studioRequested, this, [this](bool studio) { chooseMode(studio); });
	connect(page, &ui::SettingsPage::refitAllRequested, this, [this]() {
		for (const Pushback &pushback : app_.config().pushbacks)
			layoutPushback(pushback);
		for (const Layout &layout : app_.config().layouts)
			fitLayout(layout);
	});
	openPage(page);
}

void LaycueDock::openAddPushback()
{
	auto *page = new ui::AddPushbackPage(app_);
	connect(page, &ui::PageFrame::backRequested, this, [this]() {
		closePage();
		showTab(Tab::Pushbacks);
	});
	connect(page, &ui::AddPushbackPage::created, this, [this](const std::string &) { scheduleRefresh(); });
	openPage(page);
}

void LaycueDock::openNewLayout(int shape)
{
	auto *page = new ui::NewLayoutPage(app_, shape);
	connect(page, &ui::PageFrame::backRequested, this, [this]() {
		closePage();
		showTab(Tab::Pushbacks);
	});
	connect(page, &ui::NewLayoutPage::created, this, [this](const std::string &, const QString &name) {
		showToast(text("Laycue.Layouts.Made").arg(name));
		scheduleRefresh();
	});
	openPage(page);
}

void LaycueDock::repairLayouts()
{
	// A layout's scene can go missing (deleted in OBS). It is Laycue's own scene, so rebuild it.
	std::vector<Layout> rebuilt;
	for (const Layout &layout : app_.config().layouts) {
		if (layoutSceneExists(layout))
			continue;
		const LayoutBuildResult result = buildLayoutScene(layout);
		if (result.ok) {
			rebuilt.push_back(result.layout);
			showToast(text("Laycue.Layouts.Rebuilt").arg(fromStd(layout.name)));
		} else {
			obs_log(LOG_WARNING, "Could not rebuild the scene for layout '%s': %s", layout.name.c_str(),
				result.error.c_str());
		}
	}
	if (rebuilt.empty())
		return;
	app_.modify([&](Config &config) {
		for (const Layout &fixed : rebuilt) {
			for (Layout &l : config.layouts) {
				if (l.id == fixed.id) {
					l.sceneUuid = fixed.sceneUuid;
					l.sceneName = fixed.sceneName;
				}
			}
		}
	});
}

void LaycueDock::layoutMenu(const std::string &layoutId, const QPoint &globalPos)
{
	const auto found = std::find_if(app_.config().layouts.begin(), app_.config().layouts.end(),
					[&](const Layout &l) { return l.id == layoutId; });
	if (found == app_.config().layouts.end())
		return;
	const Layout layout = *found;
	const bool locked = app_.config().settings.locked;

	QMenu menu(this);
	QAction *refit = menu.addAction(text("Laycue.Layouts.Refit"));
	connect(refit, &QAction::triggered, this, [this, layout]() {
		fitLayout(layout);
		showToast(text("Laycue.Pushbacks.Refitted").arg(fromStd(layout.name)));
	});
	if (!locked) {
		QAction *rename = menu.addAction(text("Laycue.Layouts.Rename"));
		connect(rename, &QAction::triggered, this, [this, layout]() {
			bool ok = false;
			const QString name = QInputDialog::getText(this, text("Laycue.Layouts.Rename"),
								   text("Laycue.Add.Name"), QLineEdit::Normal,
								   fromStd(layout.name), &ok)
						     .trimmed();
			if (!ok || name.isEmpty() || name.toStdString() == layout.name)
				return;
			renameLayoutScene(layout, name.toStdString());
			app_.modify([&](Config &c) {
				for (Layout &l : c.layouts) {
					if (l.id == layout.id)
						l.name = name.toStdString();
				}
			});
			scheduleRefresh();
		});
		menu.addSeparator();
		QAction *remove = menu.addAction(text("Laycue.Layouts.Remove"));
		connect(remove, &QAction::triggered, this, [this, layout]() {
			if (cue_.live == layout.sceneUuid) {
				showToast(text("Laycue.Pushbacks.RemoveLive"));
				return;
			}
			const auto answer =
				QMessageBox::question(this, text("Laycue.Layouts.RemoveTitle"),
						      text("Laycue.Layouts.RemoveBody").arg(fromStd(layout.name)));
			if (answer != QMessageBox::Yes)
				return;
			removeLayoutScene(layout);
			shotRemoved(cue_, layout.sceneUuid);
			app_.modify([&](Config &c) {
				c.layouts.erase(std::remove_if(c.layouts.begin(), c.layouts.end(),
							       [&](const Layout &l) { return l.id == layout.id; }),
						c.layouts.end());
			});
			scheduleRefresh();
		});
	}
	menu.exec(globalPos);
}

void LaycueDock::repairPushbacks()
{
	// A pushback's scene can go missing (deleted in OBS). It is Laycue's own scene, so rebuild it.
	std::vector<Pushback> rebuilt;
	for (const Pushback &pushback : app_.config().pushbacks) {
		if (pushbackSceneExists(pushback))
			continue;
		const BuildResult result = buildPushbackScene(pushback);
		if (result.ok) {
			rebuilt.push_back(result.pushback);
			showToast(text("Laycue.Pushbacks.Rebuilt").arg(fromStd(pushback.name)));
		} else {
			obs_log(LOG_WARNING, "Could not rebuild the scene for pushback '%s': %s", pushback.name.c_str(),
				result.error.c_str());
		}
	}
	if (rebuilt.empty())
		return;
	app_.modify([&](Config &config) {
		for (const Pushback &fixed : rebuilt) {
			for (Pushback &p : config.pushbacks) {
				if (p.id == fixed.id) {
					p.sceneUuid = fixed.sceneUuid;
					p.sceneName = fixed.sceneName;
				}
			}
		}
	});
}

void LaycueDock::pushbackMenu(const std::string &pushbackId, const QPoint &globalPos)
{
	const auto found = std::find_if(app_.config().pushbacks.begin(), app_.config().pushbacks.end(),
					[&](const Pushback &p) { return p.id == pushbackId; });
	if (found == app_.config().pushbacks.end())
		return;
	const Pushback pushback = *found;
	const bool locked = app_.config().settings.locked;

	QMenu menu(this);
	QAction *refit = menu.addAction(text("Laycue.Pushbacks.Refit"));
	connect(refit, &QAction::triggered, this, [this, pushback]() {
		layoutPushback(pushback);
		showToast(text("Laycue.Pushbacks.Refitted").arg(fromStd(pushback.name)));
	});
	if (!locked) {
		QAction *rename = menu.addAction(text("Laycue.Pushbacks.Rename"));
		connect(rename, &QAction::triggered, this, [this, pushback]() {
			bool ok = false;
			const QString name = QInputDialog::getText(this, text("Laycue.Pushbacks.Rename"),
								   text("Laycue.Add.Name"), QLineEdit::Normal,
								   fromStd(pushback.name), &ok)
						     .trimmed();
			if (!ok || name.isEmpty() || name.toStdString() == pushback.name)
				return;
			for (const Pushback &other : app_.config().pushbacks) {
				if (other.id != pushback.id &&
				    fromStd(other.name).compare(name, Qt::CaseInsensitive) == 0) {
					showToast(text("Laycue.Add.NameTaken"));
					return;
				}
			}
			renamePushbackScene(pushback, name.toStdString());
			app_.modify([&](Config &c) {
				for (Pushback &p : c.pushbacks) {
					if (p.id == pushback.id)
						p.name = name.toStdString();
				}
			});
			scheduleRefresh();
		});
		menu.addSeparator();
		QAction *remove = menu.addAction(text("Laycue.Pushbacks.Remove"));
		connect(remove, &QAction::triggered, this, [this, pushback]() {
			if (cue_.live == pushback.sceneUuid) {
				showToast(text("Laycue.Pushbacks.RemoveLive"));
				return;
			}
			const auto answer =
				QMessageBox::question(this, text("Laycue.Pushbacks.Remove"),
						      text("Laycue.Pushbacks.RemoveAsk").arg(fromStd(pushback.name)));
			if (answer != QMessageBox::Yes)
				return;
			removePushbackScene(pushback);
			shotRemoved(cue_, pushback.sceneUuid);
			app_.modify([&](Config &c) {
				c.pushbacks.erase(
					std::remove_if(c.pushbacks.begin(), c.pushbacks.end(),
						       [&](const Pushback &p) { return p.id == pushback.id; }),
					c.pushbacks.end());
			});
			scheduleRefresh();
		});
	}
	menu.exec(globalPos);
}

void LaycueDock::updateInsideBar()
{
	using namespace ui;
	const Pushback *pushback = pushbackForScene(cue_.live);

	std::string key;
	if (pushback) {
		key = pushback->id + "|" + pushback->activeFeed + "|" + previousLive_;
		for (const Feed &feed : pushback->feeds)
			key += "|" + feed.uuid + ":" + feed.name;
	}
	if (key == insideKey_)
		return;
	insideKey_ = key;

	page::clear(insideLayout_);
	if (!pushback) {
		insideBar_->hide();
		return;
	}

	// Two plain widgets, each with its own layout: the title (and Unpin), then the camera buttons.
	const int columns = 3;
	auto *headerRow = new QWidget(insideBar_);
	auto *headerLayout = new QHBoxLayout(headerRow);
	headerLayout->setContentsMargins(0, 0, 0, 0);
	headerLayout->setSpacing(size::gapSmall);
	headerLayout->addWidget(
		makeLabel(headerRow, uiFont(12, 700), color::textSecondary, text("Laycue.Inside.Title")), 1);
	if (!previousLive_.empty()) {
		auto *unpinButton =
			new ActionButton(text("Laycue.Inside.Unpin"), ActionButton::Kind::Secondary, headerRow);
		unpinButton->setToolTip(text("Laycue.Inside.UnpinTip").arg(fromStd(nameOf(previousLive_))));
		connect(unpinButton, &QAbstractButton::clicked, this, &LaycueDock::unpin);
		headerLayout->addWidget(unpinButton);
	}
	insideLayout_->addWidget(headerRow);

	auto *feedRow = new QWidget(insideBar_);
	auto *feedLayout = new QGridLayout(feedRow);
	feedLayout->setContentsMargins(0, 0, 0, 0);
	feedLayout->setHorizontalSpacing(size::gapSmall);
	feedLayout->setVerticalSpacing(size::gapSmall);
	for (size_t i = 0; i < pushback->feeds.size(); ++i) {
		const Feed &feed = pushback->feeds[i];
		auto *button = new FeedButton(fromStd(feed.name), feedRow);
		button->setChecked(feed.uuid == pushback->activeFeed);
		button->setToolTip(fromStd(feed.name));
		const std::string id = pushback->id;
		const std::string feedUuid = feed.uuid;
		connect(button, &QPushButton::clicked, this, [this, id, feedUuid]() { switchInside(id, feedUuid); });
		feedLayout->addWidget(button, int(i) / columns, int(i) % columns);
	}
	for (int column = 0; column < columns; ++column)
		feedLayout->setColumnStretch(column, 1);
	insideLayout_->addWidget(feedRow);
	insideBar_->show();
	// The bar appears while the panel is showing: make the stage take its new height now.
	insideLayout_->invalidate();
	insideBar_->updateGeometry();
	stage_->layout()->invalidate();
	stage_->updateGeometry();
	home_->layout()->activate();
}

void LaycueDock::switchInside(const std::string &pushbackId, const std::string &feedUuid)
{
	const auto found = std::find_if(app_.config().pushbacks.begin(), app_.config().pushbacks.end(),
					[&](const Pushback &p) { return p.id == pushbackId; });
	if (found == app_.config().pushbacks.end() || found->activeFeed == feedUuid)
		return;
	Pushback updated = *found;
	const Settings settings = app_.config().settings;

	try {
		layoutPushback(updated, false); // fit to the cameras' real sizes without changing what shows
		updated.activeFeed = feedUuid;
		if (!showFeed(updated, feedUuid, settings.insideFade, settings.insideFadeMs)) {
			showToast(text("Laycue.Inside.Failed"));
			return;
		}
		app_.modify([&](Config &c) {
			for (Pushback &p : c.pushbacks) {
				if (p.id == pushbackId)
					p.activeFeed = feedUuid;
			}
		});
		if (settings.insideFade && settings.insideFadeMs > 0) {
			// When the fade is over, hide the old camera (whichever is not the active one by then).
			QTimer::singleShot(settings.insideFadeMs + 60, this, [this, pushbackId]() {
				for (const Pushback &p : app_.config().pushbacks) {
					if (p.id == pushbackId)
						hideInactiveFeeds(p);
				}
			});
		}
	} catch (const std::exception &error) {
		obs_log(LOG_ERROR, "Could not switch the camera inside the pushback: %s", error.what());
		showToast(text("Laycue.Inside.Failed"));
	}
}

void LaycueDock::runHotkey(HotkeyAction action)
{
	if (!obsReady_ || shuttingDown_)
		return;
	switch (action) {
	case HotkeyAction::GoLive:
		goLive(false);
		break;
	case HotkeyAction::Cut:
		goLive(true);
		break;
	case HotkeyAction::Unpin:
		unpin();
		break;
	case HotkeyAction::NextCamera:
		stepCamera(1);
		break;
	case HotkeyAction::PreviousCamera:
		stepCamera(-1);
		break;
	case HotkeyAction::ClearUpNext:
		cue_.next.clear();
		updatePictureTargets();
		updateStateUi();
		break;
	}
}

// The live pushback shows the next (or previous) camera in its list, wrapping around.
void LaycueDock::stepCamera(int direction)
{
	const Pushback *pushback = pushbackForScene(cue_.live);
	if (!pushback || pushback->feeds.size() < 2)
		return;
	const auto &feeds = pushback->feeds;
	size_t index = 0;
	for (size_t i = 0; i < feeds.size(); ++i) {
		if (feeds[i].uuid == pushback->activeFeed)
			index = i;
	}
	const size_t count = feeds.size();
	const size_t next = (index + count + (direction > 0 ? 1 : count - 1)) % count;
	switchInside(pushback->id, feeds[next].uuid);
}

void LaycueDock::unpin()
{
	if (previousLive_.empty())
		return;
	// Go back to what was live before the pushback, without disturbing what is cued as Up next.
	const std::string target = previousLive_;
	const std::string keepNext = cue_.next;
	cue_.next = target;
	goLive(false);
	if (keepNext != target)
		cue_.next = keepNext;
	updateStateUi();
}

void LaycueDock::onItemVisible(const std::string &sceneUuid, int64_t itemId, bool visible)
{
	if (!obsReady_ || shuttingDown_)
		return;
	try {
		const auto replaced = overlayEngine_.handleItemVisible(sceneUuid, itemId, visible);
		if (!replaced)
			return;
		QStringList names;
		for (const std::string &name : replaced->hiddenNames)
			names << fromStd(name);
		showToast(text(replaced->sameSpot ? "Laycue.Overlays.Replaced" : "Laycue.Overlays.ReplacedStrict")
				  .arg(fromStd(replaced->shownName), names.join(QStringLiteral(", "))));
		scheduleRefresh();
	} catch (const std::exception &error) {
		obs_log(LOG_ERROR, "Overlay rules failed: %s", error.what());
	} catch (...) {
		obs_log(LOG_ERROR, "Overlay rules failed: unknown error");
	}
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

void LaycueDock::chooseMode(bool studio)
{
	if (studioModeActive() == studio)
		return;
	obs_frontend_set_preview_program_mode(studio);
	showToast(text(studio ? "Laycue.Mode.StudioOn" : "Laycue.Mode.NormalOn"));
	scheduleRefresh();
}

void LaycueDock::showToast(const QString &message)
{
	toastText_->setText(message);
	toast_->adjustSize();
	placeToast();
	toast_->show();
	toast_->raise();
	toastTimer_.start();
}

void LaycueDock::placeToast()
{
	if (!toast_ || !content_)
		return;
	toast_->adjustSize();
	const int x = std::max(8, (content_->width() - toast_->width()) / 2);
	const int y = content_->height() - toast_->height() - 16;
	toast_->move(x, std::max(0, y));
}

std::string LaycueDock::nameOf(const std::string &uuid) const
{
	if (const Pushback *pushback = pushbackForScene(uuid))
		return pushback->name;
	for (const Shot &shot : scenes_) {
		if (shot.uuid == uuid)
			return shot.name;
	}
	return std::string();
}

void LaycueDock::resetForCollectionChange()
{
	grabber_->setTargets({});
	scenes_.clear();
	shots_.clear();
	pushbackShots_.clear();
	layoutShots_.clear();
	lastLive_.clear();
	previousLive_.clear();
	repairChecked_ = false;
	insideKey_.clear();
	cue_ = CueState();
	lastPreview_.clear();
	suppressPreview_ = false;
	pushNextToPreview_ = false;
	rebuildCards();
	liveView_->clearImage();
	nextView_->clearImage();
	updateStateUi();
}

} // namespace laycue
