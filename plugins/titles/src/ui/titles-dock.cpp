#include "ui/titles-dock.hpp"

#include "obs/title-source.hpp"
#include "render/font-set.hpp"
#include "store/library-io.hpp"
#include "ui/controls.hpp"
#include "ui/customize-page.hpp"
#include "ui/pages/helpers.hpp"
#include "ui/style.hpp"
#include "ui/text.hpp"
#include "ui/title-widgets.hpp"
#include "ui/tokens.hpp"

#include <obs-module.h>
#include <plugin-support.h>

#include <QApplication>
#include <QColorDialog>
#include <QEvent>
#include <QGridLayout>
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

constexpr int kPollMs = 250;
constexpr int kSaveMs = 700;
constexpr int kToastMs = 3200;
constexpr int kCardMinWidth = 150;

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
	return text && key != text ? std::string(text) : std::string();
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
	auto *header = new QHBoxLayout();
	header->setContentsMargins(size::shadowMargin, 2, size::shadowMargin, 2);
	header->setSpacing(size::gapSmall);
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

	pages_->addWidget(home_);

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
	stage->setPadding(12, 10, 12, 12);

	preview_ = new TitlePreview(stage);
	preview_->setAccessibleName(lt("Laycue.Titles.PreviewAccessible"));
	v->addWidget(preview_);

	const auto field = [&](QLabel *&label, const QString &name) {
		label = makeLabel(stage, name, 11, 600, color::textTertiary);
		v->addWidget(label);
	};

	field(label1_, lt("Laycue.Titles.Line1"));
	edit1_ = new QLineEdit(stage);
	long1_ = new QPlainTextEdit(stage);
	long1_->setFixedHeight(74);
	long1_->setTabChangesFocus(true);
	v->addWidget(edit1_);
	v->addWidget(long1_);
	connect(edit1_, &QLineEdit::textEdited, this, &TitlesDock::fieldsEdited);
	connect(long1_, &QPlainTextEdit::textChanged, this, [this]() {
		if (!buildingFields_)
			fieldsEdited();
	});

	field(label2_, lt("Laycue.Titles.Line2"));
	edit2_ = new QLineEdit(stage);
	v->addWidget(edit2_);
	connect(edit2_, &QLineEdit::textEdited, this, &TitlesDock::fieldsEdited);

	field(label3_, lt("Laycue.Titles.Tag"));
	edit3_ = new QLineEdit(stage);
	v->addWidget(edit3_);
	connect(edit3_, &QLineEdit::textEdited, this, &TitlesDock::fieldsEdited);

	auto *row = new QHBoxLayout();
	row->setSpacing(size::gapSmall);
	primary_ = new ActionButton(lt("Laycue.Titles.ShowOnAir"), ActionButton::Kind::Primary, stage);
	primary_->setMinimumHeight(size::touch);
	connect(primary_, &QAbstractButton::clicked, this, &TitlesDock::primaryClicked);
	row->addWidget(primary_, 3);
	hide_ = new ActionButton(lt("Laycue.Titles.Hide"), ActionButton::Kind::Secondary, stage);
	hide_->setMinimumHeight(size::touch);
	connect(hide_, &QAbstractButton::clicked, this, &TitlesDock::hideClicked);
	row->addWidget(hide_, 1);
	queueAdd_ = new ActionButton(lt("Laycue.Titles.QueueAdd"), ActionButton::Kind::Secondary, stage);
	queueAdd_->setMinimumHeight(size::touch);
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
	auto *host = new QWidget(tabScroll_);
	auto *hostLayout = new QVBoxLayout(host);
	hostLayout->setContentsMargins(0, 0, 4, 0);
	hostLayout->setSpacing(0);

	// Templates
	templatesTab_ = new QWidget(host);
	auto *t = new QVBoxLayout(templatesTab_);
	t->setContentsMargins(0, 0, 0, 0);
	t->setSpacing(size::gapSmall + 2);
	categories_ = new Segmented(templatesTab_);
	categories_->setItems({lt("Laycue.Titles.CatAll"), lt(categoryKey(Category::People)),
			       lt(categoryKey(Category::Worship)), lt(categoryKey(Category::Announce)),
			       lt(categoryKey(Category::Ticker))});
	categories_->setAccessibleName(lt("Laycue.Titles.CategoriesAccessible"));
	connect(categories_, &Segmented::changed, this, [this](int i) {
		categoryFilter_ = i;
		layoutTemplates();
	});
	t->addWidget(categories_);
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
}

