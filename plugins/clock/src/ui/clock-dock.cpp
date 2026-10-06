#include "ui/clock-dock.hpp"

#include "core/clock-core.hpp"
#include "render/clock-renderer.hpp"
#include "store/clock-io.hpp"
#include "ui/controls.hpp"
#include "ui/pages/helpers.hpp"
#include "ui/style.hpp"
#include "ui/text.hpp"
#include "ui/tokens.hpp"
#include "ui/update-card.hpp"

#include <obs-module.h>
#include <plugin-support.h>

#include <QColorDialog>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QMenu>
#include <QPainter>
#include <QScrollArea>
#include <QVBoxLayout>

#include <algorithm>
#include <cstdio>
#include <vector>

namespace laycue::clock::ui {

using namespace laycue::ui;
using laycue::ui::page::innerLabel;

namespace {

constexpr int kPollMs = 200;
constexpr int kSaveMs = 700;
constexpr int kToastMs = 3000;

QString qs(const std::string &s)
{
	return QString::fromStdString(s);
}

std::string stdstr(const QString &s)
{
	return s.toStdString();
}

QColor qcolor(uint32_t rgb)
{
	return QColor(int((rgb >> 16) & 0xFF), int((rgb >> 8) & 0xFF), int(rgb & 0xFF));
}

} // namespace

// The big number: what the clock shows right now, in the colour of its level.
class Readout : public QWidget {
public:
	explicit Readout(QWidget *parent) : QWidget(parent)
	{
		setFixedHeight(92);
		setAccessibleName(lt("Laycue.Clock.Readout"));
	}

