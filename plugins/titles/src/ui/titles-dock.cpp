#include "ui/titles-dock.hpp"

#include "obs/title-source.hpp"
#include "render/font-set.hpp"
#include "store/library-io.hpp"
#include "ui/brand-mark.hpp"
#include "ui/controls.hpp"
#include "ui/pack-switcher.hpp"
#include "ui/presence-text.hpp"
#include "ui/customize-page.hpp"
#include "ui/logo-parts-dialog.hpp"
#include "ui/pages/helpers.hpp"
#include "ui/style.hpp"
#include "ui/text.hpp"
#include "ui/title-widgets.hpp"
#include "ui/update-card.hpp"
#include "ui/tokens.hpp"

#include <obs-module.h>
#include <plugin-support.h>

#include <QApplication>
#include <QColorDialog>
#include <QEvent>
#include <QFileDialog>
#include <QFileInfo>
#include <QGridLayout>
#include <QInputDialog>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QMenu>
#include <QMessageBox>
#include <QPainter>
#include <QPlainTextEdit>
#include <QResizeEvent>
#include <QScrollArea>
#include <QStackedWidget>
#include <QVBoxLayout>

#include <algorithm>

namespace laycue::titles::ui {

using namespace laycue::ui;
using laycue::ui::page::button;
using laycue::ui::page::innerLabel;

namespace {

// What the renderer draws, from what the panel holds.
TitleText toText(const TitleContent &c)
{
	TitleText t;
	t.line1 = QString::fromStdString(c.line1);
	t.line2 = QString::fromStdString(c.line2);
	t.tag = QString::fromStdString(c.tag);
	t.note = QString::fromStdString(c.note);
	t.logoPath = QString::fromStdString(c.logo);
	t.logoCut = c.logoCut;
	t.logoStacked = c.logoStacked;
	t.logoMarkSecond = c.logoMarkSecond;
	for (const Row &r : c.rows)
		t.rows.append({QString::fromStdString(r.label), QString::fromStdString(r.value)});
	return t;
}

constexpr int kPollMs = 250;
constexpr int kSaveMs = 700;
constexpr int kToastMs = 3200;
constexpr int kCardMinWidth = 118;

QString fromStd(const std::string &s)
{
	return QString::fromStdString(s);
}

std::string toStd(const QString &s)
{
	return s.toStdString();
}

// A small round status light, drawn by Laycue (red = on air, grey = hidden).
class StatusDot : public QWidget {
public:
	explicit StatusDot(QWidget *parent) : QWidget(parent) { setFixedSize(10, 10); }
	void setOn(bool on)
	{
		on_ = on;
		update();
	}

protected:
	void paintEvent(QPaintEvent *) override
	{
		QPainter p(this);
		p.setRenderHint(QPainter::Antialiasing, true);
		p.setPen(Qt::NoPen);
		p.setBrush(on_ ? color::live : color::textDisabled);
		p.drawEllipse(rect().adjusted(1, 1, -1, -1));
	}

private:
	bool on_ = false;
};

QLabel *makeLabel(QWidget *parent, const QString &text, int px, int weight, const QColor &colour)
{
	QLabel *l = innerLabel(parent, text, px, weight, colour);
	return l;
}

// The locale text for a key, or "" if the key has no text (OBS then hands back the key itself).
std::string localised(const std::string &key)
{
	const char *text = obs_module_text(key.c_str());
	if (!text || key == text)
		return std::string();
	// A sample text can hold several ticker items, written "\n" in the language file.
	std::string out(text);
	for (size_t at = out.find("\\n"); at != std::string::npos; at = out.find("\\n", at + 1))
		out.replace(at, 2, "\n");
	return out;
}

} // namespace

TitlesDock::TitlesDock(QWidget *parent) : QWidget(parent)
{
	setObjectName("LaycueTitlesDock");
	setAttribute(Qt::WA_StyledBackground, true);
	setMinimumWidth(280);
	loadBrandFonts();

	library_ = loadLibrary();
	buildUi();
	refreshSources();
	refreshFields();
	rebuildTemplates();
	rebuildQueue();
	rebuildSetup();
	updatePreview();
	updateButtons();
	updateStatus();

	pollTimer_.setInterval(kPollMs);
	connect(&pollTimer_, &QTimer::timeout, this, &TitlesDock::pollStatus);
	saveTimer_.setSingleShot(true);
	saveTimer_.setInterval(kSaveMs);
	connect(&saveTimer_, &QTimer::timeout, this, &TitlesDock::saveNow);
	pendingTimer_.setInterval(60);
	connect(&pendingTimer_, &QTimer::timeout, this, [this]() {
		// A title is leaving so the next one can arrive: wait until it is gone.
		const TitleStatus st = statusOf(target());
		if (!st.onAir() || !pendingPlay_) {
			pendingTimer_.stop();
			if (pendingPlay_) {
				pendingPlay_ = false;
				applyAndShow();
			}
		}
	});
	toastTimer_.setSingleShot(true);
	toastTimer_.setInterval(kToastMs);
	connect(&toastTimer_, &QTimer::timeout, this, [this]() { toast_->hide(); });
	pollTimer_.start();
}

TitlesDock::~TitlesDock()
{
	shuttingDown_ = true;
	pollTimer_.stop();
	saveNow();
}

// ---- Building the panel ----------------------------------------------------------------------------------

void TitlesDock::buildUi()
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
	pages_ = new QStackedWidget(content_);
	contentLayout->addWidget(pages_);

	home_ = new QWidget();
	auto *root = new QVBoxLayout(home_);
	root->setContentsMargins(size::margin - size::shadowMargin, 6, size::margin - size::shadowMargin, 4);
	root->setSpacing(4);

	// Header: the name and whether a title is on air.
	root->addWidget(new PackSwitcher(home_));
	auto *header = new QHBoxLayout();
	header->setContentsMargins(size::shadowMargin, 2, size::shadowMargin, 2);
	header->setSpacing(size::gapSmall);
	header->addWidget(brandMark(home_));
	header->addWidget(makeLabel(home_, lt("Laycue.Titles.Heading"), 17, 700, color::cream));
	header->addStretch(1);
	statusDot_ = new StatusDot(home_);
	header->addWidget(statusDot_, 0, Qt::AlignVCenter);
	statusLabel_ = makeLabel(home_, QString(), 12, 600, color::textSecondary);
	statusLabel_->setWordWrap(false);
	header->addWidget(statusLabel_, 0, Qt::AlignVCenter);
	root->addLayout(header);

	root->addWidget(buildStage());
	root->addWidget(buildTabs(), 1);

	// If the panel is shorter than everything needs, the page scrolls instead of squeezing the templates.
	auto *homeScroll = new QScrollArea(pages_);
	homeScroll->setWidgetResizable(true);
	homeScroll->setFrameShape(QFrame::NoFrame);
	homeScroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
	homeScroll->setWidget(home_);
	pages_->addWidget(homeScroll);

	toast_ = new QWidget(content_);
	toast_->setObjectName("laycueToast");
	toast_->setAttribute(Qt::WA_StyledBackground, true);
	toast_->setStyleSheet(
		QStringLiteral(
			"#laycueToast { background: %1; border: 1px solid rgba(255,255,255,30); border-radius: 18px; }")
			.arg(hex(color::control)));
	auto *toastLayout = new QHBoxLayout(toast_);
	toastLayout->setContentsMargins(14, 8, 16, 8);
	toastText_ = makeLabel(toast_, QString(), 12, 500, color::cream);
	toastText_->setWordWrap(false);
	toastLayout->addWidget(toastText_);
	toast_->hide();
}