// ---- Content -----------------------------------------------------------------------------------------------

TitleContent TitlesDock::currentContent() const
{
	TitleContent c;
	const TitleTemplate t = resolveStyle(library_, library_.selected);
	c.line1 = toStd(t.longText ? long1_->toPlainText() : edit1_->text());
	c.line2 = toStd(edit2_->text());
	c.tag = toStd(edit3_->text());
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

	edit1_->setVisible(!t.longText);
	long1_->setVisible(t.longText);
	label2_->setVisible(t.usesLine2);
	edit2_->setVisible(t.usesLine2);
	label3_->setVisible(t.usesTag);
	edit3_->setVisible(t.usesTag);

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
	TitleText text;
	text.line1 = fromStd(library_.draft.line1);
	text.line2 = fromStd(library_.draft.line2);
	text.tag = fromStd(library_.draft.tag);
	preview_->setTitle(t, text, library_.useBrand, library_.brand);
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
		library_.draft = sampleFor(styleId);
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
		req.text = {fromStd(sample.line1), fromStd(sample.line2), fromStd(sample.tag)};
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
	preview_->play();
	preview_->setOnAir(true);
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
		if (onAirContent_ == library_.draft && onAirStyle_ == library_.selected) {
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
	if (!enqueue(library_, {library_.selected, c})) {
		toast(lt("Laycue.Titles.QueueFull"));
		return;
	}
	toast(lt("Laycue.Titles.QueuedToast").arg(int(library_.queue.size())));
	rebuildQueue();
	updateButtons();
	scheduleSave();
}

void TitlesDock::playQueueItem(size_t index)
{
	if (index >= library_.queue.size())
		return;
	const QueueItem item = library_.queue[index];
	removeFromQueue(library_, index);
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
	playQueueItem(0);
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
	}
	if (!status_.onAir()) {
		onAirStyle_.clear();
		if (preview_->onAir())
			preview_->stop();
	}
	preview_->setOnAir(status_.onAir());
	updateStatus();
	updateButtons();
}

