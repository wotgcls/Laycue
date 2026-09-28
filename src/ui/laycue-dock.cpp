#include "ui/laycue-dock.hpp"

#include "obs/scene-reader.hpp"

#include <obs-module.h>
#include <plugin-support.h>

#include <QFont>
#include <QGuiApplication>
#include <QLabel>
#include <QPalette>
#include <QPixmap>
#include <QScrollBar>
#include <QTreeWidget>
#include <QVBoxLayout>

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

constexpr int kRefreshDelayMs = 50; // coalesce bursts of changes into one refresh
constexpr int kSceneUuidRole = Qt::UserRole;

// Opens every group row under `row`. Rows can only be expanded once they are in the tree.
void expandGroups(QTreeWidgetItem *row)
{
	for (int i = 0; i < row->childCount(); ++i) {
		QTreeWidgetItem *child = row->child(i);
		if (child->childCount() > 0) {
			child->setExpanded(true);
			expandGroups(child);
		}
	}
}

constexpr int kLogoHeight = 18; // logical pixels

// The Laycue logo for the panel header, picked for the current OBS theme (cream on dark, ink on light).
// Returns an empty label if the image is missing, so a broken install never breaks the panel.
QLabel *makeLogo(QWidget *parent)
{
	auto *logo = new QLabel(parent);
	const bool darkTheme = parent->palette().color(QPalette::Window).lightness() < 128;
	const char *file = darkTheme ? "brand/laycue-lockup-on-dark.png" : "brand/laycue-lockup-on-light.png";

	char *path = obs_module_file(file);
	if (!path) {
		obs_log(LOG_WARNING, "Logo image not found: %s", file);
		return logo;
	}
	QPixmap image(QString::fromUtf8(path));
	bfree(path);
	if (image.isNull())
		return logo;

	const qreal ratio = qGuiApp ? qGuiApp->devicePixelRatio() : 1.0;
	QPixmap scaled = image.scaledToHeight(qRound(kLogoHeight * ratio), Qt::SmoothTransformation);
	scaled.setDevicePixelRatio(ratio);
	logo->setPixmap(scaled);
	logo->setAccessibleName(QStringLiteral("Laycue"));
	return logo;
}

} // namespace

LaycueDock::LaycueDock(QWidget *parent) : QWidget(parent)
{
	setObjectName("LaycueDock");
	setMinimumWidth(200);

	auto *layout = new QVBoxLayout(this);
	layout->setContentsMargins(6, 6, 6, 6);
	layout->setSpacing(6);

	layout->addWidget(makeLogo(this));

	summaryLabel_ = new QLabel(this);
	summaryLabel_->setWordWrap(true);
	layout->addWidget(summaryLabel_);

	messageLabel_ = new QLabel(this);
	messageLabel_->setWordWrap(true);
	messageLabel_->hide();
	layout->addWidget(messageLabel_);

	tree_ = new QTreeWidget(this);
	tree_->setHeaderHidden(true);
	tree_->setColumnCount(1);
	tree_->setUniformRowHeights(true);
	tree_->setSelectionMode(QAbstractItemView::NoSelection);
	tree_->setFocusPolicy(Qt::NoFocus);
	layout->addWidget(tree_, 1);

	connect(tree_, &QTreeWidget::itemExpanded, this, [this](QTreeWidgetItem *row) {
		const std::string uuid = row->data(0, kSceneUuidRole).toString().toStdString();
		if (uuid.empty())
			return;
		expandedScenes_.insert(uuid);
		collapsedScenes_.erase(uuid);
	});
	connect(tree_, &QTreeWidget::itemCollapsed, this, [this](QTreeWidgetItem *row) {
		const std::string uuid = row->data(0, kSceneUuidRole).toString().toStdString();
		if (uuid.empty())
			return;
		collapsedScenes_.insert(uuid);
		expandedScenes_.erase(uuid);
	});

	refreshTimer_.setSingleShot(true);
	refreshTimer_.setInterval(kRefreshDelayMs);
	connect(&refreshTimer_, &QTimer::timeout, this, &LaycueDock::refreshNow);

	watcher_ = std::make_unique<SceneWatcher>([this]() { scheduleRefresh(); });

	summaryLabel_->setText(text("Laycue.Loading"));
}

LaycueDock::~LaycueDock()
{
	// Stop OBS signals first so nothing calls back into a half-destroyed dock.
	watcher_.reset();
}