QWidget *TitlesDock::buildStage()
{
	auto *stage = new Surface(2, home_);
	auto *v = new QVBoxLayout(stage);
	v->setSpacing(size::gapSmall);
	stage->setPadding(12, 8, 12, 10);

	layerRow_ = new RowButton(stage);
	layerRow_->setTitle(lt("Laycue.Titles.LayerRow"));
	layerRow_->setOpen(true);
	connect(layerRow_, &RowButton::clicked, this, &TitlesDock::pickLayer);
	v->addWidget(layerRow_);
	sceneLabel_ = makeLabel(stage, QString(), 11, 500, color::textTertiary);
	sceneLabel_->setWordWrap(true);
	v->addWidget(sceneLabel_);

	preview_ = new TitlePreview(stage);
	preview_->setAccessibleName(lt("Laycue.Titles.PreviewAccessible"));
	v->addWidget(preview_);

	// Each field is one row, "label  [box]", so a small panel is not spent on a label above every box.
	constexpr int kLabelWidth = 96;
	const auto field = [&](QWidget *&row, QLabel *&label, const QString &name,
			       std::initializer_list<QWidget *> boxes) {
		row = new QWidget(stage);
		auto *h = new QHBoxLayout(row);
		h->setContentsMargins(0, 0, 0, 0);
		h->setSpacing(size::gapSmall);
		label = makeLabel(row, name, 11, 600, color::textTertiary);
		label->setFixedWidth(kLabelWidth);
		label->setWordWrap(true); // a long field name takes two lines rather than being cut off
		h->addWidget(label, 0, Qt::AlignTop);
		for (QWidget *box : boxes)
			h->addWidget(box, 1);
		v->addWidget(row);
	};

	edit1_ = new QLineEdit(stage);
	long1_ = new QPlainTextEdit(stage);
	long1_->setFixedHeight(64);
	long1_->setTabChangesFocus(true);
	field(fieldRow1_, label1_, lt("Laycue.Titles.Line1"), {edit1_, long1_});
	connect(edit1_, &QLineEdit::textEdited, this, &TitlesDock::fieldsEdited);
	connect(long1_, &QPlainTextEdit::textChanged, this, [this]() {
		if (!buildingFields_)
			fieldsEdited();
	});

	edit2_ = new QLineEdit(stage);
	field(fieldRow2_, label2_, lt("Laycue.Titles.Line2"), {edit2_});
	connect(edit2_, &QLineEdit::textEdited, this, &TitlesDock::fieldsEdited);

	edit3_ = new QLineEdit(stage);
	field(fieldRow3_, label3_, lt("Laycue.Titles.Tag"), {edit3_});
	connect(edit3_, &QLineEdit::textEdited, this, &TitlesDock::fieldsEdited);

	edit4_ = new QLineEdit(stage);
	long4_ = new QPlainTextEdit(stage);
	long4_->setFixedHeight(64);
	long4_->setTabChangesFocus(true);
	field(fieldRow4_, label4_, lt("Laycue.Titles.Note"), {edit4_, long4_});
	connect(edit4_, &QLineEdit::textEdited, this, &TitlesDock::fieldsEdited);
	connect(long4_, &QPlainTextEdit::textChanged, this, [this]() {
		if (!buildingFields_)
			fieldsEdited();
	});

	// A logo picture.
	logoBox_ = new QWidget(stage);
	auto *lv = new QHBoxLayout(logoBox_);
	lv->setContentsMargins(0, 2, 0, 2);
	lv->setSpacing(size::gapSmall);
	lv->addWidget(makeLabel(logoBox_, lt("Laycue.Titles.Logo"), 13, 600, color::cream));
	logoName_ = makeLabel(logoBox_, QString(), 12, 400, color::textTertiary);
	logoName_->setWordWrap(false);
	logoName_->setSizePolicy(QSizePolicy::Ignored,
				 QSizePolicy::Preferred); // a long file name is cut, not a wide panel
	lv->addWidget(logoName_, 1);
	auto *pickLogo = button(logoBox_, lt("Laycue.Titles.ChooseLogo"), "laycueSecondary");
	connect(pickLogo, &QAbstractButton::clicked, this, &TitlesDock::chooseLogo);
	lv->addWidget(pickLogo);
	auto *clearLogoButton = button(logoBox_, lt("Laycue.Titles.RemoveLogo"), "laycueLink");
	connect(clearLogoButton, &QAbstractButton::clicked, this, &TitlesDock::clearLogo);
	lv->addWidget(clearLogoButton);
	v->addWidget(logoBox_);

	// Logo mark and wordmark: only for the template that animates them apart.
	partsBox_ = new QWidget(stage);
	auto *pb = new QHBoxLayout(partsBox_);
	pb->setContentsMargins(0, 0, 0, 0);
	pb->setSpacing(size::gapSmall);
	partsLabel_ = makeLabel(partsBox_, QString(), 12, 400, color::textTertiary);
	partsLabel_->setWordWrap(true); // the sentence wraps instead of widening the panel
	partsLabel_->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Preferred);
	pb->addWidget(partsLabel_, 1);
	auto *setParts = button(partsBox_, lt("Laycue.Titles.SetLogoParts"), "laycueSecondary");
	connect(setParts, &QAbstractButton::clicked, this, &TitlesDock::chooseLogoParts);
	pb->addWidget(setParts);
	v->addWidget(partsBox_);

	// The accounts.
	rowsBox_ = new QWidget(stage);
	rowsLayout_ = new QVBoxLayout(rowsBox_);
	rowsLayout_->setContentsMargins(0, 0, 0, 0);
	rowsLayout_->setSpacing(size::gapSmall);
	v->addWidget(rowsBox_);
	keptBox_ = new QWidget(stage);
	keptLayout_ = new QVBoxLayout(keptBox_);
	keptLayout_->setContentsMargins(0, 0, 0, 0);
	keptLayout_->setSpacing(size::gapSmall);
	v->addWidget(keptBox_);

	// Position and size: only for titles that do not span the whole picture.
	placementBox_ = new QWidget(stage);
	auto *pv = new QHBoxLayout(placementBox_);
	pv->setContentsMargins(0, 2, 0, 2);
	pv->setSpacing(size::gapSmall);
	alignSeg_ = new Segmented(placementBox_);
	alignSeg_->setItems(
		{lt("Laycue.Titles.AlignLeft"), lt("Laycue.Titles.AlignCenter"), lt("Laycue.Titles.AlignRight")});
	alignSeg_->setAccessibleName(lt("Laycue.Titles.Position"));
	connect(alignSeg_, &Segmented::changed, this, [this](int i) { changePlacement(i, 0); });
	pv->addWidget(alignSeg_, 1);
	auto *smaller = button(placementBox_, QStringLiteral("\u2212"), "laycueSecondary");
	smaller->setAccessibleName(lt("Laycue.Titles.Smaller"));
	connect(smaller, &QAbstractButton::clicked, this, [this]() { changePlacement(-1, -1); });
	pv->addWidget(smaller);
	sizeValue_ = makeLabel(placementBox_, QStringLiteral("100%"), 13, 600, color::cream);
	sizeValue_->setAlignment(Qt::AlignCenter);
	sizeValue_->setMinimumWidth(52);
	sizeValue_->setWordWrap(false);
	pv->addWidget(sizeValue_);
	auto *bigger = button(placementBox_, QStringLiteral("+"), "laycueSecondary");
	bigger->setAccessibleName(lt("Laycue.Titles.Bigger"));
	connect(bigger, &QAbstractButton::clicked, this, [this]() { changePlacement(-1, +1); });
	pv->addWidget(bigger);

	// How wide the name's shape is: the template's own, hugging the text, or a width the operator sets.
	shapeBox_ = new QWidget(stage);
	auto *sv = new QVBoxLayout(shapeBox_);
	sv->setContentsMargins(0, 2, 0, 2);
	sv->setSpacing(size::gapSmall);
	fitSeg_ = new Segmented(shapeBox_);
	fitSeg_->setItems({lt("Laycue.Titles.FitDefault"), lt("Laycue.Titles.FitText"), lt("Laycue.Titles.FitManual")});
	fitSeg_->setAccessibleName(lt("Laycue.Titles.ShapeWidth"));
	connect(fitSeg_, &Segmented::changed, this, [this](int i) { changeShape(i, 0); });
	sv->addWidget(fitSeg_);
	widthRow_ = new QWidget(shapeBox_);
	auto *wv = new QHBoxLayout(widthRow_);
	wv->setContentsMargins(0, 0, 0, 0);
	wv->setSpacing(size::gapSmall);
	wv->addWidget(makeLabel(widthRow_, lt("Laycue.Titles.ShapeWidthManual"), 13, 600, color::cream), 1);
	auto *narrower = button(widthRow_, QStringLiteral("\u2212"), "laycueSecondary");
	narrower->setAccessibleName(lt("Laycue.Titles.Narrower"));
	connect(narrower, &QAbstractButton::clicked, this, [this]() { changeShape(-1, -1); });
	wv->addWidget(narrower);
	widthValue_ = makeLabel(widthRow_, QString(), 13, 600, color::cream);
	widthValue_->setAlignment(Qt::AlignCenter);
	widthValue_->setMinimumWidth(64);
	widthValue_->setWordWrap(false);
	wv->addWidget(widthValue_);
	auto *wider = button(widthRow_, QStringLiteral("+"), "laycueSecondary");
	wider->setAccessibleName(lt("Laycue.Titles.Wider"));
	connect(wider, &QAbstractButton::clicked, this, [this]() { changeShape(-1, +1); });
	wv->addWidget(wider);
	sv->addWidget(widthRow_);

	// How long it stays on screen before it leaves by itself.
	holdBox_ = new QWidget(stage);
	auto *hv = new QHBoxLayout(holdBox_);
	hv->setContentsMargins(0, 2, 0, 2);
	hv->setSpacing(size::gapSmall);
	hv->addWidget(makeLabel(holdBox_, lt("Laycue.Titles.StaysOnScreen"), 13, 600, color::cream), 1);
	auto *shorter = button(holdBox_, QStringLiteral("\u2212"), "laycueSecondary");
	shorter->setAccessibleName(lt("Laycue.Titles.Less"));
	connect(shorter, &QAbstractButton::clicked, this, [this]() { changeHold(-1); });
	hv->addWidget(shorter);
	holdValue_ = makeLabel(holdBox_, QString(), 13, 600, color::cream);
	holdValue_->setAlignment(Qt::AlignCenter);
	holdValue_->setMinimumWidth(96);
	holdValue_->setWordWrap(false);
	hv->addWidget(holdValue_);
	auto *longer = button(holdBox_, QStringLiteral("+"), "laycueSecondary");
	longer->setAccessibleName(lt("Laycue.Titles.More"));
	connect(longer, &QAbstractButton::clicked, this, [this]() { changeHold(+1); });
	hv->addWidget(longer);

	// Position, size and time on screen fold away into one row; its summary shows what they are set to.
	optionsRow_ = new RowButton(stage);
	optionsRow_->setTitle(lt("Laycue.Titles.Options"));
	connect(optionsRow_, &RowButton::clicked, this, [this]() {
		optionsOpen_ = !optionsOpen_;
		refreshOptions();
		scheduleSave();
	});
	v->addWidget(optionsRow_);
	optionsBox_ = new QWidget(stage);
	auto *ov = new QVBoxLayout(optionsBox_);
	ov->setContentsMargins(0, 0, 0, 0);
	ov->setSpacing(size::gapSmall);
	ov->addWidget(placementBox_);
	ov->addWidget(shapeBox_);
	ov->addWidget(holdBox_);
	v->addWidget(optionsBox_);

	auto *row = new QHBoxLayout();
	row->setSpacing(size::gapSmall);
	primary_ = new ActionButton(lt("Laycue.Titles.ShowOnAir"), ActionButton::Kind::Primary, stage);
	primary_->setMinimumHeight(size::controlHeight + 6);
	connect(primary_, &QAbstractButton::clicked, this, &TitlesDock::primaryClicked);
	row->addWidget(primary_, 3);
	hide_ = new ActionButton(lt("Laycue.Titles.Hide"), ActionButton::Kind::Secondary, stage);
	hide_->setMinimumHeight(size::controlHeight + 6);
	connect(hide_, &QAbstractButton::clicked, this, &TitlesDock::hideClicked);
	row->addWidget(hide_, 1);
	queueAdd_ = new ActionButton(lt("Laycue.Titles.QueueAdd"), ActionButton::Kind::Secondary, stage);
	queueAdd_->setMinimumHeight(size::controlHeight + 6);
	queueAdd_->setToolTip(lt("Laycue.Titles.QueueAddTip"));
	connect(queueAdd_, &QAbstractButton::clicked, this, &TitlesDock::addToQueueClicked);
	row->addWidget(queueAdd_, 1);
	v->addLayout(row);

	hint_ = makeLabel(stage, QString(), 11, 400, color::textTertiary);
	v->addWidget(hint_);
	return stage;
}

QWidget *TitlesDock::buildTabs()
{
	auto *surface = new Surface(1, home_);
	auto *v = new QVBoxLayout(surface);
	v->setSpacing(size::gapSmall);
	surface->setPadding(12, 10, 8, 8);

	tabs_ = new Segmented(surface);
	tabs_->setItems({lt("Laycue.Titles.TabTemplates"), lt("Laycue.Titles.TabQueue"), lt("Laycue.Titles.TabSetup")});
	tabs_->setAccessibleName(lt("Laycue.Titles.TabsAccessible"));
	connect(tabs_, &Segmented::changed, this, [this](int i) {
		showTab(i == 0 ? Tab::Templates : i == 1 ? Tab::Queue : Tab::Setup);
	});
	v->addWidget(tabs_);

	tabScroll_ = new QScrollArea(surface);
	tabScroll_->setWidgetResizable(true);
	tabScroll_->setFrameShape(QFrame::NoFrame);
	tabScroll_->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
	tabScroll_->setMinimumHeight(190); // the templates always get room; a short panel scrolls instead
	auto *host = new QWidget(tabScroll_);
	auto *hostLayout = new QVBoxLayout(host);
	hostLayout->setContentsMargins(0, 0, 4, 0);
	hostLayout->setSpacing(0);

	// Templates
	templatesTab_ = new QWidget(host);
	auto *t = new QVBoxLayout(templatesTab_);
	t->setContentsMargins(0, 0, 0, 0);
	t->setSpacing(size::gapSmall + 2);
	// Nine categories do not fit on a narrow panel, so they are one row that opens a menu.
	categoryRow_ = new RowButton(templatesTab_);
	categoryRow_->setTitle(lt("Laycue.Titles.CategoryRow"));
	connect(categoryRow_, &RowButton::clicked, this, &TitlesDock::pickCategory);
	t->addWidget(categoryRow_);
	refreshCategory();
	gridHost_ = new QWidget(templatesTab_);
	grid_ = new QGridLayout(gridHost_);
	grid_->setContentsMargins(0, 2, 0, 2);
	grid_->setHorizontalSpacing(size::gapSmall + 2);
	grid_->setVerticalSpacing(size::gapSmall + 2);
	grid_->setAlignment(Qt::AlignTop);
	t->addWidget(gridHost_);
	templateActions_ = new QWidget(templatesTab_);
	templateActionsLayout_ = new QVBoxLayout(templateActions_);
	templateActionsLayout_->setContentsMargins(0, 0, 0, 0);
	templateActionsLayout_->setSpacing(size::gapSmall);
	t->addWidget(templateActions_);
	t->addStretch(1);
	hostLayout->addWidget(templatesTab_);

	queueTab_ = new QWidget(host);
	queueLayout_ = new QVBoxLayout(queueTab_);
	queueLayout_->setContentsMargins(0, 0, 0, 0);
	queueLayout_->setSpacing(size::gapSmall);
	queueTab_->hide();
	hostLayout->addWidget(queueTab_);

	setupTab_ = new QWidget(host);
	setupLayout_ = new QVBoxLayout(setupTab_);
	setupLayout_->setContentsMargins(0, 0, 0, 0);
	setupLayout_->setSpacing(size::gap);
	setupTab_->hide();
	hostLayout->addWidget(setupTab_);
	hostLayout->addStretch(1);

	tabScroll_->setWidget(host);
	tabScroll_->viewport()->installEventFilter(this);
	v->addWidget(tabScroll_, 1);
	return surface;
}

