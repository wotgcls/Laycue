#include "ui/network-dock.hpp"

#include "obs/stream-stats.hpp"
#include "store/settings-io.hpp"
#include "ui/brand-mark.hpp"
#include "ui/controls.hpp"
#include "ui/pack-switcher.hpp"
#include "ui/stage-splitter.hpp"
#include "ui/live-chart.hpp"
#include "ui/pages/helpers.hpp"
#include "ui/style.hpp"
#include "ui/text.hpp"
#include "ui/tokens.hpp"
#include "ui/update-card.hpp"

#include <obs-module.h>
#include <plugin-support.h>

#include <QDateTime>
#include <QEvent>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QLocale>
#include <QMessageBox>
#include <QPainter>
#include <QResizeEvent>
#include <QScrollArea>
#include <QStackedWidget>
#include <QUrl>
#include <QVBoxLayout>

#include <algorithm>
#include <cmath>

namespace laycue::network::ui {

using namespace laycue::ui;
using laycue::ui::page::button;
using laycue::ui::page::innerLabel;

namespace {

constexpr int kSaveMs = 600;
constexpr int kToastMs = 4200;

QString number(double v)
{
	return QLocale().toString(int(std::lround(v)));
}

QString oneDecimal(double v)
{
	return QLocale().toString(v, 'f', 1);
}

QLabel *makeLabel(QWidget *parent, const QString &text, int px, int weight, const QColor &colour)
{
	return innerLabel(parent, text, px, weight, colour);
}

// A label that paints its own text. The panel style sheet pins every QLabel to a small size, which would
// shrink the big numbers, so this one draws with its own font.
class NumberLabel : public QLabel {
public:
	NumberLabel(QWidget *parent, const QString &text, int px, const QColor &colour)
		: QLabel(text, parent),
		  px_(px),
		  colour_(colour)
	{
		setFont(uiFont(px, 600));
		setAttribute(Qt::WA_TransparentForMouseEvents, true);
	}

	QSize sizeHint() const override
	{
		const QFontMetrics fm(uiFont(px_, 600));
		return {fm.horizontalAdvance(text()) + 2, fm.height() + 2};
	}
	QSize minimumSizeHint() const override { return sizeHint(); }

protected:
	void paintEvent(QPaintEvent *) override
	{
		QPainter p(this);
		p.setRenderHint(QPainter::TextAntialiasing, true);
		p.setPen(colour_);
		p.setFont(uiFont(px_, 600));
		p.drawText(rect(), Qt::AlignLeft | Qt::AlignVCenter, text());
	}

private:
	int px_;
	QColor colour_;
};

QLabel *monoLabel(QWidget *parent, const QString &text, int px, const QColor &colour)
{
	if (px >= 18)
		return new NumberLabel(parent, text, px, colour);
	QLabel *l = innerLabel(parent, text, px, 600, colour);
	l->setWordWrap(false);
	return l;
}

QString minutesText(long seconds)
{
	const long m = seconds / 60;
	const long s = seconds % 60;
	return m > 0 ? lt("Laycue.Network.DurationMinutes").arg(m).arg(s) : lt("Laycue.Network.DurationSeconds").arg(s);
}

} // namespace

// The health light in the header: a shape and a word as well as a colour, so it never relies on colour alone.
class HealthPill : public QWidget {
public:
	explicit HealthPill(QWidget *parent) : QWidget(parent) { setMinimumHeight(24); }

	void set(HealthState state, bool streaming)
	{
		state_ = state;
		streaming_ = streaming;
		setAccessibleName(text());
		updateGeometry();
		update();
	}

	QSize sizeHint() const override { return {QFontMetrics(uiFont(12, 600)).horizontalAdvance(text()) + 34, 24}; }

protected:
	void paintEvent(QPaintEvent *) override
	{
		QPainter p(this);
		p.setRenderHint(QPainter::Antialiasing, true);
		QColor c = color::textTertiary;
		QString glyph = QStringLiteral("○");
		if (streaming_) {
			switch (state_) {
			case HealthState::Good:
				c = color::next;
				glyph = QStringLiteral("✓");
				break;
			case HealthState::Watch:
				c = color::overlay;
				glyph = QStringLiteral("!");
				break;
			case HealthState::Poor:
				c = color::live;
				glyph = QStringLiteral("✕");
				break;
			case HealthState::Idle:
				c = color::textSecondary;
				glyph = QStringLiteral("…");
				break;
			}
		}
		p.setPen(Qt::NoPen);
		p.setBrush(QColor(c.red(), c.green(), c.blue(), 38));
		p.drawRoundedRect(rect(), height() / 2.0, height() / 2.0);
		p.setPen(c);
		p.setFont(uiFont(12, 700));
		p.drawText(QRectF(0, 0, 24, height()), Qt::AlignCenter, glyph);
		p.setFont(uiFont(12, 600));
		p.drawText(QRectF(22, 0, width() - 22, height()), Qt::AlignLeft | Qt::AlignVCenter, text());
	}

private:
	QString text() const
	{
		if (!streaming_)
			return lt("Laycue.Network.HealthIdle");
		switch (state_) {
		case HealthState::Good:
			return lt("Laycue.Network.HealthGood");
		case HealthState::Watch:
			return lt("Laycue.Network.HealthWatch");
		case HealthState::Poor:
			return lt("Laycue.Network.HealthPoor");
		case HealthState::Idle:
			return lt("Laycue.Network.HealthChecking");
		}
		return QString();
	}