	void set(const QString &time, const QString &caption, int level)
	{
		time_ = time;
		caption_ = caption;
		level_ = level;
		update();
	}

protected:
	void paintEvent(QPaintEvent *) override
	{
		QPainter p(this);
		p.setRenderHint(QPainter::Antialiasing, true);
		p.setPen(Qt::NoPen);
		p.setBrush(color::recess);
		p.drawRoundedRect(QRectF(rect()).adjusted(0.5, 0.5, -0.5, -0.5), size::radius + 2, size::radius + 2);
		QColor c = color::cream;
		if (level_ == 1)
			c = color::overlay;
		else if (level_ == 2)
			c = color::live;
		p.setPen(c);
		p.setFont(monoFont(44));
		p.drawText(QRectF(0, 6, width(), 56), Qt::AlignCenter, time_);
		p.setPen(color::textTertiary);
		p.setFont(uiFont(11, 600));
		p.drawText(QRectF(0, 62, width(), 24), Qt::AlignCenter, caption_);
	}

private:
	QString time_ = QStringLiteral("00:00");
	QString caption_;
	int level_ = 0;
};

ClockDock::ClockDock(QWidget *parent) : QWidget(parent)
{
	setObjectName("LaycueClockDock");
	setAttribute(Qt::WA_StyledBackground, true);
	setMinimumWidth(280);
	loadBrandFonts();

	const PanelState saved = loadPanelState();
	settings_ = saved.settings;
	target_ = saved.targetSource;
	build();

	pollTimer_.setInterval(kPollMs);
	connect(&pollTimer_, &QTimer::timeout, this, &ClockDock::poll);
	saveTimer_.setSingleShot(true);
	saveTimer_.setInterval(kSaveMs);
	connect(&saveTimer_, &QTimer::timeout, this, &ClockDock::saveNow);
	toastTimer_.setSingleShot(true);
	toastTimer_.setInterval(kToastMs);
	connect(&toastTimer_, &QTimer::timeout, this, [this]() {
		if (toastLabel_)
			toastLabel_->hide();
	});
	poll();
	pollTimer_.start();
}

ClockDock::~ClockDock()
{
	pollTimer_.stop();
	saveNow();
}

void ClockDock::scheduleSave()
{
	saveTimer_.start();
}

void ClockDock::saveNow()
{
	PanelState state;
	state.settings = settings_;
	state.targetSource = target_;
	savePanelState(state);
}

void ClockDock::toast(const QString &text)
{
	if (!toastLabel_)
		return;
	toastLabel_->setText(text);
	toastLabel_->show();
	toastTimer_.start();
}

void ClockDock::runHotkey(int index)
{
	try {
		if (index == 0)
			startPause();
		else if (index == 1)
			reset();
	} catch (...) {
		obs_log(LOG_ERROR, "Laycue Clock: error running a hotkey");
	}
}

// ---- Building the panel ------------------------------------------------------------------------------------

void ClockDock::build()
{
	auto *outer = new QVBoxLayout(this);
	outer->setContentsMargins(0, 0, 0, 0);
	outer->setSpacing(0);
	auto *content = new QWidget(this);
	content->setObjectName("LaycueContent");
	content->setAttribute(Qt::WA_StyledBackground, true);
	content->setStyleSheet(panelStyleSheet());
	outer->addWidget(content);
	auto *contentLayout = new QVBoxLayout(content);
	contentLayout->setContentsMargins(0, 0, 0, 0);
	contentLayout->setSpacing(0);

	// If the panel is shorter than everything needs, it scrolls.
	scroll_ = new QScrollArea(content);
	scroll_->setWidgetResizable(true);
	scroll_->setFrameShape(QFrame::NoFrame);
	scroll_->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
	contentLayout->addWidget(scroll_);

	toastLabel_ = new QLabel(content);
	toastLabel_->setStyleSheet(
		QStringLiteral("background: %1; color: %2; border-radius: 16px; padding: 8px 14px; font-size: 12px;")
			.arg(hex(color::control), hex(color::cream)));
	toastLabel_->hide();
	contentLayout->addWidget(toastLabel_, 0, Qt::AlignHCenter);

	rebuild();
}

void ClockDock::rebuild()
{
	building_ = true;
	page_ = new QWidget();
	pageLayout_ = new QVBoxLayout(page_);
	pageLayout_->setContentsMargins(size::margin - size::shadowMargin, 6, size::margin - size::shadowMargin, 8);
	pageLayout_->setSpacing(6);

	// Header.
	auto *header = new QHBoxLayout();
	header->setContentsMargins(size::shadowMargin, 2, size::shadowMargin, 2);
	header->addWidget(innerLabel(page_, lt("Laycue.Clock.Heading"), 17, 700, color::cream));
	header->addStretch(1);
	statusLabel_ = innerLabel(page_, QString(), 12, 600, color::textSecondary);
	statusLabel_->setWordWrap(false);
	header->addWidget(statusLabel_);
	pageLayout_->addLayout(header);

	const bool haveLayer = status_.found;
	auto *stage = new Surface(2, page_);
	auto *v = new QVBoxLayout(stage);
	v->setSpacing(size::gapSmall);
	stage->setPadding(12, 8, 12, 10);

	readout_ = new Readout(stage);
	v->addWidget(readout_);

	if (!haveLayer) {
		v->addWidget(innerLabel(stage, lt("Laycue.Clock.NoLayer"), 12, 400, color::textSecondary));
		auto *add = new ActionButton(lt("Laycue.Clock.AddToScene"), ActionButton::Kind::Primary, stage);
		add->setMinimumHeight(size::controlHeight + 6);
		connect(add, &QAbstractButton::clicked, this, [this]() { addLayer(false); });
		v->addWidget(add);
		startButton_ = nullptr;
	} else {
		auto *row = new QHBoxLayout();
		row->setSpacing(size::gapSmall);
		startButton_ = new ActionButton(lt("Laycue.Clock.Start"), ActionButton::Kind::Primary, stage);
		startButton_->setMinimumHeight(size::controlHeight + 6);
		connect(startButton_, &QAbstractButton::clicked, this, &ClockDock::startPause);
		row->addWidget(startButton_, 3);
		auto *resetButton = new ActionButton(lt("Laycue.Clock.Reset"), ActionButton::Kind::Secondary, stage);
		resetButton->setMinimumHeight(size::controlHeight + 6);
		connect(resetButton, &QAbstractButton::clicked, this, &ClockDock::reset);
		row->addWidget(resetButton, 2);
		v->addLayout(row);

		auto *nudge = new QHBoxLayout();
		nudge->setSpacing(size::gapSmall);
		const auto addNudge = [&](const QString &text, int seconds) {
			auto *b = new ActionButton(text, ActionButton::Kind::Secondary, stage);
			connect(b, &QAbstractButton::clicked, this,
				[this, seconds]() { adjustClock(target_, seconds); });
			nudge->addWidget(b, 1);
		};
		addNudge(lt("Laycue.Clock.Minus1"), -60);
		addNudge(lt("Laycue.Clock.Plus1"), 60);
		v->addLayout(nudge);
	}

	// What it counts.
	auto *mode = new Segmented(stage);
	mode->setItems({lt("Laycue.Clock.Mode.0"), lt("Laycue.Clock.Mode.1"), lt("Laycue.Clock.Mode.2")});
	mode->setCurrent(settings_.mode);
	mode->setAccessibleName(lt("Laycue.Clock.Mode"));
	connect(mode, &Segmented::changed, this, [this](int i) {
		settings_.mode = i;
		rebuild();
		changed();
	});
	v->addWidget(mode);

	entryHint_ = innerLabel(stage, QString(), 11, 500, color::overlay);
	entryHint_->hide();
	if (settings_.mode != int(Mode::CountUp)) {
		const bool until = settings_.mode == int(Mode::Until);
		auto *entry = new QWidget(stage);
		auto *h = new QHBoxLayout(entry);
		h->setContentsMargins(0, 0, 0, 0);
		h->setSpacing(size::gapSmall);
		auto *name = innerLabel(entry, lt(until ? "Laycue.Clock.Target" : "Laycue.Clock.Duration"), 11, 600,
					color::textTertiary);
		name->setFixedWidth(96);
		name->setWordWrap(true);
		h->addWidget(name, 0, Qt::AlignTop);
		auto *edit = new QLineEdit(entry);
		edit->setText(qs(until ? settings_.target : settings_.duration));
		edit->setPlaceholderText(until ? "10:00" : "05:00");
		connect(edit, &QLineEdit::textEdited, this, [this, until](const QString &text) {
			double seconds = 0.0;
			int secondOfDay = 0;
			const bool ok = until ? parseTimeOfDay(stdstr(text), secondOfDay)
					      : parseDuration(stdstr(text), seconds);
			entryHint_->setVisible(!ok && !text.trimmed().isEmpty());
			entryHint_->setText(lt(until ? "Laycue.Clock.TargetHint" : "Laycue.Clock.DurationHint"));
			if (!ok)
				return; // wait until it reads as a time
			(until ? settings_.target : settings_.duration) = stdstr(text.trimmed());
			changed();
		});
		h->addWidget(edit, 1);
		v->addWidget(entry);
		v->addWidget(entryHint_);
		if (!until) {
			auto *presets = new QHBoxLayout();
			presets->setSpacing(size::gapSmall);
			for (int minutes : {1, 5, 10, 15, 30}) {
				auto *b = new ActionButton(lt("Laycue.Clock.PresetMinutes").arg(minutes),
							   ActionButton::Kind::Secondary, stage);
				connect(b, &QAbstractButton::clicked, this, [this, minutes]() {
					char text[16];
					snprintf(text, sizeof(text), "%02d:00", minutes);
					settings_.duration = text;
					rebuild();
					changed();
				});
				presets->addWidget(b, 1);
			}
			v->addLayout(presets);
		}
	}

	auto *labelRow = new QWidget(stage);
	auto *lh = new QHBoxLayout(labelRow);
	lh->setContentsMargins(0, 0, 0, 0);
	lh->setSpacing(size::gapSmall);
	auto *lname = innerLabel(labelRow, lt("Laycue.Clock.Label"), 11, 600, color::textTertiary);
	lname->setFixedWidth(96);
	lname->setWordWrap(true);
	lh->addWidget(lname, 0, Qt::AlignTop);
	auto *labelEdit = new QLineEdit(labelRow);
	labelEdit->setText(qs(settings_.label));
	labelEdit->setPlaceholderText(lt("Laycue.Clock.LabelPlaceholder"));
	connect(labelEdit, &QLineEdit::textEdited, this, [this](const QString &text) {
		settings_.label = stdstr(text);
		changed();
	});
	lh->addWidget(labelEdit, 1);
	v->addWidget(labelRow);

	// Options fold away into one row.
	optionsRow_ = new RowButton(stage);
	optionsRow_->setTitle(lt("Laycue.Clock.Options"));
	connect(optionsRow_, &RowButton::clicked, this, [this]() {
		optionsOpen_ = !optionsOpen_;
		refreshOptions();
	});
	v->addWidget(optionsRow_);

	optionsBox_ = new QWidget(stage);
	auto *ov = new QVBoxLayout(optionsBox_);
	ov->setContentsMargins(0, 0, 0, 0);
	ov->setSpacing(size::gapSmall);

	auto *design = new RowButton(optionsBox_);
	design->setTitle(lt("Laycue.Clock.Design"));
	design->setValue(lt(designKey(Design(settings_.design))));
	design->setOpen(true);
	connect(design, &RowButton::clicked, this, &ClockDock::pickDesign);
	ov->addWidget(design);

	ov->addWidget(innerLabel(optionsBox_, lt("Laycue.Clock.Background"), 11, 600, color::textTertiary));
	auto *bg = new Segmented(optionsBox_);
	bg->setItems(
		{lt("Laycue.Clock.Background.0"), lt("Laycue.Clock.Background.1"), lt("Laycue.Clock.Background.2")});
	bg->setCurrent(settings_.background);
	bg->setAccessibleName(lt("Laycue.Clock.Background"));
	connect(bg, &Segmented::changed, this, [this](int i) {
		settings_.background = i;
		changed();
	});
	ov->addWidget(bg);

	ov->addWidget(innerLabel(optionsBox_, lt("Laycue.Clock.Position"), 11, 600, color::textTertiary));
	auto *align = new Segmented(optionsBox_);
	align->setItems({lt("Laycue.Clock.Left"), lt("Laycue.Clock.Centre"), lt("Laycue.Clock.Right")});
	align->setCurrent(settings_.align);
	connect(align, &Segmented::changed, this, [this](int i) {
		settings_.align = i;
		changed();
	});
	ov->addWidget(align);
	auto *vertical = new Segmented(optionsBox_);
	vertical->setItems({lt("Laycue.Clock.Top"), lt("Laycue.Clock.Middle"), lt("Laycue.Clock.Bottom")});
	vertical->setCurrent(settings_.vertical);
	connect(vertical, &Segmented::changed, this, [this](int i) {
		settings_.vertical = i;
		changed();
	});
	ov->addWidget(vertical);

	const auto stepper = [&](const QString &title, const QString &value, auto minus, auto plus) {
		auto *r = new QHBoxLayout();
		r->setSpacing(size::gapSmall);
		r->addWidget(innerLabel(optionsBox_, title, 13, 600, color::cream), 1);
		auto *m = new ActionButton(QStringLiteral("−"), ActionButton::Kind::Secondary, optionsBox_);
		connect(m, &QAbstractButton::clicked, this, minus);
		r->addWidget(m);
		auto *val = innerLabel(optionsBox_, value, 13, 600, color::cream);
		val->setAlignment(Qt::AlignCenter);
		val->setWordWrap(false);
		val->setMinimumWidth(60);
		r->addWidget(val);
		auto *pl = new ActionButton(QStringLiteral("+"), ActionButton::Kind::Secondary, optionsBox_);
		connect(pl, &QAbstractButton::clicked, this, plus);
		r->addWidget(pl);
		ov->addLayout(r);
	};
	stepper(
		lt("Laycue.Clock.Size"), QStringLiteral("%1%").arg(settings_.sizePercent),
		[this]() {
			settings_.sizePercent -= 10;
			rebuild();
			changed();
		},
		[this]() {
			settings_.sizePercent += 10;
			rebuild();
			changed();
		});
	stepper(
		lt("Laycue.Clock.WarnSeconds"), lt("Laycue.Clock.Seconds").arg(settings_.warnSeconds),
		[this]() {
			settings_.warnSeconds -= 15;
			rebuild();
			changed();
		},
		[this]() {
			settings_.warnSeconds += 15;
			rebuild();
			changed();
		});

	auto *overtimeRow = new QHBoxLayout();
	overtimeRow->setSpacing(size::gapSmall);
	overtimeRow->addWidget(innerLabel(optionsBox_, lt("Laycue.Clock.Overtime"), 13, 600, color::cream), 1);
	auto *overtime = new Switch(optionsBox_);
	overtime->setChecked(settings_.overtime);
	overtime->setAccessibleName(lt("Laycue.Clock.Overtime"));
	connect(overtime, &QAbstractButton::toggled, this, [this](bool on) {
		settings_.overtime = on;
		changed();
	});
	overtimeRow->addWidget(overtime);
	ov->addLayout(overtimeRow);

	ov->addWidget(innerLabel(optionsBox_, lt("Laycue.Clock.Colours"), 11, 600, color::textTertiary));
	auto *colours = new QHBoxLayout();
	colours->setSpacing(size::gapSmall);
	const auto addColour = [&](const QString &title, uint32_t ClockSettings::*which) {
		auto *b = new ActionButton(title, ActionButton::Kind::Secondary, optionsBox_);
		connect(b, &QAbstractButton::clicked, this, [this, which, title]() { pickColour(which, title); });
		colours->addWidget(b, 1);
	};
	addColour(lt("Laycue.Clock.TextColour"), &ClockSettings::text);
	addColour(lt("Laycue.Clock.AccentColour"), &ClockSettings::accent);
	addColour(lt("Laycue.Clock.PanelColour"), &ClockSettings::panel);
	ov->addLayout(colours);
	stepper(
		lt("Laycue.Clock.PanelOpacity"), QStringLiteral("%1%").arg(settings_.panelOpacity),
		[this]() {
			settings_.panelOpacity -= 10;
			rebuild();
			changed();
		},
		[this]() {
			settings_.panelOpacity += 10;
			rebuild();
			changed();
		});
	v->addWidget(optionsBox_);
	pageLayout_->addWidget(stage);

	// The layer: which clock source this panel controls.
	pageLayout_->addWidget(innerLabel(page_, lt("Laycue.Clock.Layer").toUpper(), 11, 700, color::textTertiary));
	auto *layer = new Surface(1, page_);
	auto *lv = new QVBoxLayout(layer);
	lv->setSpacing(size::gapSmall);
	layer->setPadding(14, 10, 14, 10);
	lv->addWidget(
		innerLabel(layer, haveLayer ? qs(target_) : lt("Laycue.Clock.NoLayerShort"), 13, 600, color::cream));
	lv->addWidget(innerLabel(layer, lt("Laycue.Clock.LayerHelp"), 11, 400, color::textTertiary));
	auto *lrow = new QHBoxLayout();
	lrow->setSpacing(size::gapSmall);
	auto *here = new ActionButton(lt("Laycue.Clock.AddToScene"), ActionButton::Kind::Secondary, layer);
	connect(here, &QAbstractButton::clicked, this, [this]() { addLayer(false); });
	lrow->addWidget(here, 1);
	auto *all = new ActionButton(lt("Laycue.Clock.AddToAll"), ActionButton::Kind::Secondary, layer);
	connect(all, &QAbstractButton::clicked, this, [this]() { addLayer(true); });
	lrow->addWidget(all, 1);
	lv->addLayout(lrow);
	pageLayout_->addWidget(layer);

	pageLayout_->addWidget(innerLabel(page_, lt("Laycue.Update.Heading").toUpper(), 11, 700, color::textTertiary));
	pageLayout_->addWidget(new UpdateCard(updater_, page_));
	pageLayout_->addStretch(1);

	QWidget *old = scroll_->takeWidget();
	scroll_->setWidget(page_);
	if (old)
		old->deleteLater();
	refreshOptions();
	building_ = false;
	poll();
}

void ClockDock::refreshOptions()
{
	if (!optionsRow_)
		return;
	static const char *const modes[] = {"Laycue.Clock.Mode.0", "Laycue.Clock.Mode.1", "Laycue.Clock.Mode.2"};
	optionsRow_->setValue(lt(designKey(Design(settings_.design))) + QStringLiteral(" · ") +
			      lt(modes[std::clamp(settings_.mode, 0, 2)]));
	optionsRow_->setOpen(optionsOpen_);
	optionsBox_->setVisible(optionsOpen_);
}

// ---- Acting on the clock -------------------------------------------------------------------------------------

void ClockDock::applyNow()
{
	if (!target_.empty())
		applyToSource(target_, settings_);
}

void ClockDock::changed()
{
	if (building_)
		return;
	sanitize(settings_);
	applyNow();
	scheduleSave();
}

void ClockDock::startPause()
{
	if (!status_.found) {
		toast(lt("Laycue.Clock.NoLayer"));
		return;
	}
	toggleClock(target_);
	poll();
}

void ClockDock::reset()
{
	if (!status_.found)
		return;
	resetClock(target_);
	poll();
}

void ClockDock::addLayer(bool everywhere)
{
	std::string error;
	const std::string name = addToCurrentScene(target_, error);
	if (name.empty()) {
		toast(qs(error));
		return;
	}
	target_ = name;
	applyNow();
	if (everywhere) {
		const int added = addToAllScenes(name);
		toast(lt("Laycue.Clock.AddedToScenes").arg(added));
	} else {
		toast(lt("Laycue.Clock.AddedToScene"));
	}
	scheduleSave();
	poll();
}

void ClockDock::pickDesign()
{
	QMenu menu(this);
	for (int i = 0; i < kDesignCount; ++i) {
		QAction *a = menu.addAction(lt(designKey(Design(i))));
		a->setCheckable(true);
		a->setChecked(i == settings_.design);
		a->setData(i);
	}
	QAction *chosen = menu.exec(QCursor::pos());
	if (!chosen)
		return;
	settings_.design = chosen->data().toInt();
	// A design starts with the look it was made for; the operator can change it after.
	const DesignDefaults def = defaultsFor(Design(settings_.design));
	settings_.background = int(def.background);
	settings_.align = def.align;
	settings_.vertical = def.vertical;
	optionsOpen_ = true;
	rebuild();
	changed();
}

void ClockDock::pickColour(uint32_t ClockSettings::*which, const QString &title)
{
	const QColor chosen = QColorDialog::getColor(qcolor(settings_.*which), this, title);
	if (!chosen.isValid())
		return;
	settings_.*which = (uint32_t(chosen.red()) << 16) | (uint32_t(chosen.green()) << 8) | uint32_t(chosen.blue());
	changed();
}

void ClockDock::poll()
{
	try {
		const bool hadLayer = status_.found;
		if (target_.empty()) { // the first clock layer found takes over
			const std::vector<std::string> sources = listClockSources();
			if (!sources.empty())
				target_ = sources.front();
		}
		status_ = statusOf(target_);
		if (!status_.found) { // the remembered clock is gone (another scene collection): use one that exists
			const std::vector<std::string> sources = listClockSources();
			if (!sources.empty() && sources.front() != target_) {
				target_ = sources.front();
				status_ = statusOf(target_);
			}
		}
		if (status_.found != hadLayer && !building_) {
			rebuild(); // show the controls (or the empty state) that fit
			return;
		}
		if (!readout_)
			return;
		if (!status_.found) {
			readout_->set(QStringLiteral("--:--"), lt("Laycue.Clock.NoLayerShort"), 0);
			statusLabel_->setText(QString());
			return;
		}
		std::string text = formatTime(status_.seconds, status_.countdown && !status_.overtime);
		if (status_.overtime)
			text = "+" + text;
		QString caption = status_.running ? lt("Laycue.Clock.Running") : lt("Laycue.Clock.Stopped");
		if (status_.finished && !status_.overtime)
			caption = lt("Laycue.Clock.Finished");
		readout_->set(qs(text), caption, status_.level);
		statusLabel_->setText(caption);
		if (startButton_)
			startButton_->setText(status_.running ? lt("Laycue.Clock.Pause") : lt("Laycue.Clock.Start"));
	} catch (...) {
		obs_log(LOG_ERROR, "Laycue Clock: error updating the panel");
	}
}

} // namespace laycue::clock::ui