void TitlesDock::showTab(Tab tab)
{
	tab_ = tab;
	templatesTab_->setVisible(tab == Tab::Templates);
	queueTab_->setVisible(tab == Tab::Queue);
	setupTab_->setVisible(tab == Tab::Setup);
	tabs_->setCurrent(tab == Tab::Templates ? 0 : tab == Tab::Queue ? 1 : 2);
	if (tab == Tab::Setup)
		rebuildSetup();
	if (tab == Tab::Queue)
		rebuildQueue();
	updateButtons();
}

// ---- Content -----------------------------------------------------------------------------------------------

TitleContent TitlesDock::currentContent() const
{
	TitleContent c;
	const TitleTemplate t = resolveStyle(library_, library_.selected);
	c.line1 = toStd(t.longText ? long1_->toPlainText() : edit1_->text());
	c.line2 = toStd(edit2_->text());
	c.tag = toStd(edit3_->text());
	c.note = toStd(t.usesNote && t.tickerSpeed > 0.0 ? long4_->toPlainText() : edit4_->text());
	c.logo = library_.draft.logo; // chosen with the button
	c.rows = library_.draft.rows; // edited in place
	return c;
}

TitleContent TitlesDock::sampleFor(const std::string &styleId) const
{
	std::string baseId;
	const TitleTemplate t = resolveStyle(library_, styleId, &baseId);
	TitleContent c;
	c.line1 = localised(sampleKey(t, 1));
	c.line2 = t.usesLine2 ? localised(sampleKey(t, 2)) : std::string();
	c.tag = t.usesTag ? localised(sampleKey(t, 3)) : std::string();
	if (t.usesNote)
		c.note = localised(sampleKey(t, 4));
	for (int r = 1; r <= t.maxRows; ++r) {
		const std::string base = nameKey(t) + ".R" + std::to_string(r);
		const std::string value = localised(base + "V");
		if (!value.empty() && value != base + "V")
			c.rows.push_back({localised(base + "L"), value});
	}
	return c;
}

void TitlesDock::refreshFields()
{
	buildingFields_ = true;
	std::string baseId;
	const TitleTemplate t = resolveStyle(library_, library_.selected, &baseId);

	// First start: show the template's sample text, so the preview is never blank.
	if (library_.draft.line1.empty() && library_.draft.line2.empty() && library_.draft.tag.empty()) {
		library_.draft = sampleFor(library_.selected);
		lastSampleStyle_ = library_.selected;
	}

	const auto label = [&](QLabel *l, int field, const char *fallbackKey) {
		const std::string k = fieldKey(t, field);
		const std::string text = localised(k);
		l->setText(text.empty() || text == k ? lt(fallbackKey) : fromStd(text));
	};
	label(label1_, 1, "Laycue.Titles.Line1");
	label(label2_, 2, "Laycue.Titles.Line2");
	label(label3_, 3, "Laycue.Titles.Tag");

	fieldRow1_->setVisible(t.usesLine1);
	fieldRow2_->setVisible(t.usesLine2);
	fieldRow3_->setVisible(t.usesTag);
	fieldRow4_->setVisible(t.usesNote);
	label1_->setVisible(t.usesLine1);
	edit1_->setVisible(!t.longText && t.usesLine1);
	long1_->setVisible(t.longText && t.usesLine1);
	label2_->setVisible(t.usesLine2);
	edit2_->setVisible(t.usesLine2);
	label3_->setVisible(t.usesTag);
	edit3_->setVisible(t.usesTag);
	label(label4_, 4, "Laycue.Titles.Note");
	label4_->setVisible(t.usesNote);
	const bool longNote = t.usesNote && t.tickerSpeed > 0.0; // a scrolling text: Enter starts a new item
	edit4_->setVisible(t.usesNote && !longNote);
	long4_->setVisible(longNote);
	if (longNote) {
		const QString note = fromStd(library_.draft.note);
		if (long4_->toPlainText() != note)
			long4_->setPlainText(note);
	}
	edit4_->setText(fromStd(library_.draft.note));
	edit4_->setPlaceholderText(fromStd(localised(sampleKey(t, 4))));
	logoBox_->setVisible(t.usesLogo);
	partsBox_->setVisible(t.usesLogoParts);
	refreshParts();
	logoName_->setText(library_.draft.logo.empty() ? lt("Laycue.Titles.NoLogo")
						       : QFileInfo(fromStd(library_.draft.logo)).fileName());
	rowsBox_->setVisible(t.maxRows > 0);
	if (t.maxRows > 0)
		rebuildRows();
	rebuildKept();

	const QString l1 = fromStd(library_.draft.line1);
	if (t.longText) {
		if (long1_->toPlainText() != l1)
			long1_->setPlainText(l1);
	} else if (edit1_->text() != l1) {
		edit1_->setText(l1);
	}
	edit2_->setText(fromStd(library_.draft.line2));
	edit3_->setText(fromStd(library_.draft.tag));
	edit1_->setPlaceholderText(fromStd(localised(sampleKey(t, 1))));
	edit2_->setPlaceholderText(fromStd(localised(sampleKey(t, 2))));
	edit3_->setPlaceholderText(fromStd(localised(sampleKey(t, 3))));
	buildingFields_ = false;
}

void TitlesDock::updatePreview()
{
	const TitleTemplate t = resolveStyle(library_, library_.selected);
	const TitleText text = toText(library_.draft);
	preview_->setTitle(t, text, library_.useBrand, library_.brand);
	const Placement placement = placementFor(library_, library_.selected);
	preview_->setPlacement(placement.align, placement.percent);
	preview_->setShape(placement.fit, placement.width);
	preview_->setSpeed(library_.speed);
	refreshPlacement();
	refreshHold();
}

void TitlesDock::fieldsEdited()
{
	if (buildingFields_)
		return;
	library_.draft = currentContent();
	updatePreview();
	updateButtons();
	scheduleSave();
}

void TitlesDock::selectStyle(const std::string &styleId)
{
	// Keep what the operator typed; only replace text that is still the previous template's sample.
	const TitleContent previousSample = sampleFor(lastSampleStyle_.empty() ? library_.selected : lastSampleStyle_);
	const bool untouched = library_.draft == previousSample ||
			       (library_.draft.line1.empty() && library_.draft.line2.empty());
	library_.selected = styleId;
	if (untouched) {
		const std::string keepLogo = library_.draft.logo; // the operator's logo carries over between templates
		library_.draft = sampleFor(styleId);
		library_.draft.logo = keepLogo;
		lastSampleStyle_ = styleId;
	}
	refreshFields();
	updatePreview();
	updateButtons();
	for (auto &entry : cards_)
		entry.second->setSelectedCard(entry.first == styleId);
	rebuildTemplates();
	scheduleSave();
}

// ---- The template gallery ----------------------------------------------------------------------------

QImage TitlesDock::thumbnailFor(const std::string &styleId)
{
	const auto found = thumbs_.find(styleId);
	if (found != thumbs_.end())
		return found->second;
	QImage image;
	if (const FontSet *fonts = sharedFonts()) {
		std::string baseId;
		const TitleTemplate t = resolveStyle(library_, styleId, &baseId);
		const TitleContent sample = sampleFor(baseId);
		RenderRequest req;
		req.tpl = t;
		req.text = toText(sample);
		req.frame = {1.0, 0.0, 300.0};
		req.canvasWidth = 480;
		req.canvasHeight = 270;
		image = stageBackground(480, 270);
		const RenderedTitle r = renderTitle(*fonts, req);
		if (!r.image.isNull()) {
			QPainter p(&image);
			p.drawImage(r.x, r.y, r.image);
		}
	}
	thumbs_[styleId] = image;
	return image;
}

void TitlesDock::rebuildTemplates()
{
	for (auto &entry : cards_)
		entry.second->deleteLater();
	cards_.clear();

	std::vector<std::pair<std::string, std::string>> styles; // id, name
	for (const TitleTemplate &t : builtInTemplates())
		styles.emplace_back(t.id, localised(nameKey(t)));
	for (const Overrides &o : library_.customs)
		styles.emplace_back(customId(o.name), o.name);

	for (const auto &style : styles) {
		auto *card = new TemplateCard(gridHost_);
		card->setName(fromStd(style.second));
		card->setCustom(isCustomId(style.first));
		card->setImage(thumbnailFor(style.first));
		card->setSelectedCard(style.first == library_.selected);
		const std::string id = style.first;
		connect(card, &QAbstractButton::clicked, this, [this, id]() { selectStyle(id); });
		cards_[id] = card;
	}
	layoutTemplates();

	// Under the grid: customise this template / delete a custom one.
	while (QLayoutItem *item = templateActionsLayout_->takeAt(0)) {
		if (item->widget())
			item->widget()->deleteLater();
		delete item;
	}
	auto *customize = button(templateActions_, lt("Laycue.Titles.Customize"), "laycueSecondary");
	connect(customize, &QAbstractButton::clicked, this, [this]() { openCustomize(false); });
	templateActionsLayout_->addWidget(customize);
	if (isCustomId(library_.selected)) {
		auto *edit = button(templateActions_, lt("Laycue.Titles.EditCustom"), "laycueSecondary");
		connect(edit, &QAbstractButton::clicked, this, [this]() { openCustomize(true); });
		templateActionsLayout_->addWidget(edit);
		auto *remove = button(templateActions_, lt("Laycue.Titles.DeleteCustom"), "laycueDanger");
		connect(remove, &QAbstractButton::clicked, this, &TitlesDock::deleteSelectedCustom);
		templateActionsLayout_->addWidget(remove);
	}
}

void TitlesDock::layoutTemplates()
{
	if (!grid_)
		return;
	while (grid_->count() > 0)
		delete grid_->takeAt(0); // removes the layout item only; the card stays

	const int available = tabScroll_->viewport()->width() - 4;
	const int columns = std::clamp(available / kCardMinWidth, 2, 5);
	const int columnWidth = std::max((available - grid_->horizontalSpacing() * (columns - 1)) / columns, 100);
	for (int c = 0; c < 6; ++c)
		grid_->setColumnStretch(c, c < columns ? 1 : 0);

	int index = 0;
	const auto place = [&](const std::string &id, bool visible) {
		const auto it = cards_.find(id);
		if (it == cards_.end())
			return;
		TemplateCard *card = it->second;
		card->setVisible(visible);
		if (!visible)
			return;
		card->setFixedHeight(card->heightForWidth(columnWidth));
		grid_->addWidget(card, index / columns, index % columns);
		++index;
	};
	for (const TitleTemplate &t : builtInTemplates()) {
		const bool visible = categoryFilter_ == 0 || int(t.category) + 1 == categoryFilter_;
		place(t.id, visible);
	}
	for (const Overrides &o : library_.customs) {
		const TitleTemplate *base = findTemplate(o.baseId);
		const bool visible = categoryFilter_ == 0 || (base && int(base->category) + 1 == categoryFilter_);
		place(customId(o.name), visible);
	}
}

bool TitlesDock::eventFilter(QObject *watched, QEvent *event)
{
	if (tabScroll_ && watched == tabScroll_->viewport() && event->type() == QEvent::Resize)
		layoutTemplates();
	return QWidget::eventFilter(watched, event);
}