	HealthState state_ = HealthState::Idle;
	bool streaming_ = false;
};

// A thin progress bar in the panel's own style.
class ProgressBar : public QWidget {
public:
	explicit ProgressBar(QWidget *parent) : QWidget(parent) { setFixedHeight(8); }
	void setValue(double v)
	{
		value_ = std::clamp(v, 0.0, 1.0);
		update();
	}

protected:
	void paintEvent(QPaintEvent *) override
	{
		QPainter p(this);
		p.setRenderHint(QPainter::Antialiasing, true);
		p.setPen(Qt::NoPen);
		p.setBrush(color::recess);
		p.drawRoundedRect(rect(), 4, 4);
		p.setBrush(color::setup);
		p.drawRoundedRect(QRectF(0, 0, width() * value_, height()), 4, 4);
	}

private:
	double value_ = 0.0;
};

NetworkDock::NetworkDock(QWidget *parent) : QWidget(parent)
{
	setObjectName("LaycueNetworkDock");
	setAttribute(Qt::WA_StyledBackground, true);
	setMinimumWidth(280);
	loadBrandFonts();

	settings_ = loadSettings();
	session_.setSettings(settings_);

	buildUi();
	rebuildHero();
	rebuildTest();
	rebuildAuto();
	rebuildReport();
	updateHealth();

	saveTimer_.setSingleShot(true);
	saveTimer_.setInterval(kSaveMs);
	connect(&saveTimer_, &QTimer::timeout, this, &NetworkDock::saveNow);
	toastTimer_.setSingleShot(true);
	toastTimer_.setInterval(kToastMs);
	connect(&toastTimer_, &QTimer::timeout, this, [this]() { toast_->hide(); });

	connect(&session_, &Session::updated, this, &NetworkDock::refreshLive);
	connect(&session_, &Session::streamStarted, this, [this]() {
		rebuildHero();
		rebuildAuto();
	});
	connect(&session_, &Session::streamStopped, this, &NetworkDock::onStreamStopped);
	connect(&session_, &Session::bitrateChanged, this, &NetworkDock::onBitrateChanged);
	connect(&session_, &Session::adaptiveStopped, this, [this](int) {
		noticeText_ = lt("Laycue.Network.EncoderIgnored");
		toast(lt("Laycue.Network.EncoderIgnoredToast"));
		rebuildHero();
		rebuildAuto();
	});
	connect(&test_, &SpeedTest::progress, this, &NetworkDock::testProgress);
	connect(&test_, &SpeedTest::finished, this, &NetworkDock::testFinished);
	connect(&test_, &SpeedTest::failed, this, &NetworkDock::testFailed);
}

NetworkDock::~NetworkDock()
{
	shuttingDown_ = true;
	test_.disconnect(this);
	test_.cancel();
	saveNow();
}

// ---- Layout ------------------------------------------------------------------------------------------------

void NetworkDock::buildUi()
{
	auto *outer = new QVBoxLayout(this);
	outer->setContentsMargins(0, 0, 0, 0);
	outer->setSpacing(0);
	content_ = new QWidget(this);
	content_->setObjectName("LaycueContent");
	content_->setAttribute(Qt::WA_StyledBackground, true);
	content_->setStyleSheet(panelStyleSheet());
	outer->addWidget(content_);
	auto *contentLayout = new QVBoxLayout(content_);
	contentLayout->setContentsMargins(0, 0, 0, 0);
	contentLayout->setSpacing(0);
	contentLayout->addWidget(packHeader(content_)); // stays on top
	pages_ = new QStackedWidget(content_);
	contentLayout->addWidget(pages_);

	home_ = new QWidget();
	auto *root = new QVBoxLayout(home_);
	root->setContentsMargins(size::margin - size::shadowMargin, 6, size::margin - size::shadowMargin, 4);
	root->setSpacing(4);

	auto *header = new QHBoxLayout();
	header->setContentsMargins(size::shadowMargin, 2, size::shadowMargin, 2);
	header->setSpacing(size::gapSmall);
	header->addWidget(makeLabel(home_, lt("Laycue.Network.Heading"), 17, 700, color::cream));
	header->addStretch(1);
	pill_ = new HealthPill(home_);
	header->addWidget(pill_, 0, Qt::AlignVCenter);
	root->addLayout(header);

	hero_ = new Surface(2, home_);
	heroLayout_ = new QVBoxLayout(hero_);
	heroLayout_->setSpacing(4);
	hero_->setPadding(12, 8, 12, 10);

	auto *tabSurface = new Surface(1, home_);
	auto *tv = new QVBoxLayout(tabSurface);
	tv->setSpacing(size::gapSmall);
	tabSurface->setPadding(10, 8, 6, 6);
	tabs_ = new Segmented(tabSurface);
	tabs_->setItems({lt("Laycue.Network.TabTest"), lt("Laycue.Network.TabAuto"), lt("Laycue.Network.TabReport")});
	tabs_->setAccessibleName(lt("Laycue.Network.TabsAccessible"));
	connect(tabs_, &Segmented::changed, this, [this](int i) {
		showTab(i == 0 ? Tab::Test : i == 1 ? Tab::Auto : Tab::Report);
	});
	tv->addWidget(tabs_);

	scroll_ = new QScrollArea(tabSurface);
	scroll_->setWidgetResizable(true);
	scroll_->setFrameShape(QFrame::NoFrame);
	scroll_->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
	scroll_->setMinimumHeight(150); // the tabs always keep some room
	auto *host = new QWidget(scroll_);
	auto *hl = new QVBoxLayout(host);
	hl->setContentsMargins(0, 0, 4, 0);
	hl->setSpacing(0);
	const auto makeTab = [&](QWidget *&tab, QVBoxLayout *&layout) {
		tab = new QWidget(host);
		layout = new QVBoxLayout(tab);
		layout->setContentsMargins(0, 0, 0, 0);
		layout->setSpacing(size::gap);
		hl->addWidget(tab);
	};
	makeTab(testTab_, testLayout_);
	makeTab(autoTab_, autoLayout_);
	makeTab(reportTab_, reportLayout_);
	autoTab_->hide();
	reportTab_->hide();
	hl->addStretch(1);
	scroll_->setWidget(host);
	tv->addWidget(scroll_, 1);
	// The live reading on top, the tabs below; drag the bar between them to give either more room.
	root->addWidget(stageSplitter(hero_, tabSurface, home_), 1);
	pages_->addWidget(home_);

	toast_ = new QWidget(content_);
	toast_->setObjectName("laycueToast");
	toast_->setAttribute(Qt::WA_StyledBackground, true);
	toast_->setStyleSheet(
		QStringLiteral(
			"#laycueToast { background: %1; border: 1px solid rgba(255,255,255,30); border-radius: 18px; }")
			.arg(hex(color::control)));
	auto *tl = new QHBoxLayout(toast_);
	tl->setContentsMargins(14, 8, 16, 8);
	toastText_ = makeLabel(toast_, QString(), 12, 500, color::cream);
	toastText_->setWordWrap(false);
	tl->addWidget(toastText_);
	toast_->hide();
}

void NetworkDock::showTab(Tab tab)
{
	tab_ = tab;
	testTab_->setVisible(tab == Tab::Test);
	autoTab_->setVisible(tab == Tab::Auto);
	reportTab_->setVisible(tab == Tab::Report);
	tabs_->setCurrent(tab == Tab::Test ? 0 : tab == Tab::Auto ? 1 : 2);
}

Platform NetworkDock::currentPlatform() const
{
	if (settings_.platform == "custom")
		return customPlatform(settings_.customMaxKbps);
	return platformById(settings_.platform);
}

Recommendation NetworkDock::recommendation() const
{
	if (!settings_.last.valid)
		return Recommendation();
	return recommend(settings_.last.measure, currentPlatform(), settings_.audioKbps);
}

// ---- The hero: the one thing that matters now --------------------------------------------------------------

void NetworkDock::rebuildHero()
{
	chart_ = nullptr;
	progress_ = nullptr;
	liveSending_ = liveTarget_ = liveDropped_ = liveNote_ = nullptr;
	page::clear(heroLayout_);

	// 1. Testing.
	if (testing_) {
		heroLayout_->addWidget(makeLabel(hero_, lt("Laycue.Network.Testing"), 14, 700, color::cream));
		auto *row = new QHBoxLayout();
		liveSending_ =
			monoLabel(hero_, lt("Laycue.Network.TestingMbps").arg(oneDecimal(testMbps_)), 28, color::cream);
		row->addWidget(liveSending_);
		row->addStretch(1);
		heroLayout_->addLayout(row);
		progress_ = new ProgressBar(hero_);
		progress_->setValue(testFraction_);
		heroLayout_->addWidget(progress_);
		heroLayout_->addWidget(
			makeLabel(hero_, lt("Laycue.Network.TestingHint"), 11, 400, color::textTertiary));
		auto *cancel = button(hero_, lt("Laycue.Network.Cancel"), "laycueSecondary");
		connect(cancel, &QAbstractButton::clicked, this, [this]() { test_.cancel(); });
		heroLayout_->addWidget(cancel, 0, Qt::AlignLeft);
		return;
	}

	// 2. Streaming: the live view.
	if (session_.streaming()) {
		auto *tiles = new QHBoxLayout();
		tiles->setSpacing(size::gapLarge);
		const auto tile = [&](QLabel *&value, const QString &caption) {
			auto *col = new QVBoxLayout();
			col->setSpacing(1);
			value = monoLabel(hero_, QStringLiteral("–"), 20, color::cream);
			col->addWidget(value);
			col->addWidget(makeLabel(hero_, caption, 11, 600, color::textTertiary));
			tiles->addLayout(col);
		};
		tile(liveSending_, lt("Laycue.Network.TileSending"));
		tile(liveTarget_, lt("Laycue.Network.TileSet"));
		tile(liveDropped_, lt("Laycue.Network.TileDropped"));
		tiles->addStretch(1);
		heroLayout_->addLayout(tiles);
		chart_ = new LiveChart(hero_);
		chart_->setAccessibleName(lt("Laycue.Network.ChartAccessible"));
		heroLayout_->addWidget(chart_);
		heroLayout_->addWidget(makeLabel(hero_, lt("Laycue.Network.ChartKey"), 11, 400, color::textTertiary));
		liveNote_ = makeLabel(hero_, QString(), 12, 500, color::textSecondary);
		heroLayout_->addWidget(liveNote_);
		if (!noticeText_.isEmpty())
			heroLayout_->addWidget(makeLabel(hero_, noticeText_, 12, 600, color::overlay));
		refreshLive();
		return;
	}

	// 3. Not streaming, with a test result: the recommendation.
	const Recommendation r = recommendation();
	if (r.valid) {
		QString ratingText;
		QColor ratingColour = color::textSecondary;
		QString glyph;
		switch (r.rating) {
		case Rating::Excellent:
			ratingText = lt("Laycue.Network.RatingExcellent");
			ratingColour = color::next;
			glyph = QStringLiteral("✓");
			break;
		case Rating::Good:
			ratingText = lt("Laycue.Network.RatingGood");
			ratingColour = color::next;
			glyph = QStringLiteral("✓");
			break;
		case Rating::Tight:
			ratingText = lt("Laycue.Network.RatingTight");
			ratingColour = color::overlay;
			glyph = QStringLiteral("!");
			break;
		default:
			ratingText = lt("Laycue.Network.RatingPoor");
			ratingColour = color::live;
			glyph = QStringLiteral("✕");
			break;
		}
		auto *topRow = new QHBoxLayout();
		topRow->setSpacing(size::gapSmall);
		topRow->addWidget(
			makeLabel(hero_, lt("Laycue.Network.Recommended").toUpper(), 11, 700, color::textTertiary));
		topRow->addStretch(1);
		topRow->addWidget(makeLabel(hero_, glyph + QStringLiteral(" ") + ratingText, 12, 700, ratingColour));
		heroLayout_->addLayout(topRow);
		auto *valueRow = new QHBoxLayout();
		valueRow->setSpacing(size::gapSmall);
		valueRow->addWidget(monoLabel(hero_, number(r.videoKbps), 26, color::cream), 0, Qt::AlignBottom);
		valueRow->addWidget(makeLabel(hero_, lt("Laycue.Network.Kbps"), 14, 600, color::textSecondary), 0,
				    Qt::AlignBottom);
		valueRow->addStretch(1);
		heroLayout_->addLayout(valueRow);
		heroLayout_->addWidget(makeLabel(hero_,
						 lt("Laycue.Network.SizeFps").arg(r.height).arg(r.fps).arg(r.audioKbps),
						 13, 500, color::cream));

		const auto when = QDateTime::fromSecsSinceEpoch(settings_.last.when).toString(QStringLiteral("h:mm"));
		heroLayout_->addWidget(makeLabel(
			hero_,
			lt("Laycue.Network.BasedOn").arg(oneDecimal(settings_.last.measure.medianMbps)).arg(when), 11,
			400, color::textTertiary));
		if (r.limitedByPlatform)
			heroLayout_->addWidget(
				makeLabel(hero_, lt("Laycue.Network.NotePlatform").arg(lt(currentPlatform().nameKey)),
					  11, 500, color::textSecondary));
		if (r.unstable)
			heroLayout_->addWidget(
				makeLabel(hero_, lt("Laycue.Network.NoteUnstable"), 11, 500, color::overlay));
		if (r.tooSlow)
			heroLayout_->addWidget(
				makeLabel(hero_, lt("Laycue.Network.NoteTooSlow"), 11, 500, color::overlay));

		auto *row = new QHBoxLayout();
		row->setSpacing(size::gapSmall);
		auto *apply = button(hero_, lt("Laycue.Network.ApplyToObs"), "laycuePrimary");
		apply->setMinimumHeight(size::controlHeight + 4);
		connect(apply, &QAbstractButton::clicked, this, &NetworkDock::applyRecommendation);
		row->addWidget(apply, 2);
		auto *again = button(hero_, lt("Laycue.Network.TestAgain"), "laycueSecondary");
		again->setMinimumHeight(size::controlHeight + 4);
		connect(again, &QAbstractButton::clicked, this, &NetworkDock::runTest);
		row->addWidget(again, 1);
		heroLayout_->addLayout(row);
		if (!testError_.isEmpty())
			heroLayout_->addWidget(makeLabel(hero_, testError_, 12, 600, color::overlay));
		return;
	}

	// 4. Nothing measured yet.
	heroLayout_->addWidget(makeLabel(hero_, lt("Laycue.Network.FirstTitle"), 14, 700, color::cream));
	heroLayout_->addWidget(makeLabel(hero_, lt("Laycue.Network.FirstBody"), 11, 400, color::textSecondary));
	auto *start = button(hero_, lt("Laycue.Network.TestMyConnection"), "laycuePrimary");
	start->setMinimumHeight(size::controlHeight + 4);
	connect(start, &QAbstractButton::clicked, this, &NetworkDock::runTest);
	heroLayout_->addWidget(start);
	if (!testError_.isEmpty())
		heroLayout_->addWidget(makeLabel(hero_, testError_, 12, 600, color::overlay));
}

void NetworkDock::refreshLive()
{
	if (shuttingDown_)
		return;
	updateHealth();
	if (!session_.streaming() || !chart_)
		return;
	const auto &points = session_.chart();
	chart_->setData(points);
	const HealthReport &h = session_.health();
	if (!points.empty()) {
		liveSending_->setText(number(points.back().sentKbps));
		liveTarget_->setText(number(session_.targetKbps()));
	}
	liveDropped_->setText(oneDecimal(h.dropPercent) + QStringLiteral("%"));

	QString note;
	if (h.state == HealthState::Idle) {
		note = lt("Laycue.Network.NoteChecking");
	} else if (h.state == HealthState::Good) {
		note = lt("Laycue.Network.NoteSteady");
	} else {
		note = lt("Laycue.Network.NoteTrouble");
		if (!session_.adaptiveActive() && !settings_.adaptive)
			note += QStringLiteral(" ") + lt("Laycue.Network.NoteTurnOnAuto");
	}
	if (!lastChangeText_.isEmpty())
		note += QStringLiteral("\n") + lastChangeText_;
	if (session_.blockedByObsDynamic())
		note += QStringLiteral("\n") + lt("Laycue.Network.NoteObsDynamic");
	liveNote_->setText(note);
}

void NetworkDock::updateHealth()
{
	pill_->set(session_.health().state, session_.streaming());
}

// ---- Test tab ----------------------------------------------------------------------------------------------

void NetworkDock::rebuildTest()
{
	page::clear(testLayout_);
	const auto group = [&](const QString &title) -> QVBoxLayout * {
		testLayout_->addWidget(makeLabel(testTab_, title.toUpper(), 11, 700, color::textTertiary));
		auto *card = new Surface(1, testTab_);
		auto *v = new QVBoxLayout(card);
		v->setSpacing(size::gapSmall + 2);
		card->setPadding(14, 10, 14, 10);
		testLayout_->addWidget(card);
		return v;
	};

	{
		QVBoxLayout *v = group(lt("Laycue.Network.Platform"));
		auto *seg = new Segmented(testTab_);
		QStringList names;
		for (const Platform &p : platforms())
			names << lt(p.nameKey);
		seg->setItems(names);
		int index = 0;
		for (size_t i = 0; i < platforms().size(); ++i)
			if (platforms()[i].id == settings_.platform)
				index = int(i);
		seg->setCurrent(index);
		seg->setAccessibleName(lt("Laycue.Network.Platform"));
		connect(seg, &Segmented::changed, this, [this](int i) {
			settings_.platform = platforms()[size_t(i)].id;
			changed();
			rebuildHero();
			rebuildTest();
		});
		v->addWidget(seg);
		v->addWidget(makeLabel(testTab_,
				       lt("Laycue.Network.PlatformHint").arg(number(currentPlatform().maxVideoKbps)),
				       11, 400, color::textTertiary));
	}

	if (settings_.last.valid) {
		QVBoxLayout *v = group(lt("Laycue.Network.LastTest"));
		const UploadMeasure &m = settings_.last.measure;
		const auto row = [&](const QString &label, const QString &value) {
			auto *h = new QHBoxLayout();
			h->addWidget(makeLabel(testTab_, label, 13, 500, color::textSecondary), 1);
			h->addWidget(monoLabel(testTab_, value, 13, color::cream));
			v->addLayout(h);
		};
		row(lt("Laycue.Network.RowTypical"), lt("Laycue.Network.Mbps").arg(oneDecimal(m.medianMbps)));
		row(lt("Laycue.Network.RowLowest"), lt("Laycue.Network.Mbps").arg(oneDecimal(m.lowMbps)));
		row(lt("Laycue.Network.RowSteadiness"), m.jitter < 0.15   ? lt("Laycue.Network.SteadyHigh")
							: m.jitter < 0.35 ? lt("Laycue.Network.SteadyMedium")
									  : lt("Laycue.Network.SteadyLow"));
		row(lt("Laycue.Network.RowLatency"), lt("Laycue.Network.Ms").arg(number(m.rttMs)));
	}

	{
		QVBoxLayout *v = group(lt("Laycue.Network.TestServer"));
		auto *edit = new QLineEdit(testTab_);
		edit->setText(QString::fromStdString(settings_.testServer));
		edit->setAccessibleName(lt("Laycue.Network.TestServer"));
		connect(edit, &QLineEdit::editingFinished, this, [this, edit]() {
			settings_.testServer = edit->text().trimmed().toStdString();
			sanitize(settings_);
			changed();
		});
		v->addWidget(edit);
		v->addWidget(makeLabel(testTab_, lt("Laycue.Network.TestServerHint"), 11, 400, color::textTertiary));
		auto *run = button(testTab_, lt("Laycue.Network.TestMyConnection"), "laycueSecondary");
		run->setEnabled(!testing_ && !session_.streaming());
		connect(run, &QAbstractButton::clicked, this, &NetworkDock::runTest);
		v->addWidget(run, 0, Qt::AlignLeft);
		if (session_.streaming())
			v->addWidget(
				makeLabel(testTab_, lt("Laycue.Network.TestNotWhileLive"), 11, 500, color::overlay));
	}
	testLayout_->addWidget(
		makeLabel(testTab_, lt("Laycue.Update.Heading").toUpper(), 11, 700, color::textTertiary));
	testLayout_->addWidget(new UpdateCard(updater_, testTab_));
	testLayout_->addStretch(1);
}

void NetworkDock::runTest()
{
	if (testing_)
		return;
	if (session_.streaming()) {
		toast(lt("Laycue.Network.TestNotWhileLive"));
		return;
	}
	if (!settings_.testConsent) {
		const auto answer = QMessageBox::question(
			this, lt("Laycue.Network.ConsentTitle"),
			lt("Laycue.Network.ConsentBody").arg(QString::fromStdString(settings_.testServer)));
		if (answer != QMessageBox::Yes)
			return;
		settings_.testConsent = true;
		changed();
	}
	testError_.clear();
	testing_ = true;
	testMbps_ = 0.0;
	testFraction_ = 0.0;
	rebuildHero();
	rebuildTest();
	test_.start(QUrl(QString::fromStdString(settings_.testServer)), settings_.testSeconds);
}

void NetworkDock::testProgress(double mbps, double fraction)
{
	testMbps_ = mbps;
	testFraction_ = fraction;
	if (liveSending_)
		liveSending_->setText(lt("Laycue.Network.TestingMbps").arg(oneDecimal(mbps)));
	if (progress_)
		progress_->setValue(fraction);
}

void NetworkDock::testFinished(const UploadMeasure &measure)
{
	testing_ = false;
	settings_.last.valid = true;
	settings_.last.measure = measure;
	settings_.last.when = QDateTime::currentSecsSinceEpoch();
	settings_.last.platformId = settings_.platform;
	changed();
	rebuildHero();
	rebuildTest();
}

void NetworkDock::testFailed(SpeedTest::Failure reason)
{
	testing_ = false;
	switch (reason) {
	case SpeedTest::Failure::BadAddress:
		testError_ = lt("Laycue.Network.ErrAddress");
		break;
	case SpeedTest::Failure::Unreachable:
		testError_ = lt("Laycue.Network.ErrUnreachable");
		if (!test_.detail().isEmpty()) // the real reason, so a screenshot says what is wrong
			testError_ += QStringLiteral(" (") + test_.detail() + QStringLiteral(")");
		break;
	case SpeedTest::Failure::NoSecure:
		testError_ = lt("Laycue.Network.ErrNoSecure");
		break;
	case SpeedTest::Failure::NotEnoughData:
		testError_ = lt("Laycue.Network.ErrNotEnough");
		break;
	case SpeedTest::Failure::Cancelled:
		testError_.clear();
		break;
	}
	rebuildHero();
	rebuildTest();
}

void NetworkDock::applyRecommendation()
{
	const Recommendation r = recommendation();
	if (!r.valid)
		return;
	const std::string where = applyBitrateToProfile(r.videoKbps);
	if (where.empty()) {
		toast(lt("Laycue.Network.ApplyFailed"));
		return;
	}
	toast(lt("Laycue.Network.AppliedToast").arg(number(r.videoKbps)).arg(QString::fromStdString(where)));
}

// ---- Auto tab ----------------------------------------------------------------------------------------------

void NetworkDock::rebuildAuto()
{
	page::clear(autoLayout_);
	const auto group = [&](const QString &title) -> QVBoxLayout * {
		autoLayout_->addWidget(makeLabel(autoTab_, title.toUpper(), 11, 700, color::textTertiary));
		auto *card = new Surface(1, autoTab_);
		auto *v = new QVBoxLayout(card);
		v->setSpacing(size::gapSmall + 2);
		card->setPadding(14, 10, 14, 10);
		autoLayout_->addWidget(card);
		return v;
	};

	{
		QVBoxLayout *v = group(lt("Laycue.Network.AutoTitle"));
		auto *row = new QHBoxLayout();
		row->setSpacing(size::gap);
		auto *text = new QVBoxLayout();
		text->setSpacing(1);
		text->addWidget(makeLabel(autoTab_, lt("Laycue.Network.AutoSwitch"), 13, 600, color::cream));
		text->addWidget(makeLabel(autoTab_, lt("Laycue.Network.AutoSwitchSub"), 11, 400, color::textTertiary));
		row->addLayout(text, 1);
		auto *sw = new Switch(autoTab_);
		sw->setAccessibleName(lt("Laycue.Network.AutoSwitch"));
		sw->setOn(settings_.adaptive);
		connect(sw, &QAbstractButton::toggled, this, &NetworkDock::setAdaptive);
		row->addWidget(sw, 0, Qt::AlignVCenter);
		v->addLayout(row);
		if (obsDynamicBitrateEnabled()) {
			v->addWidget(makeLabel(autoTab_, lt("Laycue.Network.ObsDynamicOn"), 11, 600, color::overlay));
			auto *off = button(autoTab_, lt("Laycue.Network.TurnObsDynamicOff"), "laycueSecondary");
			connect(off, &QAbstractButton::clicked, this, [this]() {
				setObsDynamicBitrate(false);
				toast(lt("Laycue.Network.ObsDynamicOffToast"));
				rebuildAuto();
			});
			v->addWidget(off, 0, Qt::AlignLeft);
		}
		if (!noticeText_.isEmpty()) {
			v->addWidget(makeLabel(autoTab_, noticeText_, 11, 600, color::overlay));
			auto *clear = button(autoTab_, lt("Laycue.Network.GotIt"), "laycueLink");
			connect(clear, &QAbstractButton::clicked, this, [this]() {
				noticeText_.clear();
				rebuildAuto();
				rebuildHero();
			});
			v->addWidget(clear, 0, Qt::AlignLeft);
		}
	}

	{
		QVBoxLayout *v = group(lt("Laycue.Network.AutoLimits"));
		const auto stepper = [&](const QString &label, const QString &value, std::function<void(int)> change) {
			auto *h = new QHBoxLayout();
			h->setSpacing(size::gapSmall);
			h->addWidget(makeLabel(autoTab_, label, 13, 500, color::cream), 1);
			auto *minus = button(autoTab_, QStringLiteral("−"), "laycueSecondary");
			minus->setAccessibleName(lt("Laycue.Network.Less"));
			connect(minus, &QAbstractButton::clicked, this, [change]() { change(-1); });
			h->addWidget(minus);
			h->addWidget(monoLabel(autoTab_, value, 13, color::cream));
			auto *plus = button(autoTab_, QStringLiteral("+"), "laycueSecondary");
			plus->setAccessibleName(lt("Laycue.Network.More"));
			connect(plus, &QAbstractButton::clicked, this, [change]() { change(+1); });
			h->addWidget(plus);
			v->addLayout(h);
		};
		stepper(lt("Laycue.Network.Floor"), lt("Laycue.Network.KbpsValue").arg(number(settings_.floorKbps)),
			[this](int d) {
				settings_.floorKbps = std::clamp(settings_.floorKbps + 250 * d, 300, 20000);
				sanitize(settings_);
				changed();
				rebuildAuto();
			});
		v->addWidget(makeLabel(autoTab_, lt("Laycue.Network.FloorHint"), 11, 400, color::textTertiary));

		auto *seg = new Segmented(autoTab_);
		seg->setItems({lt("Laycue.Network.CeilingKeep"), lt("Laycue.Network.CeilingChoose")});
		seg->setCurrent(settings_.ceilingFromProfile ? 0 : 1);
		seg->setAccessibleName(lt("Laycue.Network.Ceiling"));
		connect(seg, &Segmented::changed, this, [this](int i) {
			settings_.ceilingFromProfile = i == 0;
			if (!settings_.ceilingFromProfile && settings_.ceilingKbps < settings_.floorKbps)
				settings_.ceilingKbps = settings_.floorKbps;
			changed();
			rebuildAuto();
		});
		v->addWidget(makeLabel(autoTab_, lt("Laycue.Network.Ceiling"), 13, 500, color::cream));
		v->addWidget(seg);
		if (!settings_.ceilingFromProfile) {
			stepper(lt("Laycue.Network.CeilingValue"),
				lt("Laycue.Network.KbpsValue").arg(number(settings_.ceilingKbps)), [this](int d) {
					settings_.ceilingKbps =
						std::clamp(settings_.ceilingKbps + 250 * d, settings_.floorKbps, 50000);
					changed();
					rebuildAuto();
				});
		}
		v->addWidget(makeLabel(autoTab_, lt("Laycue.Network.CeilingHint"), 11, 400, color::textTertiary));
	}

	if (session_.streaming()) {
		QVBoxLayout *v = group(lt("Laycue.Network.AutoNow"));
		v->addWidget(makeLabel(autoTab_,
				       session_.adaptiveActive() ? lt("Laycue.Network.AutoActive")
									   .arg(number(session_.targetKbps()))
									   .arg(number(session_.ceilingKbps()))
								 : lt("Laycue.Network.AutoNotActive"),
				       13, 500, color::cream));
		if (!lastChangeText_.isEmpty())
			v->addWidget(makeLabel(autoTab_, lastChangeText_, 11, 400, color::textSecondary));
		if (session_.adaptiveActive()) {
			auto *full = button(autoTab_, lt("Laycue.Network.TryFull"), "laycueSecondary");
			full->setToolTip(lt("Laycue.Network.TryFullTip"));
			connect(full, &QAbstractButton::clicked, this, [this]() {
				session_.tryFullBitrate();
				toast(lt("Laycue.Network.TryFullToast"));
			});
			v->addWidget(full, 0, Qt::AlignLeft);
		}
	}
	autoLayout_->addStretch(1);
}

void NetworkDock::setAdaptive(bool on)
{
	settings_.adaptive = on;
	changed();
	session_.setSettings(settings_);
	noticeText_.clear();
	toast(lt(on ? "Laycue.Network.AutoOnToast" : "Laycue.Network.AutoOffToast"));
	rebuildAuto();
	rebuildHero();
}

void NetworkDock::onBitrateChanged(int fromKbps, int toKbps, int reason)
{
	QString why;
	switch (AdaptiveReason(reason)) {
	case AdaptiveReason::Dropped:
		why = lt("Laycue.Network.WhyDropped");
		break;
	case AdaptiveReason::Congestion:
		why = lt("Laycue.Network.WhyCongestion");
		break;
	case AdaptiveReason::Recovering:
		why = lt("Laycue.Network.WhyRecovering");
		break;
	default:
		why = QString();
		break;
	}
	lastChangeText_ = lt(toKbps < fromKbps ? "Laycue.Network.ChangedDown" : "Laycue.Network.ChangedUp")
				  .arg(number(toKbps))
				  .arg(why)
				  .arg(QDateTime::currentDateTime().toString(QStringLiteral("h:mm:ss")));
	toast(lt(toKbps < fromKbps ? "Laycue.Network.ToastDown" : "Laycue.Network.ToastUp").arg(number(toKbps)));
	rebuildAuto();
}

// ---- Report tab --------------------------------------------------------------------------------------------

void NetworkDock::onStreamStopped(const StreamReport &report)
{
	HistoryItem item;
	item.when = QDateTime::currentSecsSinceEpoch();
	item.report = report;
	if (report.seconds >= 30) { // a few seconds of connecting is not a stream worth a report
		addHistory(settings_, item);
		changed();
		toast(lt("Laycue.Network.ReportReady"));
		showTab(Tab::Report);
	}
	lastChangeText_.clear();
	rebuildHero();
	rebuildAuto();
	rebuildReport();
}

void NetworkDock::rebuildReport()
{
	page::clear(reportLayout_);
	if (settings_.history.empty()) {
		auto *card = new Surface(1, reportTab_);
		auto *v = new QVBoxLayout(card);
		v->setSpacing(size::gapSmall);
		card->setPadding(14, 12, 14, 12);
		v->addWidget(makeLabel(card, lt("Laycue.Network.ReportEmptyTitle"), 14, 700, color::cream));
		v->addWidget(makeLabel(card, lt("Laycue.Network.ReportEmptyBody"), 12, 400, color::textSecondary));
		reportLayout_->addWidget(card);
		reportLayout_->addStretch(1);
		return;
	}
	for (size_t i = settings_.history.size(); i-- > 0;) {
		const HistoryItem &h = settings_.history[i];
		const StreamReport &r = h.report;
		auto *card = new Surface(1, reportTab_);
		auto *v = new QVBoxLayout(card);
		v->setSpacing(size::gapSmall);
		card->setPadding(14, 10, 14, 12);
		QString verdict;
		QColor colour = color::textSecondary;
		QString glyph;
		switch (r.verdict) {
		case Verdict::Smooth:
			verdict = lt("Laycue.Network.VerdictSmooth");
			colour = color::next;
			glyph = QStringLiteral("✓");
			break;
		case Verdict::Bumpy:
			verdict = lt("Laycue.Network.VerdictBumpy");
			colour = color::overlay;
			glyph = QStringLiteral("!");
			break;
		case Verdict::Rough:
			verdict = lt("Laycue.Network.VerdictRough");
			colour = color::live;
			glyph = QStringLiteral("✕");
			break;
		}
		auto *top = new QHBoxLayout();
		top->addWidget(makeLabel(card, glyph + QStringLiteral("  ") + verdict, 14, 700, colour), 1);
		top->addWidget(makeLabel(
			card, QDateTime::fromSecsSinceEpoch(h.when).toString(QStringLiteral("ddd d MMM, h:mm")), 11,
			500, color::textTertiary));
		v->addLayout(top);
		const auto row = [&](const QString &label, const QString &value) {
			auto *hl = new QHBoxLayout();
			hl->addWidget(makeLabel(card, label, 12, 500, color::textSecondary), 1);
			hl->addWidget(monoLabel(card, value, 12, color::cream));
			v->addLayout(hl);
		};
		row(lt("Laycue.Network.RepLength"), minutesText(r.seconds));
		row(lt("Laycue.Network.RepAverage"), lt("Laycue.Network.KbpsValue").arg(number(r.averageKbps)));
		row(lt("Laycue.Network.RepDropped"), oneDecimal(r.dropPercent) + QStringLiteral("%"));
		row(lt("Laycue.Network.RepChanges"), QString::number(r.bitrateChanges));
		if (r.worstMinuteDropPercent > 0.0)
			row(lt("Laycue.Network.RepWorst"), lt("Laycue.Network.RepWorstValue")
								   .arg(r.worstMinuteStart / 60 + 1)
								   .arg(oneDecimal(r.worstMinuteDropPercent)));
		reportLayout_->addWidget(card);
		if (i + 3 < settings_.history.size())
			break; // the latest few are enough on screen
	}
	reportLayout_->addStretch(1);
}

// ---- Housekeeping ------------------------------------------------------------------------------------------

void NetworkDock::toast(const QString &message)
{
	toastText_->setText(message);
	toast_->adjustSize();
	toast_->move((width() - toast_->width()) / 2, height() - toast_->height() - 18);
	toast_->show();
	toast_->raise();
	toastTimer_.start();
}

void NetworkDock::changed()
{
	session_.setSettings(settings_);
	saveTimer_.start();
}

void NetworkDock::saveNow()
{
	saveTimer_.stop();
	saveSettings(settings_);
}

void NetworkDock::resizeEvent(QResizeEvent *event)
{
	QWidget::resizeEvent(event);
	if (toast_ && toast_->isVisible()) {
		toast_->adjustSize();
		toast_->move((width() - toast_->width()) / 2, height() - toast_->height() - 18);
	}
}

void NetworkDock::handleFrontendEvent(enum obs_frontend_event event)
{
	switch (event) {
	case OBS_FRONTEND_EVENT_EXIT:
		shuttingDown_ = true;
		test_.disconnect(this);
		test_.cancel();
		saveNow();
		break;
	case OBS_FRONTEND_EVENT_PROFILE_CHANGED:
		rebuildAuto();
		break;
	default:
		break;
	}
}

void NetworkDock::runHotkey(int index)
{
	switch (index) {
	case 0:
		setAdaptive(!settings_.adaptive);
		break;
	case 1:
		session_.tryFullBitrate();
		toast(lt("Laycue.Network.TryFullToast"));
		break;
	case 2:
		runTest();
		break;
	default:
		break;
	}
}

bool NetworkDock::event(QEvent *event)
{
	if (event->type() == QEvent::Type(QEvent::User + QEvent::Close))
		saveNow();
	return QWidget::event(event);
}

} // namespace laycue::network::ui