void LaycueDock::handleFrontendEvent(enum obs_frontend_event event)
{
	switch (event) {
	case OBS_FRONTEND_EVENT_FINISHED_LOADING:
	case OBS_FRONTEND_EVENT_SCENE_COLLECTION_CHANGED:
		obsReady_ = true;
		scheduleRefresh();
		break;

	case OBS_FRONTEND_EVENT_SCENE_LIST_CHANGED:
	case OBS_FRONTEND_EVENT_SCENE_CHANGED:
	case OBS_FRONTEND_EVENT_PREVIEW_SCENE_CHANGED:
	case OBS_FRONTEND_EVENT_STUDIO_MODE_ENABLED:
	case OBS_FRONTEND_EVENT_STUDIO_MODE_DISABLED:
	case OBS_FRONTEND_EVENT_PROFILE_CHANGED: // canvas size can change with the profile
		scheduleRefresh();
		break;

	case OBS_FRONTEND_EVENT_SCENE_COLLECTION_CLEANUP:
		// OBS is about to free every scene. Let go of them before it does.
		obsReady_ = false;
		refreshTimer_.stop();
		watcher_->detachAll();
		tree_->clear();
		summaryLabel_->setText(text("Laycue.Loading"));
		break;

	case OBS_FRONTEND_EVENT_EXIT:
		shuttingDown_ = true;
		obsReady_ = false;
		refreshTimer_.stop();
		watcher_->detachAll();
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
		// Re-attach every time: new scenes or groups may have appeared since last time.
		watcher_->attachAll();
		populate(readSnapshot());
		messageLabel_->hide();
	} catch (const std::exception &error) {
		obs_log(LOG_ERROR, "Failed to read scenes: %s", error.what());
		showMessage(text("Laycue.ReadError"));
	} catch (...) {
		obs_log(LOG_ERROR, "Failed to read scenes: unknown error");
		showMessage(text("Laycue.ReadError"));
	}
}

void LaycueDock::showMessage(const QString &message)
{
	messageLabel_->setText(message);
	messageLabel_->show();
}

void LaycueDock::populate(const Snapshot &snapshot)
{
	summaryLabel_->setText(text("Laycue.Summary")
				       .arg(static_cast<qulonglong>(snapshot.scenes.size()))
				       .arg(static_cast<uint>(snapshot.canvasWidth))
				       .arg(static_cast<uint>(snapshot.canvasHeight)));

	const int scrollPosition = tree_->verticalScrollBar() ? tree_->verticalScrollBar()->value() : 0;
	const QSignalBlocker blocker(tree_); // don't record our own expand/collapse calls
	tree_->clear();

	if (snapshot.scenes.empty()) {
		showMessage(text("Laycue.NoScenes"));
		return;
	}

	for (const SceneInfo &scene : snapshot.scenes) {
		const bool isProgram = !scene.uuid.empty() && scene.uuid == snapshot.programSceneUuid;
		const bool isPreview = !scene.uuid.empty() && scene.uuid == snapshot.previewSceneUuid;

		QString label = fromStd(scene.name);
		if (isProgram)
			label += QStringLiteral("  ·  ") + text("Laycue.Live");
		if (isPreview)
			label += QStringLiteral("  ·  ") + text("Laycue.Preview");

		auto *sceneRow = new QTreeWidgetItem(tree_, QStringList(label));
		sceneRow->setData(0, kSceneUuidRole, fromStd(scene.uuid));
		QFont font = sceneRow->font(0);
		font.setBold(true);
		sceneRow->setFont(0, font);

		if (scene.items.empty()) {
			auto *emptyRow = new QTreeWidgetItem(sceneRow, QStringList(text("Laycue.EmptyScene")));
			emptyRow->setDisabled(true);
		}
		for (const ItemInfo &item : scene.items)
			sceneRow->addChild(makeItemRow(item));
		expandGroups(sceneRow);

		// Live and preview scenes start open; after that, remember what the operator chose.
		bool expand = isProgram || isPreview;
		if (expandedScenes_.count(scene.uuid))
			expand = true;
		if (collapsedScenes_.count(scene.uuid))
			expand = false;
		sceneRow->setExpanded(expand);
	}

	if (tree_->verticalScrollBar())
		tree_->verticalScrollBar()->setValue(scrollPosition);
}

QTreeWidgetItem *LaycueDock::makeItemRow(const ItemInfo &item)
{
	QString label = fromStd(item.sourceName) + QStringLiteral("  —  ") + text(kindLocaleKey(item.kind));
	if (!item.visible)
		label += QStringLiteral("  (") + text("Laycue.Hidden") + QStringLiteral(")");

	auto *row = new QTreeWidgetItem(QStringList(label));
	row->setToolTip(0, fromStd(item.sourceName) + QStringLiteral("\n") + fromStd(item.typeId));
	if (!item.visible)
		row->setForeground(0, palette().color(QPalette::Disabled, QPalette::Text));

	for (const ItemInfo &child : item.children)
		row->addChild(makeItemRow(child));
	return row;
}

} // namespace laycue