void TitlesDock::resizeEvent(QResizeEvent *event)
{
	QWidget::resizeEvent(event);
	if (toast_ && toast_->isVisible()) {
		toast_->adjustSize();
		toast_->move((width() - toast_->width()) / 2, height() - toast_->height() - 18);
	}
}

// ---- Show, hide, update ------------------------------------------------------------------------------

bool TitlesDock::applyAndShow()
{
	if (target().empty() || !applyToSource(target(), library_, library_.selected, library_.draft)) {
		toast(lt("Laycue.Titles.NoLayerToast"));
		return false;
	}
	if (!showOnAir(target())) {
		toast(lt("Laycue.Titles.ShowFailed"));
		return false;
	}
	onAirContent_ = library_.draft;
	onAirStyle_ = library_.selected;
	onAirPlacement_ = placementFor(library_, library_.selected);
	nameLayerAfterContent(library_.draft);
	const TitleStatus st = statusOf(target());
	if (st.live()) {
		preview_->play();
		preview_->setOnAir(true);
	} else {
		toast(lt("Laycue.Titles.CuedToast")); // waiting in Preview: it plays when its scene goes live
	}
	pollStatus();
	return true;
}

void TitlesDock::primaryClicked()
{
	if (sources_.empty()) {
		addToThisScene();
		return;
	}
	if (status_.onAir() && status_.phase != TitleStatus::Phase::Out) {
		// On air already. Nothing changed: this button says "Hide". Something changed: it says "Update",
		// and only the text changes; nothing re-arrives.
		if (onAirContent_ == library_.draft && onAirStyle_ == library_.selected &&
		    onAirPlacement_ == placementFor(library_, library_.selected)) {
			hideClicked();
			return;
		}
		if (onAirStyle_ != library_.selected) {
			// A different design: the one on air leaves and the new one arrives, as a vision mixer would do it.
			hideFromAir(target());
			preview_->stop();
			pendingPlay_ = true;
			pendingTimer_.start();
			return;
		}
		applyToSource(target(), library_, library_.selected, library_.draft);
		onAirContent_ = library_.draft;
		onAirPlacement_ = placementFor(library_, library_.selected);
		nameLayerAfterContent(library_.draft);
		toast(lt("Laycue.Titles.Updated"));
		updateButtons();
		return;
	}
	applyAndShow();
}

void TitlesDock::hideClicked()
{
	if (hideFromAir(target())) {
		preview_->stop();
		pollStatus();
	}
}

void TitlesDock::addToQueueClicked()
{
	const TitleContent c = library_.draft;
	if (c.line1.empty() && c.line2.empty()) {
		toast(lt("Laycue.Titles.QueueNeedsText"));
		return;
	}
	const QueueItem item{library_.selected, c, target()};
	if (editingRow_ >= 0 && replaceInQueue(library_, size_t(editingRow_), item)) {
		editingRow_ = -1;
		toast(lt("Laycue.Titles.RowSaved"));
	} else {
		editingRow_ = -1;
		if (!enqueue(library_, item)) {
			toast(lt("Laycue.Titles.QueueFull"));
			return;
		}
		toast(lt("Laycue.Titles.QueuedToast").arg(int(library_.queue.size())));
	}
	rebuildQueue();
	updateButtons();
	scheduleSave();
}

void TitlesDock::playQueueItem(size_t index)
{
	if (index >= library_.queue.size())
		return;
	const QueueItem item = library_.queue[index];
	library_.queueCurrent = int(index); // the list keeps the title; the marker moves to it
	if (!item.layer.empty() && item.layer != target() &&
	    std::find(sources_.begin(), sources_.end(), item.layer) != sources_.end())
		switchLayer(item.layer); // a crawl row plays on the crawl layer
	library_.selected = item.styleId;
	library_.draft = item.content;
	lastSampleStyle_ = std::string();
	refreshFields();
	updatePreview();
	rebuildQueue();
	for (auto &entry : cards_)
		entry.second->setSelectedCard(entry.first == library_.selected);

	status_ = statusOf(target());
	if (status_.onAir()) {
		// The title on air leaves first, then the next one arrives.
		hideFromAir(target());
		preview_->stop();
		pendingPlay_ = true;
		pendingTimer_.start();
	} else {
		applyAndShow();
	}
	scheduleSave();
}

void TitlesDock::playNextClicked()
{
	if (library_.queue.empty()) {
		toast(lt("Laycue.Titles.QueueEmptyToast"));
		return;
	}
	const int next = nextQueueIndex(library_);
	if (next < 0) {
		toast(lt("Laycue.Titles.QueueEndToast"));
		return;
	}
	playQueueItem(size_t(next));
}

void TitlesDock::previousClicked()
{
	if (library_.queue.empty()) {
		toast(lt("Laycue.Titles.QueueEmptyToast"));
		return;
	}
	const int previous = previousQueueIndex(library_);
	if (previous < 0) {
		toast(lt("Laycue.Titles.QueueStartToast"));
		return;
	}
	playQueueItem(size_t(previous));
}

void TitlesDock::runHotkey(int index)
{
	switch (index) {
	case 0:
		if (status_.onAir() && status_.phase != TitleStatus::Phase::Out)
			hideClicked();
		else
			primaryClicked();
		break;
	case 1:
		playNextClicked();
		break;
	case 3:
		previousClicked();
		break;
	case 2:
		for (const std::string &name : sources_)
			hideFromAir(name);
		preview_->stop();
		pollStatus();
		break;
	default:
		break;
	}
}

// ---- Status ------------------------------------------------------------------------------------------------

void TitlesDock::refreshSources()
{
	sources_ = listTitleSources();
	if (std::find(sources_.begin(), sources_.end(), library_.targetSource) == sources_.end())
		library_.targetSource = sources_.empty() ? std::string() : sources_.front();
}

void TitlesDock::pollStatus()
{
	if (shuttingDown_)
		return;
	const std::vector<std::string> before = sources_;
	refreshSources();
	if (before != sources_) {
		if (tab_ == Tab::Setup)
			rebuildSetup();
	}
	status_ = statusOf(target());
	if (status_.onAir() && onAirStyle_.empty()) {
		// A title is on air that this panel didn't show (a saved scene, a hotkey): treat the text now in the boxes as it.
		onAirContent_ = library_.draft;
		onAirStyle_ = library_.selected;
		onAirPlacement_ = placementFor(library_, library_.selected);
	}
	if (!status_.onAir()) {
		onAirStyle_.clear();
		if (preview_->onAir())
			preview_->stop();
	}
	refreshLayerRow();
	if (status_.onAir() != lastQueueOnAir_) {
		lastQueueOnAir_ = status_.onAir();
		if (!library_.queue.empty())
			rebuildQueue(); // the ON AIR mark moves with it
	}
	preview_->setOnAir(status_.live());
	if (status_.live() && !prevLive_)
		preview_->play(); // the scene just went live: the panel's picture arrives with it
	prevLive_ = status_.live();
	updateStatus();
	updateButtons();
}

void TitlesDock::updateStatus()
{
	if (sceneLabel_) {
		bool live = false;
		sceneLabel_->setText(sources_.empty() ? QString() : presenceText(presenceOf(target()), live));
		sceneLabel_->setStyleSheet(labelStyle(live ? color::next : color::textTertiary, sceneLabel_->font()));
	}
	static_cast<StatusDot *>(statusDot_)->setOn(status_.live());
	if (!status_.found) {
		statusLabel_->setText(sources_.empty() ? lt("Laycue.Titles.StatusNoLayer")
						       : lt("Laycue.Titles.StatusHidden"));
		statusLabel_->setStyleSheet(labelStyle(color::textTertiary, statusLabel_->font()));
		return;
	}
	if (status_.phase == TitleStatus::Phase::Cued) {
		statusLabel_->setText(lt("Laycue.Titles.StatusCued"));
		statusLabel_->setStyleSheet(labelStyle(color::next, statusLabel_->font()));
		return;
	}
	if (status_.onAir()) {
		QString text = lt("Laycue.Titles.StatusOnAir");
		if (status_.holdRemaining >= 0.0 && status_.phase == TitleStatus::Phase::Hold)
			text += lt("Laycue.Titles.StatusHidesIn").arg(int(status_.holdRemaining + 0.99));
		statusLabel_->setText(text);
		statusLabel_->setStyleSheet(labelStyle(color::live, statusLabel_->font()));
	} else {
		statusLabel_->setText(lt("Laycue.Titles.StatusHidden"));
		statusLabel_->setStyleSheet(labelStyle(color::textTertiary, statusLabel_->font()));
	}
}

void TitlesDock::updateButtons()
{
	const bool noLayer = sources_.empty();
	const bool onAir = status_.onAir() && status_.phase != TitleStatus::Phase::Out;
	const bool changed = onAir && !(onAirContent_ == library_.draft && onAirStyle_ == library_.selected &&
					onAirPlacement_ == placementFor(library_, library_.selected));

	if (noLayer) {
		primary_->setText(lt("Laycue.Titles.AddLayerToScene"));
		primary_->setKind(ActionButton::Kind::Primary);
		hint_->setText(lt("Laycue.Titles.AddLayerHint"));
	} else if (onAir) {
		const bool cued = status_.phase == TitleStatus::Phase::Cued;
		primary_->setText(changed ? lt("Laycue.Titles.UpdateOnAir")
					  : lt(cued ? "Laycue.Titles.CancelCue" : "Laycue.Titles.HideTitle"));
		primary_->setKind(changed ? ActionButton::Kind::Primary : ActionButton::Kind::Secondary);
		primary_->setEnabled(true);
		hint_->setText(changed                                     ? lt("Laycue.Titles.UpdateHint")
			       : status_.phase == TitleStatus::Phase::Cued ? lt("Laycue.Titles.CuedHint")
									   : QString());
	} else {
		primary_->setText(lt("Laycue.Titles.ShowOnAir"));
		primary_->setKind(ActionButton::Kind::Primary);
		primary_->setEnabled(true);
		hint_->setText(QString());
	}
	// On the Queue tab, "Play next" is the main action, so the stage's button steps back.
	if (tab_ == Tab::Queue && !library_.queue.empty() && !noLayer && !changed)
		primary_->setKind(ActionButton::Kind::Secondary);
	hide_->setVisible(onAir && changed); // when nothing changed, the main button already says Hide
	queueAdd_->setText(editingRow_ >= 0 ? lt("Laycue.Titles.QueueSaveRow") : lt("Laycue.Titles.QueueAdd"));
	queueAdd_->setVisible(!noLayer);
	hint_->setVisible(!hint_->text().isEmpty());
}

// ---- Queue -------------------------------------------------------------------------------------------------