void TitlesDock::updateStatus()
{
	static_cast<StatusDot *>(statusDot_)->setOn(status_.onAir());
	if (!status_.found) {
		statusLabel_->setText(sources_.empty() ? lt("Laycue.Titles.StatusNoLayer")
						       : lt("Laycue.Titles.StatusHidden"));
		statusLabel_->setStyleSheet(labelStyle(color::textTertiary, statusLabel_->font()));
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
	const bool changed = onAir && !(onAirContent_ == library_.draft && onAirStyle_ == library_.selected);

	if (noLayer) {
		primary_->setText(lt("Laycue.Titles.AddLayerToScene"));
		primary_->setKind(ActionButton::Kind::Primary);
		hint_->setText(lt("Laycue.Titles.AddLayerHint"));
	} else if (onAir) {
		primary_->setText(changed ? lt("Laycue.Titles.UpdateOnAir") : lt("Laycue.Titles.HideTitle"));
		primary_->setKind(changed ? ActionButton::Kind::Primary : ActionButton::Kind::Secondary);
		primary_->setEnabled(true);
		hint_->setText(changed ? lt("Laycue.Titles.UpdateHint") : QString());
	} else {
		primary_->setText(lt("Laycue.Titles.ShowOnAir"));
		primary_->setKind(ActionButton::Kind::Primary);
		primary_->setEnabled(true);
		hint_->setText(QString());
	}
	hide_->setVisible(onAir && changed); // when nothing changed, the main button already says Hide
	queueAdd_->setVisible(!noLayer);
	hint_->setVisible(!hint_->text().isEmpty());
}

// ---- Queue -------------------------------------------------------------------------------------------------

void TitlesDock::rebuildQueue()
{
	page::clear(queueLayout_);
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
	auto *playNext = button(queueTab_, lt("Laycue.Titles.PlayNext"), "laycuePrimary");
	playNext->setMinimumHeight(size::touch);
	connect(playNext, &QAbstractButton::clicked, this, &TitlesDock::playNextClicked);
	queueLayout_->addWidget(playNext);

	for (size_t i = 0; i < library_.queue.size(); ++i) {
		const QueueItem &item = library_.queue[i];
		auto *card = new Surface(1, queueTab_);
		auto *h = new QHBoxLayout(card);
		h->setSpacing(size::gapSmall);
		card->setPadding(12, 8, 10, 8);
		auto *textColumn = new QVBoxLayout();
		textColumn->setSpacing(1);
		QString first = fromStd(item.content.line1).simplified();
		if (first.size() > 60)
			first = first.left(58) + QStringLiteral("…");
		textColumn->addWidget(
			makeLabel(card, first.isEmpty() ? lt("Laycue.Titles.Untitled") : first, 13, 600, color::cream));
		std::string baseId;
		const TitleTemplate t = resolveStyle(library_, item.styleId, &baseId);
		QString detail = isCustomId(item.styleId) ? fromStd(customNameOf(item.styleId))
							  : fromStd(localised(nameKey(t)));
		if (!item.content.line2.empty())
			detail += QStringLiteral(" · ") + fromStd(item.content.line2).simplified().left(40);
		textColumn->addWidget(makeLabel(card, detail, 11, 400, color::textTertiary));
		h->addLayout(textColumn, 1);
		auto *play = button(card, lt("Laycue.Titles.PlayNow"), "laycueSecondary");
		const size_t index = i;
		connect(play, &QAbstractButton::clicked, this, [this, index]() { playQueueItem(index); });
		h->addWidget(play);
		auto *remove = button(card, QStringLiteral("✕"), "laycueLink");
		remove->setAccessibleName(lt("Laycue.Titles.Remove"));
		connect(remove, &QAbstractButton::clicked, this, [this, index]() {
			removeFromQueue(library_, index);
			rebuildQueue();
			scheduleSave();
		});
		h->addWidget(remove);
		queueLayout_->addWidget(card);
	}
	queueLayout_->addStretch(1);
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
		if (sources_.size() > 1) {
			auto *choose = button(v->parentWidget(), lt("Laycue.Titles.ChooseLayer"), "laycueLink");
			connect(choose, &QAbstractButton::clicked, this, [this, choose]() {
				QMenu menu(this);
				for (const std::string &name : sources_) {
					QAction *a = menu.addAction(fromStd(name));
					a->setCheckable(true);
					a->setChecked(name == target());
					connect(a, &QAction::triggered, this, [this, name]() {
						library_.targetSource = name;
						scheduleSave();
						pollStatus();
						rebuildSetup();
					});
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
	}

	// Hotkeys.
	{
		QVBoxLayout *v = group(lt("Laycue.Titles.SetupKeys"));
		v->addWidget(
			makeLabel(v->parentWidget(), lt("Laycue.Titles.SetupKeysBody"), 12, 400, color::textSecondary));
	}
	setupLayout_->addStretch(1);
}

void TitlesDock::changeHold(int delta)
{
	int hold = library_.holdOverride;
	// -1 (template) -> 0 (never) -> 3 -> 4 ... : never is a deliberate choice, so it sits next to the template default.
	if (delta > 0)
		hold = hold < 0 ? 0 : hold == 0 ? 3 : std::min(hold + 1, 120);
	else
		hold = hold <= 0 ? -1 : hold == 3 ? 0 : hold - 1;
	library_.holdOverride = hold;
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
			pages_->setCurrentWidget(home_);
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
