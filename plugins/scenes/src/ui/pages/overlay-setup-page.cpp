#include "ui/pages/overlay-setup-page.hpp"

#include "core/autotag.hpp"
#include "obs/source-facts.hpp"
#include "core/config.hpp"
#include "features/app.hpp"
#include "obs/overlay-items.hpp"
#include "obs/scene-reader.hpp"
#include "ui/controls.hpp"
#include "ui/labels.hpp"
#include "ui/pages/helpers.hpp"
#include "ui/tokens.hpp"

#include <QAction>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QInputDialog>
#include <QLabel>
#include <QLineEdit>
#include <QMenu>
#include <QPushButton>
#include <QVBoxLayout>

#include <algorithm>
#include <functional>
#include <map>

namespace laycue::ui {

namespace {

struct SourceRow {
	std::string uuid;
	std::string name;
	SourceKind kind = SourceKind::Other;
	int sceneCount = 0;
	RectF box; // from the first scene it is in, for guessing what it is
	std::string typeId;
};

std::vector<SourceRow> readSourceRows()
{
	std::vector<SourceRow> rows;
	std::map<std::string, size_t> index;
	for (const OverlayItem &item : readAllItems()) {
		auto found = index.find(item.sourceUuid);
		if (found == index.end()) {
			index[item.sourceUuid] = rows.size();
			rows.push_back({item.sourceUuid, item.sourceName, item.kind, 1, item.box, item.typeId});
		} else {
			rows[found->second].sceneCount++;
		}
	}
	std::sort(rows.begin(), rows.end(), [](const SourceRow &a, const SourceRow &b) { return a.name < b.name; });
	return rows;
}

using page::label;
using page::button;

void clearLayout(QWidget *host)
{
	if (auto *layout = host->layout()) {
		while (QLayoutItem *item = layout->takeAt(0)) {
			if (item->widget())
				item->widget()->deleteLater();
			delete item;
		}
	}
}

// Tags a person can pick: the built-in ones plus any custom tag already in use.
std::vector<std::string> knownTags(const Config &config)
{
	std::vector<std::string> tags = builtInTags();
	for (const TagAssignment &t : config.tags) {
		if (std::find(tags.begin(), tags.end(), t.tag) == tags.end())
			tags.push_back(t.tag);
	}
	return tags;
}

} // namespace

OverlaySetupPage::OverlaySetupPage(App &app, QWidget *parent)
	: PageFrame(lt("Laycue.Back"), lt("Laycue.OverlaySetup.Title"), parent),
	  app_(app)
{
	body()->addWidget(label(this, lt("Laycue.OverlaySetup.Intro"), 12, 400, color::textSecondary));

	// Sources
	auto *sourcesHeader = new QHBoxLayout();
	sourcesHeader->setContentsMargins(0, 0, size::shadowMargin, 0);
	sourcesHeader->addWidget(label(this, lt("Laycue.OverlaySetup.Sources"), 15, 700, color::cream), 1);
	auto *autoButton = button(this, lt("Laycue.AutoTag"), "laycueSecondary");
	autoButton->setToolTip(lt("Laycue.AutoTagTip"));
	connect(autoButton, &QAbstractButton::clicked, this, &OverlaySetupPage::autoTag);
	sourcesHeader->addWidget(autoButton);
	body()->addLayout(sourcesHeader);

	message_ = label(this, QString(), 12, 500, color::overlay);
	message_->hide();
	body()->addWidget(message_);

	auto *sourcesSurface = new Surface(1, this);
	auto *surfaceLayout = new QVBoxLayout(sourcesSurface);
	sourcesHost_ = new QWidget(sourcesSurface);
	auto *rows = new QVBoxLayout(sourcesHost_);
	rows->setContentsMargins(0, 0, 0, 0);
	rows->setSpacing(0);
	surfaceLayout->addWidget(sourcesHost_);
	sourcesSurface->setPadding(4, 4, 4, 4);
	body()->addWidget(sourcesSurface);

	// Rules
	auto *rulesHeader = new QHBoxLayout();
	rulesHeader->setContentsMargins(0, 0, size::shadowMargin, 0);
	rulesHeader->addWidget(label(this, lt("Laycue.Rules.Title"), 15, 700, color::cream), 1);
	auto *add = button(this, lt("Laycue.Rules.New"), "laycueSecondary");
	connect(add, &QAbstractButton::clicked, this, [this]() {
		app_.modify([](Config &c) {
			Rule rule;
			rule.name = "Rule " + std::to_string(c.rules.size() + 1);
			c.rules.push_back(rule);
		});
		rebuildRules();
	});
	rulesHeader->addWidget(add);
	body()->addSpacing(size::gapSmall);
	body()->addLayout(rulesHeader);
	body()->addWidget(label(this, lt("Laycue.Rules.Intro"), 12, 400, color::textSecondary));

	rulesHost_ = new QWidget(this);
	auto *rulesLayout = new QVBoxLayout(rulesHost_);
	rulesLayout->setContentsMargins(0, 0, 0, 0);
	rulesLayout->setSpacing(size::gap);
	body()->addWidget(rulesHost_);

	rebuildSources();
	rebuildRules();
}

void OverlaySetupPage::rebuildSources()
{
	clearLayout(sourcesHost_);
	auto *layout = static_cast<QVBoxLayout *>(sourcesHost_->layout());
	const std::vector<SourceRow> rows = readSourceRows();

	if (rows.empty()) {
		auto *empty = label(sourcesHost_, lt("Laycue.OverlaySetup.NoSources"), 12, 400, color::textSecondary);
		empty->setContentsMargins(10, 8, 10, 8);
		layout->addWidget(empty);
		return;
	}

	for (const SourceRow &row : rows) {
		auto *widget = new QWidget(sourcesHost_);
		widget->setContextMenuPolicy(Qt::CustomContextMenu);
		auto *h = new QHBoxLayout(widget);
		h->setContentsMargins(10, 6, 6, 6);
		h->setSpacing(8);

		auto *text = new QVBoxLayout();
		text->setSpacing(1);
		text->addWidget(label(widget, QString::fromStdString(row.name), 13, 600, color::cream));
		QString subtitle =
			kindDisplayName(row.kind) + QStringLiteral(" · ") +
			lt(row.sceneCount == 1 ? "Laycue.OverlaySetup.InScene" : "Laycue.OverlaySetup.InScenes")
				.arg(row.sceneCount);
		const auto why = reasons_.find(row.uuid);
		if (why != reasons_.end())
			subtitle += QStringLiteral(" · ") + why->second;
		text->addWidget(label(widget, subtitle, 11, 400, color::textTertiary));
		h->addLayout(text, 1);

		const std::string current = tagOf(app_.config(), row.uuid);
		auto *tagButton =
			button(widget, current.empty() ? lt("Laycue.OverlaySetup.NotOverlay") : tagDisplayName(current),
			       current.empty() ? "laycueSecondary" : "laycuePrimary");
		tagButton->setAccessibleName(QString::fromStdString(row.name) + QStringLiteral(": ") +
					     tagButton->text());
		h->addWidget(tagButton);

		const SourceRow captured = row;
		auto showMenu = [this, captured, current](const QPoint &globalPos) {
			QMenu menu(this);
			auto addChoice = [&](const QString &text, const std::string &tagId) {
				QAction *action = menu.addAction(text);
				action->setCheckable(true);
				action->setChecked(current == tagId);
				connect(action, &QAction::triggered, this, [this, captured, tagId]() {
					app_.modify([&](Config &c) { setTag(c, captured.uuid, captured.name, tagId); });
					rebuildSources();
					rebuildRules();
				});
			};
			addChoice(lt("Laycue.OverlaySetup.NotOverlay"), std::string());
			menu.addSeparator();
			for (const std::string &tagId : knownTags(app_.config()))
				addChoice(tagDisplayName(tagId), tagId);
			menu.addSeparator();
			QAction *custom = menu.addAction(lt("Laycue.OverlaySetup.CustomTag"));
			connect(custom, &QAction::triggered, this, [this, captured]() {
				bool ok = false;
				const QString name = QInputDialog::getText(this,
									   lt("Laycue.OverlaySetup.CustomTagTitle"),
									   lt("Laycue.OverlaySetup.CustomTagPrompt"),
									   QLineEdit::Normal, QString(), &ok)
							     .trimmed();
				if (!ok || name.isEmpty())
					return;
				app_.modify([&](Config &c) {
					setTag(c, captured.uuid, captured.name, name.toStdString());
				});
				rebuildSources();
				rebuildRules();
			});
			menu.exec(globalPos);
		};
		connect(tagButton, &QAbstractButton::clicked, this,
			[tagButton, showMenu]() { showMenu(tagButton->mapToGlobal(QPoint(0, tagButton->height()))); });
		connect(widget, &QWidget::customContextMenuRequested, this,
			[widget, showMenu](const QPoint &pos) { showMenu(widget->mapToGlobal(pos)); });

		layout->addWidget(widget);
	}
}

void OverlaySetupPage::rebuildRules()
{
	clearLayout(rulesHost_);
	auto *layout = static_cast<QVBoxLayout *>(rulesHost_->layout());
	const Config &config = app_.config();

	if (config.rules.empty()) {
		layout->addWidget(label(rulesHost_, lt("Laycue.Rules.None"), 12, 400, color::textTertiary));
		return;
	}

	// Scenes for the "Chosen scenes" picker.
	std::vector<std::pair<std::string, std::string>> scenes; // uuid, name
	for (const SceneInfo &scene : readSnapshot().scenes)
		scenes.emplace_back(scene.uuid, scene.name);

	for (size_t i = 0; i < config.rules.size(); ++i) {
		const Rule rule = config.rules[i];

		auto *card = new Surface(1, rulesHost_);
		auto *v = new QVBoxLayout(card);
		v->setSpacing(size::gapSmall + 2);
		card->setPadding(12, 12, 12, 12);

		// Name and delete
		auto *top = new QHBoxLayout();
		auto *name = new QLineEdit(QString::fromStdString(rule.name), card);
		name->setAccessibleName(lt("Laycue.Rules.Name"));
		connect(name, &QLineEdit::editingFinished, this, [this, i, name]() {
			app_.modify([&](Config &c) {
				if (i < c.rules.size())
					c.rules[i].name = name->text().toStdString();
			});
		});
		top->addWidget(name, 1);
		auto *remove = button(card, lt("Laycue.Rules.Delete"), "laycueDanger");
		connect(remove, &QAbstractButton::clicked, this, [this, i]() {
			app_.modify([&](Config &c) {
				if (i < c.rules.size())
					c.rules.erase(c.rules.begin() + i);
			});
			rebuildRules();
		});
		top->addWidget(remove);
		v->addLayout(top);

		// Which tags replace each other
		v->addWidget(label(card, lt("Laycue.Rules.ReplaceEachOther"), 12, 600, color::textSecondary));
		auto *chips = new QGridLayout();
		chips->setSpacing(6);
		const std::vector<std::string> tags = knownTags(config);
		for (size_t t = 0; t < tags.size(); ++t) {
			const std::string tagId = tags[t];
			auto *chip = new QPushButton(tagDisplayName(tagId), card);
			chip->setObjectName("laycueChip");
			chip->setCursor(Qt::PointingHandCursor);
			chip->setCheckable(true);
			chip->setChecked(std::find(rule.tags.begin(), rule.tags.end(), tagId) != rule.tags.end());
			connect(chip, &QPushButton::toggled, this, [this, i, tagId](bool on) {
				app_.modify([&](Config &c) {
					if (i >= c.rules.size())
						return;
					auto &list = c.rules[i].tags;
					list.erase(std::remove(list.begin(), list.end(), tagId), list.end());
					if (on)
						list.push_back(tagId);
				});
			});
			chips->addWidget(chip, int(t) / 3, int(t) % 3);
		}
		v->addLayout(chips);

		// Where the rule applies
		v->addWidget(label(card, lt("Laycue.Rules.Where"), 12, 600, color::textSecondary));
		auto *scope = new Segmented(card);
		scope->setItems(
			{lt("Laycue.Rules.ThisScene"), lt("Laycue.Rules.AllScenes"), lt("Laycue.Rules.ChosenScenes")});
		scope->setCurrent(rule.scope == Scope::SameScene ? 0 : rule.scope == Scope::AllScenes ? 1 : 2);
		connect(scope, &Segmented::changed, this, [this, i](int index) {
			app_.modify([&](Config &c) {
				if (i < c.rules.size())
					c.rules[i].scope = index == 0   ? Scope::SameScene
							   : index == 1 ? Scope::AllScenes
									: Scope::ChosenScenes;
			});
			rebuildRules();
		});
		v->addWidget(scope);

		if (rule.scope == Scope::ChosenScenes) {
			const QString text = rule.scenes.empty()
						     ? lt("Laycue.Rules.ChooseScenes")
						     : lt("Laycue.Rules.ChooseScenesN").arg(rule.scenes.size());
			auto *choose = button(card, text, "laycueSecondary");
			connect(choose, &QAbstractButton::clicked, this, [this, i, choose, scenes]() {
				QMenu menu(this);
				for (const auto &scene : scenes) {
					QAction *action = menu.addAction(QString::fromStdString(scene.second));
					action->setCheckable(true);
					const auto &chosen = app_.config().rules[i].scenes;
					action->setChecked(std::find(chosen.begin(), chosen.end(), scene.first) !=
							   chosen.end());
					const std::string uuid = scene.first;
					connect(action, &QAction::toggled, this, [this, i, uuid](bool on) {
						app_.modify([&](Config &c) {
							if (i >= c.rules.size())
								return;
							auto &list = c.rules[i].scenes;
							list.erase(std::remove(list.begin(), list.end(), uuid),
								   list.end());
							if (on)
								list.push_back(uuid);
						});
					});
				}
				menu.exec(choose->mapToGlobal(QPoint(0, choose->height())));
				rebuildRules();
			});
			v->addWidget(choose, 0, Qt::AlignLeft);
		}

		// Strict and pause
		auto addSwitchRow = [&](const QString &title, const QString &sub, bool on,
					std::function<void(Rule &, bool)> apply) {
			auto *row = new QHBoxLayout();
			auto *text = new QVBoxLayout();
			text->setSpacing(1);
			text->addWidget(label(card, title, 13, 500, color::cream));
			if (!sub.isEmpty())
				text->addWidget(label(card, sub, 11, 400, color::textTertiary));
			row->addLayout(text, 1);
			auto *sw = new Switch(card);
			sw->setAccessibleName(title);
			sw->setOn(on);
			connect(sw, &QAbstractButton::toggled, this, [this, i, apply](bool checked) {
				app_.modify([&](Config &c) {
					if (i < c.rules.size())
						apply(c.rules[i], checked);
				});
			});
			row->addWidget(sw, 0, Qt::AlignVCenter);
			v->addLayout(row);
		};
		addSwitchRow(lt("Laycue.Rules.Strict"), lt("Laycue.Rules.StrictSub"), rule.strict,
			     [](Rule &r, bool on) { r.strict = on; });
		addSwitchRow(lt("Laycue.Rules.Pause"), lt("Laycue.Rules.PauseSub"), rule.paused,
			     [](Rule &r, bool on) { r.paused = on; });
		layout->addWidget(card);
	}
}

void OverlaySetupPage::autoTag()
{
	double canvasW = 0, canvasH = 0;
	canvasSize(canvasW, canvasH);
	int tagged = 0;
	const std::vector<SourceRow> rows = readSourceRows();
	// Look at each unlabelled source: its type, its filters and (for pictures) what the picture is made of.
	std::vector<std::pair<SourceRow, TagSuggestion>> found;
	for (const SourceRow &row : rows) {
		if (!tagOf(app_.config(), row.uuid).empty())
			continue;
		SourceFacts facts;
		facts.name = row.name;
		facts.kind = row.kind;
		facts.spot = row.box;
		facts.canvasWidth = canvasW;
		facts.canvasHeight = canvasH;
		const bool picture = row.kind == SourceKind::Image || row.kind == SourceKind::Slideshow ||
				     row.kind == SourceKind::Media || row.kind == SourceKind::Browser ||
				     row.kind == SourceKind::Other || row.kind == SourceKind::Color;
		const PictureFacts seen = readPictureFacts(row.uuid, picture);
		facts.typeId = seen.typeId.empty() ? row.typeId : seen.typeId;
		facts.keyFilter = seen.keyFilter;
		facts.seeThrough = seen.seeThrough;
		facts.green = seen.green;
		if (const auto suggestion = classifySource(facts))
			found.push_back({row, *suggestion});
	}
	app_.modify([&](Config &c) {
		for (const auto &entry : found) {
			setTag(c, entry.first.uuid, entry.first.name, entry.second.tag);
			++tagged;
		}
	});
	for (const auto &entry : found) {
		const std::string key = "Laycue.AutoWhy." + entry.second.why;
		reasons_[entry.first.uuid] = lt(key.c_str());
	}
	message_->setText(tagged > 0 ? lt("Laycue.AutoTagDone").arg(tagged) : lt("Laycue.AutoTagNone"));
	message_->show();
	rebuildSources();
	rebuildRules();
}

} // namespace laycue::ui