void TitlesDock::rebuildQueue()
{
	page::clear(queueLayout_);

	auto *serviceRow = new RowButton(queueTab_);
	serviceRow->setTitle(lt("Laycue.Titles.ServiceRow"));
	serviceRow->setValue(library_.serviceName.empty() ? lt("Laycue.Titles.ServiceUnsaved")
							  : fromStd(library_.serviceName));
	serviceRow->setOpen(true);
	connect(serviceRow, &RowButton::clicked, this, [this, serviceRow]() { serviceMenu(serviceRow); });
	queueLayout_->addWidget(serviceRow);

	if (library_.queue.empty()) {
		auto *empty = new Surface(1, queueTab_);
		auto *v = new QVBoxLayout(empty);
		v->setSpacing(size::gapSmall);
		empty->setPadding(14, 12, 14, 12);
		v->addWidget(makeLabel(empty, lt("Laycue.Titles.QueueEmptyTitle"), 14, 700, color::cream));
		v->addWidget(makeLabel(empty, lt("Laycue.Titles.QueueEmptyBody"), 12, 400, color::textSecondary));
		queueLayout_->addWidget(empty);
		queueLayout_->addStretch(1);
		return;
	}

	auto *buttons = new QHBoxLayout();
	buttons->setSpacing(size::gapSmall);
	auto *previous = button(queueTab_, lt("Laycue.Titles.Previous"), "laycueSecondary");
	previous->setMinimumHeight(size::touch);
	previous->setEnabled(previousQueueIndex(library_) >= 0);
	connect(previous, &QAbstractButton::clicked, this, &TitlesDock::previousClicked);
	buttons->addWidget(previous, 1);
	auto *playNext = button(queueTab_, lt("Laycue.Titles.PlayNext"), "laycuePrimary");
	playNext->setMinimumHeight(size::touch);
	playNext->setEnabled(nextQueueIndex(library_) >= 0);
	connect(playNext, &QAbstractButton::clicked, this, &TitlesDock::playNextClicked);
	buttons->addWidget(playNext, 2);
	queueLayout_->addLayout(buttons);

	// Saving the list is in plain sight, not hidden in a menu: next week, load it and play.
	auto *save = button(queueTab_,
			    library_.serviceName.empty()
				    ? lt("Laycue.Titles.ServiceSaveThis")
				    : lt("Laycue.Titles.ServiceSaveHere").arg(fromStd(library_.serviceName)),
			    "laycueSecondary");
	connect(save, &QAbstractButton::clicked, this, &TitlesDock::saveServiceClicked);
	queueLayout_->addWidget(save);

	const int next = nextQueueIndex(library_);
	for (size_t i = 0; i < library_.queue.size(); ++i) {
		const QueueItem &item = library_.queue[i];
		const bool onAirRow = int(i) == library_.queueCurrent && status_.onAir();
		const bool played = int(i) <= library_.queueCurrent;
		auto *card = new Surface(1, queueTab_);
		auto *h = new QHBoxLayout(card);
		h->setSpacing(size::gapSmall);
		card->setPadding(12, 8, 10, 8);
		auto *textColumn = new QVBoxLayout();
		textColumn->setSpacing(1);
		if (onAirRow)
			textColumn->addWidget(makeLabel(card, lt("Laycue.Titles.RowOnAir"), 10, 700, color::live));
		else if (int(i) == next)
			textColumn->addWidget(makeLabel(card, lt("Laycue.Titles.RowNext"), 10, 700, color::next));
		QString first = fromStd(item.content.line1).simplified();
		if (first.size() > 60)
			first = first.left(58) + QStringLiteral("…");
		textColumn->addWidget(makeLabel(card, first.isEmpty() ? lt("Laycue.Titles.Untitled") : first, 13, 600,
						played && !onAirRow ? color::textTertiary : color::cream));
		std::string baseId;
		const TitleTemplate t = resolveStyle(library_, item.styleId, &baseId);
		QString detail = isCustomId(item.styleId) ? fromStd(customNameOf(item.styleId))
							  : fromStd(localised(nameKey(t)));
		if (!item.content.line2.empty())
			detail += QStringLiteral(" · ") + fromStd(item.content.line2).simplified().left(40);
		if (!item.layer.empty() && sources_.size() > 1)
			detail += QStringLiteral(" · ") + fromStd(item.layer);
		textColumn->addWidget(makeLabel(card, detail, 11, 400, color::textTertiary));
		h->addLayout(textColumn, 1);
		auto *play = button(card, lt("Laycue.Titles.PlayNow"), "laycueSecondary");
		const size_t index = i;
		connect(play, &QAbstractButton::clicked, this, [this, index]() { playQueueItem(index); });
		h->addWidget(play);
		auto *more = button(card, QStringLiteral("⋯"), "laycueLink");
		more->setAccessibleName(lt("Laycue.Titles.RowMore"));
		connect(more, &QAbstractButton::clicked, this, [this, index, more]() { rowMenu(index, more); });
		h->addWidget(more);
		queueLayout_->addWidget(card);
	}
	queueLayout_->addStretch(1);
}

void TitlesDock::rowMenu(size_t index, QWidget *anchor)
{
	QMenu menu(this);
	QAction *edit = menu.addAction(lt("Laycue.Titles.RowEdit"));
	QAction *up = menu.addAction(lt("Laycue.Titles.RowUp"));
	up->setEnabled(index > 0);
	QAction *down = menu.addAction(lt("Laycue.Titles.RowDown"));
	down->setEnabled(index + 1 < library_.queue.size());
	menu.addSeparator();
	QAction *remove = menu.addAction(lt("Laycue.Titles.RowRemove"));
	QAction *chosen = menu.exec(anchor->mapToGlobal(QPoint(0, anchor->height() + 2)));
	if (!chosen)
		return;
	if (chosen == edit) {
		editRow(index);
		return;
	}
	editingRow_ = -1;
	if (chosen == up)
		moveInQueue(library_, index, index - 1);
	else if (chosen == down)
		moveInQueue(library_, index, index + 1);
	else if (chosen == remove)
		removeFromQueue(library_, index);
	rebuildQueue();
	updateButtons();
	scheduleSave();
}

// Puts a row's template and text in the boxes. "Save row" then writes the changes back to the same row.
void TitlesDock::editRow(size_t index)
{
	if (index >= library_.queue.size())
		return;
	const QueueItem item = library_.queue[index];
	if (!item.layer.empty() && item.layer != target() &&
	    std::find(sources_.begin(), sources_.end(), item.layer) != sources_.end())
		switchLayer(item.layer);
	library_.selected = item.styleId;
	library_.draft = item.content;
	lastSampleStyle_ = std::string();
	editingRow_ = int(index);
	refreshFields();
	updatePreview();
	for (auto &entry : cards_)
		entry.second->setSelectedCard(entry.first == library_.selected);
	updateButtons();
	toast(lt("Laycue.Titles.RowEditing"));
	scheduleSave();
}

// Saves the list: under its current name, or (a new list) after asking for a name.
void TitlesDock::saveServiceClicked()
{
	std::string name = library_.serviceName;
	if (name.empty()) {
		bool ok = false;
		const QString typed = QInputDialog::getText(this, lt("Laycue.Titles.ServiceSaveThis"),
							    lt("Laycue.Titles.ServiceNamePrompt"), QLineEdit::Normal,
							    QString(), &ok);
		if (!ok)
			return;
		name = typed.trimmed().toStdString();
		if (library_.services.count(name) &&
		    QMessageBox::question(this, lt("Laycue.Titles.ServiceSaveThis"),
					  lt("Laycue.Titles.ServiceReplaceAsk").arg(fromStd(name))) != QMessageBox::Yes)
			return;
	}
	if (saveService(library_, name))
		toast(lt("Laycue.Titles.ServiceSavedToast").arg(fromStd(library_.serviceName)));
	else
		toast(lt("Laycue.Titles.ServiceSaveFailed"));
	rebuildQueue();
	scheduleSave();
}

// Saved run orders: keep "Sunday 9am", load it again next week.
void TitlesDock::serviceMenu(QWidget *anchor)
{
	QMenu menu(this);
	QAction *saveAs = menu.addAction(lt("Laycue.Titles.ServiceSaveAs"));
	saveAs->setEnabled(!library_.queue.empty());
	QAction *saveHere = nullptr;
	if (!library_.serviceName.empty()) {
		saveHere = menu.addAction(lt("Laycue.Titles.ServiceSaveHere").arg(fromStd(library_.serviceName)));
		saveHere->setEnabled(!library_.queue.empty());
	}
	QMenu *load = menu.addMenu(lt("Laycue.Titles.ServiceLoad"));
	load->setEnabled(!library_.services.empty());
	for (const auto &kv : library_.services) {
		const std::string name = kv.first;
		QAction *a = load->addAction(fromStd(name));
		a->setCheckable(true);
		a->setChecked(name == library_.serviceName);
		connect(a, &QAction::triggered, this, [this, name]() {
			const auto saved = library_.services.find(name);
			bool same = !library_.queue.empty() && name == library_.serviceName &&
				    saved != library_.services.end() && saved->second.size() == library_.queue.size();
			for (size_t i = 0; same && i < library_.queue.size(); ++i) {
				const QueueItem &a2 = library_.queue[i];
				const QueueItem &b2 = saved->second[i];
				same = a2.styleId == b2.styleId && a2.content == b2.content && a2.layer == b2.layer;
			}
			if (!library_.queue.empty() && !same &&
			    QMessageBox::question(this, lt("Laycue.Titles.ServiceLoad"),
						  lt("Laycue.Titles.ServiceLoadAsk").arg(fromStd(name))) !=
				    QMessageBox::Yes)
				return;
			loadService(library_, name);
			editingRow_ = -1;
			rebuildQueue();
			updateButtons();
			scheduleSave();
		});
	}
	QMenu *del = menu.addMenu(lt("Laycue.Titles.ServiceDelete"));
	del->setEnabled(!library_.services.empty());
	for (const auto &kv : library_.services) {
		const std::string name = kv.first;
		connect(del->addAction(fromStd(name)), &QAction::triggered, this, [this, name]() {
			if (QMessageBox::question(this, lt("Laycue.Titles.ServiceDelete"),
						  lt("Laycue.Titles.ServiceDeleteAsk").arg(fromStd(name))) !=
			    QMessageBox::Yes)
				return;
			deleteService(library_, name);
			rebuildQueue();
			scheduleSave();
		});
	}
	menu.addSeparator();
	QAction *top = menu.addAction(lt("Laycue.Titles.ServiceTop"));
	top->setEnabled(library_.queueCurrent >= 0);
	QAction *clearAll = menu.addAction(lt("Laycue.Titles.ServiceClear"));
	clearAll->setEnabled(!library_.queue.empty());

	QAction *chosen = menu.exec(anchor->mapToGlobal(QPoint(0, anchor->height() + 2)));
	if (!chosen)
		return;
	if (chosen == saveAs || chosen == saveHere) {
		std::string name = library_.serviceName;
		if (chosen == saveAs) {
			bool ok = false;
			const QString typed = QInputDialog::getText(this, lt("Laycue.Titles.ServiceSaveAs"),
								    lt("Laycue.Titles.ServiceNamePrompt"),
								    QLineEdit::Normal, fromStd(library_.serviceName),
								    &ok);
			if (!ok)
				return;
			name = typed.trimmed().toStdString();
			if (library_.services.count(name) &&
			    QMessageBox::question(this, lt("Laycue.Titles.ServiceSaveAs"),
						  lt("Laycue.Titles.ServiceReplaceAsk").arg(fromStd(name))) !=
				    QMessageBox::Yes)
				return;
		}
		if (saveService(library_, name))
			toast(lt("Laycue.Titles.ServiceSavedToast").arg(fromStd(library_.serviceName)));
		else
			toast(lt("Laycue.Titles.ServiceSaveFailed"));
	} else if (chosen == top) {
		library_.queueCurrent = -1;
	} else if (chosen == clearAll) {
		if (QMessageBox::question(this, lt("Laycue.Titles.ServiceClear"),
					  lt("Laycue.Titles.ServiceClearAsk")) != QMessageBox::Yes)
			return;
		library_.queue.clear();
		library_.queueCurrent = -1;
		library_.serviceName.clear();
		editingRow_ = -1;
	}
	rebuildQueue();
	updateButtons();
	scheduleSave();
}

// ---- Setup -------------------------------------------------------------------------------------------------

