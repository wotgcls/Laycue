#include "ui/pages/settings-page.hpp"

#include "core/config.hpp"
#include "features/app.hpp"
#include "features/profiles.hpp"
#include "obs/overlay-items.hpp"
#include "obs/scene-reader.hpp"
#include "obs/switcher.hpp"
#include "ui/controls.hpp"
#include "ui/labels.hpp"
#include "ui/pages/helpers.hpp"
#include "ui/tokens.hpp"
#include "ui/update-card.hpp"

#include <plugin-support.h>

#include <QComboBox>
#include <QFileDialog>
#include <QFrame>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QMessageBox>
#include <QVBoxLayout>

#include <algorithm>
#include <functional>
#include <set>

namespace laycue::ui {

namespace {

using page::button;
using page::innerLabel;
using page::label;

QFrame *divider(QWidget *parent)
{
	auto *line = new QFrame(parent);
	line->setFixedHeight(1);
	line->setStyleSheet(QStringLiteral("background: rgba(255,255,255,14); border: none;"));
	return line;
}

// A titled group: a small caption above a raised card. Returns the card's vertical layout.
QVBoxLayout *group(QVBoxLayout *page, QWidget *parent, const QString &title, QWidget **cardOut = nullptr)
{
	page->addWidget(label(parent, title.toUpper(), 11, 700, color::textTertiary));
	auto *card = new Surface(1, parent);
	auto *v = new QVBoxLayout(card);
	v->setSpacing(0);
	card->setPadding(14, 4, 14, 4);
	page->addWidget(card);
	if (cardOut)
		*cardOut = card;
	return v;
}

// label (and small text) on the left, a control on the right.
QWidget *row(QWidget *parent, const QString &title, const QString &sub, QWidget *control)
{
	auto *r = new QWidget(parent);
	auto *h = new QHBoxLayout(r);
	h->setContentsMargins(0, 8, 0, 8);
	h->setSpacing(size::gap);
	auto *text = new QVBoxLayout();
	text->setSpacing(1);
	text->addWidget(innerLabel(r, title, 13, 500, color::cream));
	if (!sub.isEmpty())
		text->addWidget(innerLabel(r, sub, 11, 400, color::textTertiary));
	h->addLayout(text, 1);
	control->setParent(r);
	h->addWidget(control, 0, Qt::AlignVCenter);
	return r;
}

Switch *makeSwitch(QWidget *parent, const QString &accessible, bool on, std::function<void(bool)> changed)
{
	auto *sw = new Switch(parent);
	sw->setAccessibleName(accessible);
	sw->setOn(on);
	QObject::connect(sw, &QAbstractButton::toggled, sw, [changed](bool checked) { changed(checked); });
	return sw;
}

} // namespace

SettingsPage::SettingsPage(App &app, QWidget *parent)
	: PageFrame(lt("Laycue.Back"), lt("Laycue.Settings.Title"), parent),
	  app_(app)
{
	build();
}

void SettingsPage::say(const QString &text)
{
	message_->setText(text);
	message_->show();
}

void SettingsPage::build()
{
	const Config &config = app_.config();
	QVBoxLayout *page = body();

	message_ = label(this, QString(), 12, 500, color::overlay);
	message_->hide();
	page->addWidget(message_);

	// --- How Laycue works with OBS ---
	{
		QVBoxLayout *v = group(page, this, lt("Laycue.Settings.WithObs"));
		auto *mode = new Segmented();
		mode->setItems({lt("Laycue.Mode.Normal"), lt("Laycue.Mode.Studio")});
		mode->setCurrent(studioModeActive() ? 1 : 0);
		mode->setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Fixed);
		mode->setFixedWidth(160);
		mode->setAccessibleName(lt("Laycue.Mode.Accessible"));
		connect(mode, &Segmented::changed, this, [this](int index) { emit studioRequested(index == 1); });
		v->addWidget(row(this, lt("Laycue.Settings.Mode"), lt("Laycue.Mode.Tip"), mode));
		v->addWidget(divider(this));
		v->addWidget(row(
			this, lt("Laycue.Settings.View"), lt("Laycue.Settings.ViewSub"),
			makeSwitch(nullptr, lt("Laycue.Settings.View"), config.settings.laycueView,
				   [this](bool on) { app_.modify([&](Config &c) { c.settings.laycueView = on; }); })));
		v->addWidget(divider(this));
		v->addWidget(row(
			this, lt("Laycue.Settings.Requeue"), lt("Laycue.Settings.RequeueSub"),
			makeSwitch(nullptr, lt("Laycue.Settings.Requeue"), config.settings.requeueLive,
				   [this](bool on) { app_.modify([&](Config &c) { c.settings.requeueLive = on; }); })));
	}