void TitlesDock::rebuildSetup()
{
	page::clear(setupLayout_);

	const auto group = [&](const QString &title) -> QVBoxLayout * {
		setupLayout_->addWidget(makeLabel(setupTab_, title.toUpper(), 11, 700, color::textTertiary));
		auto *card = new Surface(1, setupTab_);
		auto *v = new QVBoxLayout(card);
		v->setSpacing(size::gapSmall);
		card->setPadding(14, 10, 14, 10);
		setupLayout_->addWidget(card);
		return v;
	};

	// The title layer.
	{
		QVBoxLayout *v = group(lt("Laycue.Titles.SetupLayer"));
		const QString current = sources_.empty() ? lt("Laycue.Titles.SetupNoLayer") : fromStd(target());
		v->addWidget(makeLabel(v->parentWidget(), current, 14, 600, color::cream));
		v->addWidget(
			makeLabel(v->parentWidget(), lt("Laycue.Titles.SetupLayerBody"), 11, 400, color::textTertiary));
		auto *row = new QHBoxLayout();
		row->setSpacing(size::gapSmall);
		auto *addHere = button(v->parentWidget(), lt("Laycue.Titles.AddToScene"), "laycueSecondary");
		connect(addHere, &QAbstractButton::clicked, this, &TitlesDock::addToThisScene);
		row->addWidget(addHere);
		auto *addAll = button(v->parentWidget(), lt("Laycue.Titles.AddToAll"), "laycueSecondary");
		addAll->setEnabled(!sources_.empty());
		connect(addAll, &QAbstractButton::clicked, this, &TitlesDock::addToAllScenesClicked);
		row->addWidget(addAll);
		v->addLayout(row);
		auto *newLayer = button(v->parentWidget(), lt("Laycue.Titles.NewLayer"), "laycueSecondary");
		connect(newLayer, &QAbstractButton::clicked, this, &TitlesDock::newLayerClicked);
		v->addWidget(newLayer);
		if (sources_.size() > 1) {
			auto *choose = button(v->parentWidget(), lt("Laycue.Titles.ChooseLayer"), "laycueLink");
			connect(choose, &QAbstractButton::clicked, this, [this, choose]() {
				QMenu menu(this);
				for (const std::string &name : sources_) {
					QAction *a = menu.addAction(fromStd(name));
					a->setCheckable(true);
					a->setChecked(name == target());
					connect(a, &QAction::triggered, this, [this, name]() { switchLayer(name); });
				}
				menu.exec(choose->mapToGlobal(QPoint(0, choose->height())));
			});
			v->addWidget(choose, 0, Qt::AlignLeft);
		}
	}

	// Brand colour.
	{
		QVBoxLayout *v = group(lt("Laycue.Titles.SetupLook"));
		auto *row = new QHBoxLayout();
		row->setSpacing(size::gap);
		auto *text = new QVBoxLayout();
		text->setSpacing(1);
		text->addWidget(
			makeLabel(v->parentWidget(), lt("Laycue.Titles.UseBrandColour"), 13, 500, color::cream));
		text->addWidget(makeLabel(v->parentWidget(), lt("Laycue.Titles.UseBrandColourSub"), 11, 400,
					  color::textTertiary));
		row->addLayout(text, 1);
		auto *sw = new Switch(v->parentWidget());
		sw->setAccessibleName(lt("Laycue.Titles.UseBrandColour"));
		sw->setOn(library_.useBrand);
		connect(sw, &QAbstractButton::toggled, this, [this](bool on) {
			library_.useBrand = on;
			thumbs_.clear();
			updatePreview();
			rebuildSetup();
			scheduleSave();
		});
		row->addWidget(sw, 0, Qt::AlignVCenter);
		v->addLayout(row);
		if (library_.useBrand) {
			auto *pick = button(
				v->parentWidget(),
				lt("Laycue.Titles.PickColour").arg(fromStd(colourToString(library_.brand).substr(0, 7))),
				"laycueSecondary");
			connect(pick, &QAbstractButton::clicked, this, &TitlesDock::pickBrandColour);
			v->addWidget(pick, 0, Qt::AlignLeft);
		}
	}

	// Timing.
	{
		QVBoxLayout *v = group(lt("Laycue.Titles.SetupTiming"));
		auto *row = new QHBoxLayout();
		row->setSpacing(size::gapSmall);
		auto *text = new QVBoxLayout();
		text->setSpacing(1);
		text->addWidget(makeLabel(v->parentWidget(), lt("Laycue.Titles.HideByItself"), 13, 500, color::cream));
		text->addWidget(makeLabel(v->parentWidget(), lt("Laycue.Titles.HideByItselfSub"), 11, 400,
					  color::textTertiary));
		row->addLayout(text, 1);
		auto *minus = button(v->parentWidget(), QStringLiteral("−"), "laycueSecondary");
		minus->setAccessibleName(lt("Laycue.Titles.Less"));
		connect(minus, &QAbstractButton::clicked, this, [this]() { changeHold(-1); });
		row->addWidget(minus);
		const int hold = library_.holdOverride;
		auto *value = makeLabel(v->parentWidget(),
					hold < 0    ? lt("Laycue.Titles.HoldTemplate")
					: hold == 0 ? lt("Laycue.Titles.HoldNever")
						    : lt("Laycue.Titles.HoldSeconds").arg(hold),
					13, 600, color::cream);
		value->setAlignment(Qt::AlignCenter);
		value->setMinimumWidth(78);
		row->addWidget(value);
		auto *plus = button(v->parentWidget(), QStringLiteral("+"), "laycueSecondary");
		plus->setAccessibleName(lt("Laycue.Titles.More"));
		connect(plus, &QAbstractButton::clicked, this, [this]() { changeHold(+1); });
		row->addWidget(plus);
		v->addLayout(row);

		// How fast titles arrive and leave: a few steps, not a free number.
		auto *speedRow = new QHBoxLayout();
		speedRow->setSpacing(size::gapSmall);
		auto *speedText = new QVBoxLayout();
		speedText->setSpacing(1);
		speedText->addWidget(makeLabel(v->parentWidget(), lt("Laycue.Titles.Speed"), 13, 500, color::cream));
		speedText->addWidget(
			makeLabel(v->parentWidget(), lt("Laycue.Titles.SpeedSub"), 11, 400, color::textTertiary));
		speedRow->addLayout(speedText, 1);
		auto *slower = button(v->parentWidget(), QStringLiteral("\u2212"), "laycueSecondary");
		slower->setAccessibleName(lt("Laycue.Titles.SpeedSlower"));
		connect(slower, &QAbstractButton::clicked, this, [this]() { changeSpeed(-1); });
		speedRow->addWidget(slower);
		auto *speedValue = makeLabel(v->parentWidget(),
					     lt(("Laycue.Titles.Speed" + std::to_string(library_.speed)).c_str()), 13,
					     600, color::cream);
		speedValue->setAlignment(Qt::AlignCenter);
		speedValue->setMinimumWidth(78);
		speedRow->addWidget(speedValue);
		auto *faster = button(v->parentWidget(), QStringLiteral("+"), "laycueSecondary");
		faster->setAccessibleName(lt("Laycue.Titles.SpeedFaster"));
		connect(faster, &QAbstractButton::clicked, this, [this]() { changeSpeed(+1); });
		speedRow->addWidget(faster);
		v->addLayout(speedRow);
	}

	// Updates.
	setupLayout_->addWidget(
		makeLabel(setupTab_, lt("Laycue.Update.Heading").toUpper(), 11, 700, color::textTertiary));
	setupLayout_->addWidget(new laycue::ui::UpdateCard(updater_, setupTab_));

	// Hotkeys.
	{
		QVBoxLayout *v = group(lt("Laycue.Titles.SetupKeys"));
		v->addWidget(
			makeLabel(v->parentWidget(), lt("Laycue.Titles.SetupKeysBody"), 12, 400, color::textSecondary));
	}
	setupLayout_->addStretch(1);
}

void TitlesDock::refreshPlacement()
{
	const TitleTemplate t = resolveStyle(library_, library_.selected);
	const bool can = supportsPlacement(t);
	placementBox_->setVisible(can);
	if (can) {
		const Placement p = placementFor(library_, library_.selected);
		alignSeg_->setCurrent(p.align);
		sizeValue_->setText(QStringLiteral("%1%").arg(p.percent));
	}
	refreshShape();
	refreshOptions();
}

void TitlesDock::refreshShape()
{
	const TitleTemplate t = resolveStyle(library_, library_.selected);
	const bool can = supportsFit(t);
	shapeBox_->setVisible(can);
	if (!can)
		return;
	const Placement p = placementFor(library_, library_.selected);
	fitSeg_->setCurrent(p.fit);
	widthRow_->setVisible(p.fit == 2);
	widthValue_->setText(lt("Laycue.Titles.WidthPx").arg(p.width));
}

void TitlesDock::changeShape(int fit, int widthDelta)
{
	Placement p = placementFor(library_, library_.selected);
	if (fit >= 0)
		p.fit = fit;
	p.width += 50 * widthDelta;
	setPlacement(library_, library_.selected, p);
	refreshPlacement();
	updatePreview();
	updateButtons();
	scheduleSave();
}

void TitlesDock::changePlacement(int align, int percentDelta)
{
	Placement p = placementFor(library_, library_.selected);
	if (align >= 0)
		p.align = align;
	p.percent += 10 * percentDelta;
	setPlacement(library_, library_.selected, p);
	refreshPlacement();
	updatePreview();
	updateButtons();
	scheduleSave();
}

void TitlesDock::refreshHold()
{
	const TitleTemplate t = resolveStyle(library_, library_.selected);
	const int hold = library_.holdOverride;
	QString text;
	if (hold == 0 || (hold < 0 && t.holdSeconds <= 0.0))
		text = lt("Laycue.Titles.HoldNever");
	else if (hold < 0)
		text = lt("Laycue.Titles.HoldDefault").arg(int(t.holdSeconds + 0.5));
	else
		text = lt("Laycue.Titles.HoldSeconds").arg(hold);
	holdValue_->setText(text);
	refreshOptions();
}

void TitlesDock::draftChanged()
{
	updatePreview();
	updateButtons();
	scheduleSave();
}

void TitlesDock::rebuildRows()
{
	page::clear(rowsLayout_);
	pointsEdit_ = nullptr;
	const TitleTemplate t = resolveStyle(library_, library_.selected);
	if (t.category == Category::Notes) {
		auto *mode = new Segmented(rowsBox_);
		mode->setItems({lt("Laycue.Titles.PointsOne"), lt("Laycue.Titles.PointsStack")});
		mode->setCurrent(library_.pointsOneAtATime ? 0 : 1);
		connect(mode, &Segmented::changed, this, [this](int index) {
			library_.pointsOneAtATime = index == 0;
			rebuildRows();
			scheduleSave();
		});
		rowsLayout_->addWidget(mode);
		if (library_.pointsOneAtATime) {
			rebuildPointsBank(t);
			return;
		}
	}
	// A template can word the rows itself ("POINTS", "Marker", "The point"); otherwise they are accounts.
	const auto own = [&](const char *suffix, const char *fallback) {
		const std::string key = nameKey(t) + "." + suffix;
		const std::string text = localised(key);
		return text.empty() || text == key ? lt(fallback) : fromStd(text);
	};
	rowsLayout_->addWidget(makeLabel(rowsBox_, own("RT", "Laycue.Titles.Accounts"), 11, 600, color::textTertiary));
	const QString labelHint = own("RL", "Laycue.Titles.AccountLabel");
	const QString valueHint = own("RV", "Laycue.Titles.AccountNumber");
	auto &rows = library_.draft.rows;
	for (size_t i = 0; i < rows.size(); ++i) {
		auto *line = new QWidget(rowsBox_);
		auto *h = new QHBoxLayout(line);
		h->setContentsMargins(0, 0, 0, 0);
		h->setSpacing(size::gapSmall);
		auto *label = new QLineEdit(line);
		label->setText(fromStd(rows[i].label));
		label->setPlaceholderText(labelHint);
		label->setAccessibleName(labelHint);
		connect(label, &QLineEdit::textEdited, this, [this, i](const QString &text) {
			if (i < library_.draft.rows.size()) {
				library_.draft.rows[i].label = toStd(text);
				draftChanged();
			}
		});
		h->addWidget(label, 2);
		auto *value = new QLineEdit(line);
		value->setText(fromStd(rows[i].value));
		value->setPlaceholderText(valueHint);
		value->setAccessibleName(valueHint);
		connect(value, &QLineEdit::textEdited, this, [this, i](const QString &text) {
			if (i < library_.draft.rows.size()) {
				library_.draft.rows[i].value = toStd(text);
				draftChanged();
			}
		});
		h->addWidget(value, 3);
		auto *remove = button(line, QStringLiteral("\u2715"), "laycueSecondary");
		remove->setAccessibleName(lt("Laycue.Titles.RemoveAccount"));
		connect(remove, &QAbstractButton::clicked, this, [this, i]() {
			if (i < library_.draft.rows.size()) {
				library_.draft.rows.erase(library_.draft.rows.begin() + std::ptrdiff_t(i));
				rebuildRows();
				draftChanged();
			}
		});
		h->addWidget(remove);
		rowsLayout_->addWidget(line);
	}
	if (rows.size() < size_t(t.maxRows)) {
		auto *add = button(rowsBox_, own("RA", "Laycue.Titles.AddAccount"), "laycueSecondary");
		connect(add, &QAbstractButton::clicked, this, [this]() {
			library_.draft.rows.push_back({});
			rebuildRows();
			draftChanged();
		});
		rowsLayout_->addWidget(add, 0, Qt::AlignLeft);
	}
}

void TitlesDock::rebuildPointsBank(const TitleTemplate &)
{
	rowsLayout_->addWidget(makeLabel(rowsBox_, lt("Laycue.Titles.PointsBank").arg(int(library_.points.size())), 11,
					 600, color::textTertiary));
	auto *addLine = new QWidget(rowsBox_);
	auto *addLayout = new QHBoxLayout(addLine);
	addLayout->setContentsMargins(0, 0, 0, 0);
	addLayout->setSpacing(size::gapSmall);
	auto *edit = new QLineEdit(addLine);
	edit->setPlaceholderText(lt("Laycue.Titles.PointsHint"));
	edit->setAccessibleName(lt("Laycue.Titles.PointsHint"));
	pointsEdit_ = edit;
	auto *keep = button(addLine, lt("Laycue.Titles.PointKeep"), "laycueSecondary");
	const auto keepPoint = [this, edit]() {
		const QString text = edit->text().trimmed();
		if (text.isEmpty() || library_.points.size() >= kMaxPoints)
			return;
		library_.points.push_back(toStd(text));
		rebuildRows(); // the box is rebuilt, so the new point appears and the field is empty again
		if (pointsEdit_)
			pointsEdit_->setFocus(); // so the next point can be typed straight away
		scheduleSave();
	};
	connect(edit, &QLineEdit::returnPressed, this, keepPoint);
	connect(keep, &QAbstractButton::clicked, this, keepPoint);
	addLayout->addWidget(edit, 1);
	addLayout->addWidget(keep);
	rowsLayout_->addWidget(addLine);

	if (library_.points.empty()) {
		auto *hint = makeLabel(rowsBox_, lt("Laycue.Titles.PointsEmpty"), 11, 400, color::textTertiary);
		hint->setWordWrap(true);
		rowsLayout_->addWidget(hint);
		return;
	}
	for (size_t i = 0; i < library_.points.size(); ++i) {
		auto *line = new QWidget(rowsBox_);
		auto *h = new QHBoxLayout(line);
		h->setContentsMargins(0, 0, 0, 0);
		h->setSpacing(size::gapSmall);
		auto *show = new RowButton(line);
		const bool shown = int(i) == pointShown_;
		show->setTitle(QStringLiteral("%1  %2").arg(int(i) + 1).arg(fromStd(library_.points[i])));
		show->setValue(shown ? lt("Laycue.Titles.PointShown") : lt("Laycue.Titles.PointShow"));
		connect(show, &RowButton::clicked, this, [this, i]() { playPoint(i); });
		h->addWidget(show, 1);
		auto *remove = button(line, QStringLiteral("\u2715"), "laycueSecondary");
		remove->setAccessibleName(lt("Laycue.Titles.PointRemove"));
		connect(remove, &QAbstractButton::clicked, this, [this, i]() {
			if (i >= library_.points.size())
				return;
			library_.points.erase(library_.points.begin() + std::ptrdiff_t(i));
			if (pointShown_ == int(i))
				pointShown_ = -1;
			else if (pointShown_ > int(i))
				--pointShown_;
			rebuildRows();
			scheduleSave();
		});
		h->addWidget(remove);
		rowsLayout_->addWidget(line);
	}
}

void TitlesDock::playPoint(size_t index)
{
	if (index >= library_.points.size())
		return;
	// Only this point goes on screen; the sermon title and heading stay as they are in the boxes.
	library_.draft = contentForPoint(library_.draft, library_.points, index);
	pointShown_ = int(index);
	swapOnAir();
}

void TitlesDock::swapOnAir()
{
	refreshFields();
	updatePreview();
	status_ = statusOf(target());
	if (status_.onAir()) {
		// The title on air leaves first, then the next one arrives, so they never stack.
		hideFromAir(target());
		preview_->stop();
		pendingPlay_ = true;
		pendingTimer_.start();
	} else {
		applyAndShow();
	}
	scheduleSave();
}

void TitlesDock::playKept(size_t index)
{
	const auto bank = library_.banks.find(target());
	if (bank == library_.banks.end() || index >= bank->second.size())
		return;
	library_.draft = bank->second[index]; // the layer keeps its template; only the words change
	keptShown_ = int(index);
	swapOnAir();
}

void TitlesDock::rebuildKept()
{
	page::clear(keptLayout_);
	const TitleTemplate t = resolveStyle(library_, library_.selected);
	const bool pointsMode = t.category == Category::Notes && t.maxRows > 0 && library_.pointsOneAtATime;
	keptBox_->setVisible(!pointsMode && !sources_.empty());
	if (pointsMode || sources_.empty())
		return;
	const std::vector<TitleContent> empty;
	const auto found = library_.banks.find(target());
	const std::vector<TitleContent> &bank = found == library_.banks.end() ? empty : found->second;

	auto *header = new QWidget(keptBox_);
	auto *headerLayout = new QHBoxLayout(header);
	headerLayout->setContentsMargins(0, 0, 0, 0);
	headerLayout->setSpacing(size::gapSmall);
	headerLayout->addWidget(makeLabel(header, lt("Laycue.Titles.KeptTitle").arg(int(bank.size())), 11, 600,
					  color::textTertiary),
				1);
	auto *keep = button(header, lt("Laycue.Titles.KeepThis"), "laycueSecondary");
	connect(keep, &QAbstractButton::clicked, this, [this]() {
		if (keepTitle(library_, target(), library_.draft)) {
			keptShown_ =
				int(library_.banks[target()].size()) - 1; // what is in the boxes is the one just kept
			rebuildKept();
			scheduleSave();
		} else {
			toast(lt("Laycue.Titles.KeepNothing"));
		}
	});
	headerLayout->addWidget(keep);
	keptLayout_->addWidget(header);
	if (bank.empty()) {
		auto *hint = makeLabel(keptBox_, lt("Laycue.Titles.KeptEmpty"), 11, 400, color::textTertiary);
		hint->setWordWrap(true);
		keptLayout_->addWidget(hint);
		return;
	}
	for (size_t i = 0; i < bank.size(); ++i) {
		auto *line = new QWidget(keptBox_);
		auto *h = new QHBoxLayout(line);
		h->setContentsMargins(0, 0, 0, 0);
		h->setSpacing(size::gapSmall);
		auto *show = new RowButton(line);
		show->setTitle(fromStd(keptTitleLabel(bank[i])));
		show->setValue(int(i) == keptShown_ ? lt("Laycue.Titles.PointShown") : lt("Laycue.Titles.PointShow"));
		connect(show, &RowButton::clicked, this, [this, i]() { playKept(i); });
		h->addWidget(show, 1);
		auto *remove = button(line, QStringLiteral("\u2715"), "laycueSecondary");
		remove->setAccessibleName(lt("Laycue.Titles.KeptRemove"));
		connect(remove, &QAbstractButton::clicked, this, [this, i]() {
			auto found = library_.banks.find(target());
			if (found == library_.banks.end() || i >= found->second.size())
				return;
			found->second.erase(found->second.begin() + std::ptrdiff_t(i));
			if (keptShown_ == int(i))
				keptShown_ = -1;
			else if (keptShown_ > int(i))
				--keptShown_;
			rebuildKept();
			scheduleSave();
		});
		h->addWidget(remove);
		keptLayout_->addWidget(line);
	}
}

void TitlesDock::chooseLogo()
{
	const QString file = QFileDialog::getOpenFileName(this, lt("Laycue.Titles.ChooseLogo"), QString(),
							  lt("Laycue.Titles.PictureFiles"));
	if (file.isEmpty())
		return;
	library_.draft.logo = toStd(file);
	logoName_->setText(QFileInfo(file).fileName());
	draftChanged();
}

void TitlesDock::refreshParts()
{
	if (!partsLabel_)
		return;
	const TitleContent &d = library_.draft;
	partsLabel_->setText(d.logoCut <= 0 ? lt("Laycue.Titles.LogoPartsNotSet")
					    : lt("Laycue.Titles.LogoPartsSet").arg(d.logoCut));
}

void TitlesDock::chooseLogoParts()
{
	LogoPartsDialog dialog(fromStd(library_.draft.logo), library_.draft.logoCut, library_.draft.logoStacked,
			       library_.draft.logoMarkSecond, this);
	if (dialog.exec() != QDialog::Accepted)
		return;
	library_.draft.logoCut = dialog.cut();
	library_.draft.logoStacked = dialog.stacked();
	library_.draft.logoMarkSecond = dialog.markSecond();
	refreshParts();
	draftChanged();
}

void TitlesDock::clearLogo()
{
	library_.draft.logo.clear();
	logoName_->setText(lt("Laycue.Titles.NoLogo"));
	draftChanged();
}

void TitlesDock::changeHold(int delta)
{
	// The template's own time, then "until I hide it", then 3 seconds and up in steps that grow.
	static const int steps[] = {-1, 0, 3, 4, 5, 6, 8, 10, 12, 15, 20, 30, 45, 60, 90, 120};
	constexpr int n = int(sizeof(steps) / sizeof(steps[0]));
	int at = 0;
	for (int i = 0; i < n; ++i)
		if (steps[i] <= library_.holdOverride)
			at = i;
	at = std::clamp(at + (delta > 0 ? 1 : -1), 0, n - 1);
	library_.holdOverride = steps[at];
	refreshHold();
	if (status_.onAir() && !onAirStyle_.empty()) // a title is up: the new time applies to it now, its text stays
		applyToSource(target(), library_, onAirStyle_, onAirContent_);
	rebuildSetup();
	scheduleSave();
}

void TitlesDock::refreshOptions()
{
	if (!optionsRow_)
		return;
	const TitleTemplate t = resolveStyle(library_, library_.selected);
	QStringList parts;
	if (supportsPlacement(t)) {
		const Placement p = placementFor(library_, library_.selected);
		static const char *const names[] = {"Laycue.Titles.AlignLeft", "Laycue.Titles.AlignCenter",
						    "Laycue.Titles.AlignRight"};
		parts << lt(names[std::clamp(p.align, 0, 2)]) << QStringLiteral("%1%").arg(p.percent);
	}
	if (supportsFit(t)) {
		const int fit = placementFor(library_, library_.selected).fit;
		if (fit == 1)
			parts << lt("Laycue.Titles.FitText");
		else if (fit == 2)
			parts << lt("Laycue.Titles.WidthPx").arg(placementFor(library_, library_.selected).width);
	}
	if (holdValue_)
		parts << holdValue_->text();
	optionsRow_->setValue(parts.join(QStringLiteral(" \u00B7 ")));
	optionsRow_->setOpen(optionsOpen_);
	optionsBox_->setVisible(optionsOpen_);
}