	// --- Pushbacks and overlays (setup: hidden while locked) ---
	editHost_ = new QWidget(this);
	auto *editLayout = new QVBoxLayout(editHost_);
	editLayout->setContentsMargins(0, 0, 0, 0);
	editLayout->setSpacing(size::gap);

	{
		QVBoxLayout *v = group(editLayout, editHost_, lt("Laycue.Settings.Pushbacks"));
		auto *seg = new Segmented();
		seg->setItems({lt("Laycue.Settings.Cut"), lt("Laycue.Settings.Fade")});
		seg->setCurrent(config.settings.insideFade ? 1 : 0);
		seg->setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Fixed);
		seg->setFixedWidth(140);
		seg->setAccessibleName(lt("Laycue.Settings.InsideWith"));
		connect(seg, &Segmented::changed, this,
			[this](int index) { app_.modify([&](Config &c) { c.settings.insideFade = index == 1; }); });
		v->addWidget(
			row(editHost_, lt("Laycue.Settings.InsideWith"), lt("Laycue.Settings.InsideWithSub"), seg));
		v->addWidget(divider(editHost_));
		auto *refit = button(nullptr, lt("Laycue.Settings.Refit"), "laycueSecondary");
		connect(refit, &QAbstractButton::clicked, this, [this]() {
			emit refitAllRequested();
			say(lt("Laycue.Settings.Refitted"));
		});
		v->addWidget(row(editHost_, lt("Laycue.Settings.RefitTitle"), lt("Laycue.Settings.RefitSub"), refit));
	}
	{
		QVBoxLayout *v = group(editLayout, editHost_, lt("Laycue.Settings.Overlays"));
		auto *labels = button(nullptr, lt("Laycue.Settings.LabelsAndRules"), "laycueSecondary");
		connect(labels, &QAbstractButton::clicked, this, &SettingsPage::overlaySetupRequested);
		v->addWidget(
			row(editHost_, lt("Laycue.Settings.LabelsTitle"), lt("Laycue.Settings.LabelsSub"), labels));
	}
	{
		// --- Failover: the stream is never empty because a source died (setup: hidden while locked) ---
		QVBoxLayout *v = group(editLayout, editHost_, lt("Laycue.Settings.Failover"));
		v->addWidget(
			innerLabel(editHost_, lt("Laycue.Settings.FailoverIntro"), 11, 400, color::textTertiary));
		v->addWidget(row(
			editHost_, lt("Laycue.Settings.FailoverOn"), QString(),
			makeSwitch(nullptr, lt("Laycue.Settings.FailoverOn"), config.settings.failoverOn,
				   [this](bool on) {
					   app_.modify([&](Config &c) { c.settings.failoverOn = on; });
				   })));
		v->addWidget(divider(editHost_));

		// The feed to watch, with the shots that exist named for the shot to go live. One walk over
		// OBS's scenes answers both: every feed placed in a scene, and every scene that isn't Laycue's.
		std::vector<std::pair<std::string, std::string>> feeds, shotsList;
		std::set<std::string> laycueScenes;
		for (const Pushback &pushback : config.pushbacks)
			laycueScenes.insert(pushback.sceneUuid);
		for (const Layout &layout : config.layouts)
			laycueScenes.insert(layout.sceneUuid);
		{
			std::set<std::string> seenFeeds, seenScenes;
			for (const OverlayItem &item : readAllItems()) {
				if (!item.sceneUuid.empty() && seenScenes.insert(item.sceneUuid).second &&
				    !laycueScenes.count(item.sceneUuid))
					shotsList.push_back({item.sceneUuid, item.sceneName});
				if (item.sourceUuid.empty() || !seenFeeds.insert(item.sourceUuid).second)
					continue;
				switch (item.kind) { // the kinds that carry a picture
				case SourceKind::Scene:
				case SourceKind::Group:
				case SourceKind::Text:
				case SourceKind::Audio:
					continue;
				default:
					feeds.push_back({item.sourceUuid, item.sourceName});
				}
			}
		}

		const auto fill = [](QComboBox *box, const std::vector<std::pair<std::string, std::string>> &items,
				     const std::string &chosen, const QString &none) {
			box->addItem(none, QString());
			for (const auto &[uuid, name] : items)
				box->addItem(QString::fromStdString(name), QString::fromStdString(uuid));
			const int at = box->findData(QString::fromStdString(chosen));
			box->setCurrentIndex(at < 0 ? 0 : at);
		};
		auto *watchBox = new QComboBox(editHost_);
		fill(watchBox, feeds, config.settings.failoverSourceUuid, lt("Laycue.Settings.FailoverNothing"));
		connect(watchBox, &QComboBox::activated, this, [watchBox, this](int index) {
			const std::string uuid = watchBox->itemData(index).toString().toStdString();
			const std::string name = uuid.empty() ? std::string() : watchBox->itemText(index).toStdString();
			app_.modify([&](Config &c) {
				c.settings.failoverSourceUuid = uuid;
				c.settings.failoverSourceName = name;
			});
		});
		v->addWidget(row(editHost_, lt("Laycue.Settings.FailoverWatch"),
				 lt("Laycue.Settings.FailoverWatchSub"), watchBox));
		v->addWidget(divider(editHost_));

		auto *shotBox = new QComboBox(editHost_);
		fill(shotBox, shotsList, config.settings.failoverShotUuid, lt("Laycue.Settings.FailoverNothing"));
		connect(shotBox, &QComboBox::activated, this, [shotBox, this](int index) {
			const std::string uuid = shotBox->itemData(index).toString().toStdString();
			const std::string name = uuid.empty() ? std::string() : shotBox->itemText(index).toStdString();
			app_.modify([&](Config &c) {
				c.settings.failoverShotUuid = uuid;
				c.settings.failoverShotName = name;
			});
		});
		v->addWidget(row(editHost_, lt("Laycue.Settings.FailoverShot"), QString(), shotBox));
	}
	page->addWidget(editHost_);

	// --- Panel ---
	{
		QVBoxLayout *v = group(page, this, lt("Laycue.Settings.Panel"));
		v->addWidget(row(this, lt("Laycue.Settings.Large"), lt("Laycue.Settings.LargeSub"),
				 makeSwitch(nullptr, lt("Laycue.Settings.Large"), config.settings.largeButtons,
					    [this](bool on) {
						    app_.modify([&](Config &c) { c.settings.largeButtons = on; });
					    })));
		v->addWidget(divider(this));
		v->addWidget(
			row(this, lt("Laycue.Settings.Tips"), lt("Laycue.Settings.TipsSub"),
			    makeSwitch(nullptr, lt("Laycue.Settings.Tips"), config.settings.showTips, [this](bool on) {
				    app_.modify([&](Config &c) { c.settings.showTips = on; });
			    })));
		v->addWidget(divider(this));
		auto *again = button(nullptr, lt("Laycue.Settings.TipsAgain"), "laycueSecondary");
		connect(again, &QAbstractButton::clicked, this, [this]() {
			app_.modify([](Config &c) {
				c.dismissedTips.clear();
				c.settings.showTips = true;
			});
			say(lt("Laycue.Settings.TipsBack"));
		});
		v->addWidget(row(this, lt("Laycue.Settings.TipsAgainTitle"), QString(), again));
	}

	// --- Profiles (setup: hidden while locked) ---
	auto *profilesOuter = new QWidget(this);
	auto *profilesLayout = new QVBoxLayout(profilesOuter);
	profilesLayout->setContentsMargins(0, 0, 0, 0);
	profilesLayout->setSpacing(size::gap);
	{
		QVBoxLayout *v = group(profilesLayout, profilesOuter, lt("Laycue.Settings.Profiles"));
		v->addWidget(
			innerLabel(profilesOuter, lt("Laycue.Settings.ProfilesIntro"), 11, 400, color::textTertiary));

		auto *saveRow = new QWidget(profilesOuter);
		auto *h = new QHBoxLayout(saveRow);
		h->setContentsMargins(0, 8, 0, 8);
		h->setSpacing(size::gapSmall);
		profileName_ = new QLineEdit(saveRow);
		profileName_->setPlaceholderText(lt("Laycue.Settings.ProfileName"));
		profileName_->setAccessibleName(lt("Laycue.Settings.ProfileName"));
		h->addWidget(profileName_, 1);
		auto *save = button(saveRow, lt("Laycue.Settings.SaveProfile"), "laycuePrimary");
		connect(save, &QAbstractButton::clicked, this, [this]() {
			const std::string name = profileName_->text().trimmed().toStdString();
			if (name.empty()) {
				say(lt("Laycue.Settings.NeedName"));
				return;
			}
			const auto existing = listProfiles();
			if (std::find(existing.begin(), existing.end(), name) != existing.end() &&
			    QMessageBox::question(this, lt("Laycue.Settings.SaveProfile"),
						  lt("Laycue.Settings.Replace").arg(QString::fromStdString(name))) !=
				    QMessageBox::Yes)
				return;
			if (saveProfile(app_, name)) {
				say(lt("Laycue.Settings.Saved").arg(QString::fromStdString(name)));
				profileName_->clear();
			} else {
				say(lt("Laycue.Settings.SaveFailed"));
			}
			rebuildProfiles();
		});
		h->addWidget(save);
		v->addWidget(saveRow);

		profilesHost_ = new QWidget(profilesOuter);
		auto *list = new QVBoxLayout(profilesHost_);
		list->setContentsMargins(0, 0, 0, 0);
		list->setSpacing(0);
		v->addWidget(profilesHost_);

		v->addWidget(divider(profilesOuter));
		auto *files = new QWidget(profilesOuter);
		auto *fh = new QHBoxLayout(files);
		fh->setContentsMargins(0, 8, 0, 8);
		fh->setSpacing(size::gapSmall);
		auto *exportButton = button(files, lt("Laycue.Settings.Export"), "laycueSecondary");
		connect(exportButton, &QAbstractButton::clicked, this, [this]() {
			const QString path = QFileDialog::getSaveFileName(this, lt("Laycue.Settings.Export"),
									  QStringLiteral("laycue-setup.json"),
									  QStringLiteral("Laycue setup (*.json)"));
			if (path.isEmpty())
				return;
			say(exportSetup(app_, path.toStdString()) ? lt("Laycue.Settings.Exported")
								  : lt("Laycue.Settings.SaveFailed"));
		});
		fh->addWidget(exportButton);
		auto *importButton = button(files, lt("Laycue.Settings.Import"), "laycueSecondary");
		connect(importButton, &QAbstractButton::clicked, this, [this]() {
			const QString path = QFileDialog::getOpenFileName(this, lt("Laycue.Settings.Import"), QString(),
									  QStringLiteral("Laycue setup (*.json)"));
			if (path.isEmpty())
				return;
			if (QMessageBox::question(this, lt("Laycue.Settings.Import"),
						  lt("Laycue.Settings.ImportAsk")) != QMessageBox::Yes)
				return;
			std::string summary;
			importSetup(app_, path.toStdString(), summary);
			say(QString::fromStdString(summary));
		});
		fh->addWidget(importButton);
		fh->addStretch(1);
		v->addWidget(files);
	}
	page->addWidget(profilesOuter);

	// --- Safety ---
	{
		QVBoxLayout *v = group(page, this, lt("Laycue.Settings.Safety"));
		v->addWidget(row(this, lt("Laycue.Settings.Lock"), lt("Laycue.Settings.LockSub"),
				 makeSwitch(nullptr, lt("Laycue.Settings.Lock"), config.settings.locked,
					    [this, profilesOuter](bool on) {
						    app_.modify([&](Config &c) { c.settings.locked = on; });
						    editHost_->setVisible(!on);
						    profilesOuter->setVisible(!on);
					    })));
	}
	editHost_->setVisible(!config.settings.locked);
	profilesOuter->setVisible(!config.settings.locked);

	// --- Updates ---
	page->addWidget(label(this, lt("Laycue.Update.Heading").toUpper(), 11, 700, color::textTertiary));
	page->addWidget(row(this, lt("Laycue.Settings.AutoUpdate"), lt("Laycue.Settings.AutoUpdateSub"),
			    makeSwitch(nullptr, lt("Laycue.Settings.AutoUpdate"), config.settings.autoUpdate,
				       [this](bool on) {
					       app_.modify([&](Config &c) { c.settings.autoUpdate = on; });
				       })));
	page->addWidget(new UpdateCard(updater_, this));
	if (config.settings.autoUpdate)
		updater_.check(); // the page opening is a good moment to look again

	page->addWidget(label(this, lt("Laycue.Settings.About").arg(QString::fromUtf8(PLUGIN_VERSION)), 11, 400,
			      color::textTertiary));
	rebuildProfiles();
}