void TitlesDock::refreshCategory()
{
	if (!categoryRow_)
		return;
	QString name = lt("Laycue.Titles.CatAll");
	if (categoryFilter_ > 0)
		name = lt(categoryKey(Category(categoryFilter_ - 1)));
	categoryRow_->setValue(name);
	categoryRow_->setOpen(true); // the chevron points down: it opens a menu
}

void TitlesDock::pickCategory()
{
	// "All" first, then the categories in the order of the panel's old chips.
	static const Category order[] = {Category::People, Category::Worship, Category::Announce, Category::Notes,
					 Category::News,   Category::Live,    Category::Social,   Category::Ticker};
	QMenu menu(this);
	const auto count = [](int filter) {
		int n = 0;
		for (const TitleTemplate &t : builtInTemplates())
			if (filter == 0 || int(t.category) + 1 == filter)
				++n;
		return n;
	};
	QAction *all = menu.addAction(lt("Laycue.Titles.CategoryItem").arg(lt("Laycue.Titles.CatAll")).arg(count(0)));
	all->setCheckable(true);
	all->setChecked(categoryFilter_ == 0);
	all->setData(0);
	for (Category c : order) {
		QAction *a =
			menu.addAction(lt("Laycue.Titles.CategoryItem").arg(lt(categoryKey(c))).arg(count(int(c) + 1)));
		a->setCheckable(true);
		a->setChecked(categoryFilter_ == int(c) + 1);
		a->setData(int(c) + 1);
	}
	QAction *chosen = menu.exec(categoryRow_->mapToGlobal(QPoint(0, categoryRow_->height() + 2)));
	if (!chosen)
		return;
	categoryFilter_ = chosen->data().toInt();
	refreshCategory();
	layoutTemplates();
}

void TitlesDock::changeSpeed(int delta)
{
	const int next = std::clamp(library_.speed + (delta > 0 ? 1 : -1), 0, kSpeedSteps - 1);
	if (next == library_.speed)
		return;
	library_.speed = next;
	preview_->setSpeed(next);
	preview_->play(); // show what the new speed looks like
	if (status_.onAir() && !onAirStyle_.empty())
		applyToSource(target(), library_, onAirStyle_, onAirContent_);
	else
		applyToSource(target(), library_, library_.selected, library_.draft);
	rebuildSetup();
	scheduleSave();
}

void TitlesDock::pickBrandColour()
{
	const QColor start(library_.brand.r, library_.brand.g, library_.brand.b);
	const QColor chosen = QColorDialog::getColor(start, this, lt("Laycue.Titles.BrandColour"));
	if (!chosen.isValid())
		return;
	library_.brand = Rgba{uint8_t(chosen.red()), uint8_t(chosen.green()), uint8_t(chosen.blue()), 255};
	updatePreview();
	rebuildSetup();
	scheduleSave();
}

void TitlesDock::addToThisScene()
{
	std::string error;
	const std::string name = addToCurrentScene(library_.targetSource, error);
	if (name.empty()) {
		toast(fromStd(error));
		return;
	}
	library_.targetSource = name;
	refreshSources();
	toast(lt("Laycue.Titles.LayerAddedToast"));
	pollStatus();
	rebuildSetup();
	scheduleSave();
}

// Which layers are on air right now, by name, for the layer menu and its summary.
void TitlesDock::refreshLayerRow()
{
	if (!layerRow_)
		return;
	layerRow_->setVisible(!sources_.empty());
	int others = 0;
	for (const std::string &name : sources_) {
		if (name != target() && statusOf(name).live())
			++others;
	}
	QString value = fromStd(target());
	if (others > 0)
		value += QStringLiteral("  \u00B7  ") + lt("Laycue.Titles.LayersOnAir").arg(others);
	layerRow_->setValue(value);
}

void TitlesDock::pickLayer()
{
	QMenu menu(this);
	for (const std::string &name : sources_) {
		const TitleStatus st = statusOf(name);
		QString text = fromStd(name);
		if (st.live())
			text += QStringLiteral("   \u25CF ") + lt("Laycue.Titles.StatusOnAir");
		else if (st.onAir())
			text += QStringLiteral("   ") + lt("Laycue.Titles.StatusCued");
		QAction *a = menu.addAction(text);
		a->setCheckable(true);
		a->setChecked(name == target());
		connect(a, &QAction::triggered, this, [this, name]() { switchLayer(name); });
	}
	menu.addSeparator();
	connect(menu.addAction(lt("Laycue.Titles.NewLayer")), &QAction::triggered, this, &TitlesDock::newLayerClicked);
	menu.exec(layerRow_->mapToGlobal(QPoint(0, layerRow_->height() + 2)));
}

// Moves the panel to another layer: the one it leaves keeps its template and text, the new one brings back its own.
void TitlesDock::switchLayer(const std::string &name)
{
	keptShown_ = -1; // each layer has its own kept titles
	if (name == target() || name.empty())
		return;
	rememberLayer(library_, target());
	library_.targetSource = name;
	recallLayer(library_, name);
	onAirStyle_.clear(); // what is on air is read from the new layer again
	lastSampleStyle_ = std::string();
	refreshFields();
	updatePreview();
	for (auto &entry : cards_)
		entry.second->setSelectedCard(entry.first == library_.selected);
	pollStatus();
	refreshLayerRow();
	if (tab_ == Tab::Setup)
		rebuildSetup();
	scheduleSave();
}

// A layer Laycue made is named after what it shows ("ttl. Pastor David…"), so the Sources panel says which is which.
// A name the operator chose is never touched.
void TitlesDock::nameLayerAfterContent(const TitleContent &shown)
{
	const std::string name = target();
	if (name.empty() || !isAutoLayerName(name, toStd(lt("Laycue.Titles.SourceName"))))
		return;
	const std::string wanted = layerNameFor(shown);
	if (wanted.empty() || wanted == name)
		return;
	const std::string result = renameLayer(name, wanted);
	if (result.empty() || result == name)
		return;
	renameLayerRefs(library_, name, result);
	refreshSources();
	rebuildQueue();
	refreshLayerRow();
	if (tab_ == Tab::Setup)
		rebuildSetup();
	scheduleSave();
}

void TitlesDock::newLayerClicked()
{
	std::string error;
	const std::string name = addNewLayerToCurrentScene(error);
	if (name.empty()) {
		toast(fromStd(error));
		return;
	}
	rememberLayer(library_, target());
	library_.targetSource = name;
	rememberLayer(library_, name); // a new layer starts with what the panel shows now, and keeps its own from here
	onAirStyle_.clear();
	refreshSources();
	toast(lt("Laycue.Titles.LayerAddedToast"));
	pollStatus();
	refreshLayerRow();
	rebuildSetup();
	scheduleSave();
}

void TitlesDock::addToAllScenesClicked()
{
	const auto answer = QMessageBox::question(this, lt("Laycue.Titles.AddToAll"), lt("Laycue.Titles.AddToAllAsk"));
	if (answer != QMessageBox::Yes)
		return;
	const int added = addToAllScenes(target());
	toast(added > 0 ? lt("Laycue.Titles.AddedToScenesToast").arg(added)
			: lt("Laycue.Titles.AlreadyEverywhereToast"));
}

// ---- Custom templates --------------------------------------------------------------------------------------

void TitlesDock::openCustomize(bool editSelected)
{
	Overrides start;
	std::string baseId;
	resolveStyle(library_, library_.selected, &baseId);
	if (editSelected && isCustomId(library_.selected)) {
		if (const Overrides *o = findCustom(library_, customNameOf(library_.selected)))
			start = *o;
	} else {
		start.baseId = baseId;
		start.name = uniqueCustomName(
			library_, fromStd(localised(nameKey(*findTemplate(baseId)))).toStdString() + " custom");
	}

	auto *page = new CustomizePage(start, library_, this);
	connect(page, &PageFrame::backRequested, this, [this]() {
		if (openedPage_) {
			pages_->setCurrentIndex(0);
			pages_->removeWidget(openedPage_);
			openedPage_->deleteLater();
			openedPage_ = nullptr;
		}
	});
	connect(page, &CustomizePage::saved, this, [this](const Overrides &o, const std::string &previousName) {
		if (!previousName.empty() && previousName != o.name)
			removeCustom(library_, previousName);
		if (!saveCustom(library_, o)) {
			toast(lt("Laycue.Titles.CustomFull"));
			return;
		}
		thumbs_.erase(customId(o.name));
		selectStyle(customId(o.name));
		saveNow();
		toast(lt("Laycue.Titles.CustomSavedToast").arg(fromStd(o.name)));
	});
	openedPage_ = page;
	pages_->addWidget(page);
	pages_->setCurrentWidget(page);
}

void TitlesDock::deleteSelectedCustom()
{
	if (!isCustomId(library_.selected))
		return;
	const std::string name = customNameOf(library_.selected);
	const auto answer = QMessageBox::question(this, lt("Laycue.Titles.DeleteCustom"),
						  lt("Laycue.Titles.DeleteCustomAsk").arg(fromStd(name)));
	if (answer != QMessageBox::Yes)
		return;
	thumbs_.erase(library_.selected);
	removeCustom(library_, name);
	refreshFields();
	updatePreview();
	rebuildTemplates();
	scheduleSave();
}

// ---- Housekeeping ------------------------------------------------------------------------------------------

void TitlesDock::toast(const QString &message)
{
	toastText_->setText(message);
	toast_->adjustSize();
	toast_->move((width() - toast_->width()) / 2, height() - toast_->height() - 18);
	toast_->show();
	toast_->raise();
	toastTimer_.start();
}

void TitlesDock::scheduleSave()
{
	saveTimer_.start();
}

void TitlesDock::saveNow()
{
	saveTimer_.stop();
	rememberLayer(library_, target());
	saveLibrary(library_);
}

void TitlesDock::handleFrontendEvent(enum obs_frontend_event event)
{
	switch (event) {
	case OBS_FRONTEND_EVENT_FINISHED_LOADING:
	case OBS_FRONTEND_EVENT_SCENE_COLLECTION_CHANGED:
	case OBS_FRONTEND_EVENT_SCENE_LIST_CHANGED:
		pollStatus();
		break;
	case OBS_FRONTEND_EVENT_SCENE_COLLECTION_CLEANUP:
		// OBS is about to free every source: forget the title layer until the new collection is loaded.
		pendingPlay_ = false;
		pendingTimer_.stop();
		sources_.clear();
		status_ = TitleStatus();
		preview_->stop();
		preview_->setOnAir(false);
		updateStatus();
		updateButtons();
		break;
	case OBS_FRONTEND_EVENT_EXIT:
		shuttingDown_ = true;
		pollTimer_.stop();
		pendingTimer_.stop();
		saveNow();
		break;
	default:
		break;
	}
}

void TitlesDock::showEvent(QShowEvent *event)
{
	QWidget::showEvent(event);
	if (!shuttingDown_ && !pollTimer_.isActive())
		pollTimer_.start();
}

void TitlesDock::hideEvent(QHideEvent *event)
{
	QWidget::hideEvent(event);
	// A hidden panel needs no polling. Titles keep running in OBS either way.
}

bool TitlesDock::event(QEvent *event)
{
	// OBS sends QEvent::User + QEvent::Close when the dock is closed from the Docks menu.
	if (event->type() == QEvent::Type(QEvent::User + QEvent::Close))
		saveNow();
	return QWidget::event(event);
}

} // namespace laycue::titles::ui