void SettingsPage::rebuildProfiles()
{
	auto *layout = profilesHost_->layout();
	while (QLayoutItem *item = layout->takeAt(0)) {
		if (item->widget())
			item->widget()->deleteLater();
		delete item;
	}
	const std::vector<std::string> names = listProfiles();
	if (names.empty()) {
		layout->addWidget(
			innerLabel(profilesHost_, lt("Laycue.Settings.NoProfiles"), 12, 400, color::textSecondary));
		return;
	}
	for (const std::string &name : names) {
		layout->addWidget(divider(profilesHost_));
		auto *r = new QWidget(profilesHost_);
		auto *h = new QHBoxLayout(r);
		h->setContentsMargins(0, 6, 0, 6);
		h->setSpacing(size::gapSmall);
		h->addWidget(innerLabel(r, QString::fromStdString(name), 13, 600, color::cream), 1);
		auto *load = button(r, lt("Laycue.Settings.Load"), "laycueSecondary");
		connect(load, &QAbstractButton::clicked, this, [this, name]() {
			if (QMessageBox::question(this, lt("Laycue.Settings.Load"),
						  lt("Laycue.Settings.LoadAsk").arg(QString::fromStdString(name))) !=
			    QMessageBox::Yes)
				return;
			std::string summary;
			loadProfile(app_, name, summary);
			say(QString::fromStdString(summary));
		});
		h->addWidget(load);
		auto *remove = button(r, lt("Laycue.Rules.Delete"), "laycueDanger");
		connect(remove, &QAbstractButton::clicked, this, [this, name]() {
			deleteProfile(name);
			rebuildProfiles();
		});
		h->addWidget(remove);
		layout->addWidget(r);
	}
}

} // namespace laycue::ui
