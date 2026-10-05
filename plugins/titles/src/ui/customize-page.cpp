#include "ui/customize-page.hpp"

#include "ui/controls.hpp"
#include "ui/pages/helpers.hpp"
#include "ui/text.hpp"
#include "ui/title-widgets.hpp"
#include "ui/tokens.hpp"

#include <obs-module.h>

#include <QColorDialog>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QMenu>
#include <QPainter>
#include <QVBoxLayout>

#include <algorithm>
#include <vector>

namespace laycue::titles::ui {

using namespace laycue::ui;
using laycue::ui::page::button;
using laycue::ui::page::innerLabel;

namespace {

struct FaceName {
	FontFace face;
	const char *name;
};

const FaceName kFaces[] = {
	{FontFace::BarlowCondensedMedium, "Barlow Condensed Medium"},
	{FontFace::BarlowCondensedSemiBold, "Barlow Condensed SemiBold"},
	{FontFace::BarlowCondensedBold, "Barlow Condensed Bold"},
	{FontFace::BarlowRegular, "Barlow"},
	{FontFace::BarlowSemiBold, "Barlow SemiBold"},
	{FontFace::BebasNeue, "Bebas Neue"},
	{FontFace::Baskerville, "Libre Baskerville"},
	{FontFace::BaskervilleItalic, "Libre Baskerville Italic"},
	{FontFace::OutfitRegular, "Outfit"},
	{FontFace::OutfitSemiBold, "Outfit SemiBold"},
	{FontFace::OutfitBold, "Outfit Bold"},
	{FontFace::OutfitExtraBold, "Outfit ExtraBold"},
};

const char *faceName(FontFace f)
{
	for (const FaceName &n : kFaces) {
		if (n.face == f)
			return n.name;
	}
	return "";
}

const MotionKind kMotions[] = {MotionKind::Slide, MotionKind::Rise, MotionKind::Fade, MotionKind::Pop,
			       MotionKind::Wipe};

int motionIndex(MotionKind m)
{
	for (int i = 0; i < 5; ++i) {
		if (kMotions[i] == m)
			return i;
	}
	return 0;
}

// A small square showing a colour.
class Swatch : public QWidget {
public:
	Swatch(const QColor &c, QWidget *parent) : QWidget(parent), colour_(c) { setFixedSize(30, 30); }

protected:
	void paintEvent(QPaintEvent *) override
	{
		QPainter p(this);
		p.setRenderHint(QPainter::Antialiasing, true);
		const QRectF r = QRectF(rect()).adjusted(1, 1, -1, -1);
		// A checkerboard under the colour so transparency is visible.
		p.setPen(Qt::NoPen);
		p.setBrush(QColor(90, 90, 90));
		p.drawRoundedRect(r, 7, 7);
		p.setBrush(QColor(140, 140, 140));
		p.drawRect(QRectF(r.center().x(), r.top(), r.width() / 2, r.height() / 2));
		p.drawRect(QRectF(r.left(), r.center().y(), r.width() / 2, r.height() / 2));
		p.setBrush(colour_);
		p.drawRoundedRect(r, 7, 7);
		p.setBrush(Qt::NoBrush);
		p.setPen(QPen(QColor(255, 255, 255, 60), 1));
		p.drawRoundedRect(r, 7, 7);
	}

private:
	QColor colour_;
};

QColor toQ(const Rgba &c)
{
	return QColor(c.r, c.g, c.b, c.a);
}

Rgba fromQ(const QColor &c)
{
	return Rgba{uint8_t(c.red()), uint8_t(c.green()), uint8_t(c.blue()), uint8_t(c.alpha())};
}

} // namespace

CustomizePage::CustomizePage(const Overrides &start, const Library &library, QWidget *parent)
	: PageFrame(lt("Laycue.Titles.Heading"), lt("Laycue.Titles.CustomizeTitle"), parent),
	  o_(start),
	  previousName_(findCustom(library, start.name) ? start.name : std::string()),
	  library_(library)
{
	sanitize(o_);

	preview_ = new TitlePreview(this);
	auto *previewRow = new QHBoxLayout();
	previewRow->setContentsMargins(size::shadowMargin, 0, size::shadowMargin, 0);
	previewRow->addWidget(preview_);
	body()->addLayout(previewRow);

	auto *play = button(this, lt("Laycue.Titles.PreviewPlay"), "laycueSecondary");
	connect(play, &QAbstractButton::clicked, this, [this]() { preview_->play(); });
	auto *playRow = new QHBoxLayout();
	playRow->setContentsMargins(size::shadowMargin, 0, size::shadowMargin, 0);
	playRow->addWidget(play);
	auto *stopButton = button(this, lt("Laycue.Titles.Hide"), "laycueSecondary");
	connect(stopButton, &QAbstractButton::clicked, this, [this]() { preview_->stop(); });
	playRow->addWidget(stopButton);
	playRow->addStretch(1);
	body()->addLayout(playRow);

	controls_ = new QWidget(this);
	new QVBoxLayout(controls_);
	controls_->layout()->setContentsMargins(0, 0, 0, 0);
	static_cast<QVBoxLayout *>(controls_->layout())->setSpacing(size::gap);
	body()->addWidget(controls_);

	auto *footer = new QWidget(this);
	auto *row = new QHBoxLayout(footer);
	row->setContentsMargins(size::shadowMargin, 4, size::shadowMargin, 4);
	auto *saveButton = button(footer, lt("Laycue.Titles.SaveCustom"), "laycuePrimary");
	saveButton->setMinimumHeight(size::touch);
	connect(saveButton, &QAbstractButton::clicked, this, &CustomizePage::save);
	row->addWidget(saveButton, 1);
	setFooter(footer);

	rebuild();
	refreshPreview();
}

void CustomizePage::refreshPreview()
{
	const TitleTemplate *base = findTemplate(o_.baseId);
	if (!base)
		return;
	const TitleTemplate t = resolve(*base, o_);
	const auto text = [&](int field) {
		return QString::fromUtf8(obs_module_text(sampleKey(*base, field).c_str()));
	};
	TitleText sample{text(1), t.usesLine2 ? text(2) : QString(), t.usesTag ? text(3) : QString()};
	if (t.usesNote)
		sample.note = text(4);
	for (int r = 1; r <= t.maxRows; ++r) {
		const std::string rowKey = nameKey(*base) + ".R" + std::to_string(r);
		const QString value = QString::fromUtf8(obs_module_text((rowKey + "V").c_str()));
		if (!value.isEmpty() && value != QString::fromStdString(rowKey + "V"))
			sample.rows.append({QString::fromUtf8(obs_module_text((rowKey + "L").c_str())), value});
	}
	preview_->setTitle(t, sample, false, Rgba());
}

void CustomizePage::rebuild()
{
	auto *layout = static_cast<QVBoxLayout *>(controls_->layout());
	page::clear(layout);
	const TitleTemplate *base = findTemplate(o_.baseId);
	if (!base)
		return;
	const TitleTemplate t = resolve(*base, o_);

	const auto group = [&](const QString &title) -> QVBoxLayout * {
		layout->addWidget(innerLabel(controls_, title.toUpper(), 11, 700, color::textTertiary));
		auto *card = new Surface(1, controls_);
		auto *v = new QVBoxLayout(card);
		v->setSpacing(size::gapSmall + 2);
		card->setPadding(14, 10, 14, 10);
		layout->addWidget(card);
		return v;
	};

	// Name and starting design.
	{
		QVBoxLayout *v = group(lt("Laycue.Titles.CustomName"));
		auto *name = new QLineEdit(controls_);
		name->setText(QString::fromStdString(o_.name));
		name->setAccessibleName(lt("Laycue.Titles.CustomName"));
		connect(name, &QLineEdit::textEdited, this,
			[this](const QString &s) { o_.name = s.trimmed().toStdString(); });
		v->addWidget(name);
		auto *from = button(
			controls_,
			lt("Laycue.Titles.StartFrom").arg(QString::fromUtf8(obs_module_text(nameKey(*base).c_str()))),
			"laycueSecondary");
		connect(from, &QAbstractButton::clicked, this, [this, from]() {
			QMenu menu(this);
			for (const TitleTemplate &candidate : builtInTemplates()) {
				QAction *a =
					menu.addAction(QString::fromUtf8(obs_module_text(nameKey(candidate).c_str())));
				connect(a, &QAction::triggered, this, [this, id = candidate.id]() {
					o_.baseId = id;
					rebuild();
					refreshPreview();
				});
			}
			menu.exec(from->mapToGlobal(QPoint(0, from->height())));
		});
		v->addWidget(from);
	}

	// Colours.
	{
		QVBoxLayout *v = group(lt("Laycue.Titles.CustomColours"));
		struct ColourRow {
			const char *role; // names the template's own label for it: Laycue.Titles.T.<id>.C.<role>
			const char *labelKey;
			Rgba Palette::*member;
			bool Overrides::*has;
			bool alpha;
			QString label;
		};
		std::vector<ColourRow> rows = {
			{"panel", "Laycue.Titles.ColourPanel", &Palette::panel, &Overrides::hasPanel, true, {}},
			{"accent", "Laycue.Titles.ColourAccent", &Palette::accent, &Overrides::hasAccent, false, {}},
			{"text", "Laycue.Titles.ColourText", &Palette::text, &Overrides::hasText, false, {}},
			{"subtext", "Laycue.Titles.ColourSubtext", &Palette::subtext, &Overrides::hasSubtext, false, {}},
		};
		if (t.usesShape2)
			rows.push_back({"shape2",
					"Laycue.Titles.ColourShape2",
					&Palette::shape2,
					&Overrides::hasShape2,
					false,
					{}});
		if (t.usesShape3)
			rows.push_back({"shape3",
					"Laycue.Titles.ColourShape3",
					&Palette::shape3,
					&Overrides::hasShape3,
					false,
					{}});
		if (t.usesInk)
			rows.push_back(
				{"ink", "Laycue.Titles.ColourInk", &Palette::ink, &Overrides::hasInk, false, {}});
		for (ColourRow &r : rows) {
			// A template can name its own colours ("Tab above the bar"); otherwise the general name is used.
			const std::string key = nameKey(t) + ".C." + r.role;
			const char *own = obs_module_text(key.c_str());
			r.label = own && key != own ? QString::fromUtf8(own) : lt(r.labelKey);
		}
		for (const ColourRow &r : rows) {
			auto *h = new QHBoxLayout();
			h->setSpacing(size::gapSmall + 2);
			h->addWidget(new Swatch(toQ(t.palette.*r.member), controls_));
			h->addWidget(innerLabel(controls_, r.label, 13, 500, color::cream), 1);
			auto *pick = button(controls_, lt("Laycue.Titles.Change"), "laycueSecondary");
			connect(pick, &QAbstractButton::clicked, this, [this, r, t]() {
				const QColorDialog::ColorDialogOptions options =
					r.alpha ? QColorDialog::ShowAlphaChannel : QColorDialog::ColorDialogOptions();
				const QColor chosen =
					QColorDialog::getColor(toQ(t.palette.*r.member), this, r.label, options);
				if (!chosen.isValid())
					return;
				o_.palette.*r.member = fromQ(chosen);
				o_.*r.has = true;
				rebuild();
				refreshPreview();
			});
			h->addWidget(pick);
			if (o_.*r.has) {
				auto *reset = button(controls_, lt("Laycue.Titles.Reset"), "laycueLink");
				connect(reset, &QAbstractButton::clicked, this, [this, r]() {
					o_.*r.has = false;
					rebuild();
					refreshPreview();
				});
				h->addWidget(reset);
			}
			v->addLayout(h);
		}
	}

	// Fonts.
	{
		QVBoxLayout *v = group(lt("Laycue.Titles.CustomFonts"));
		const auto fontRow = [&](const QString &label, FontFace current, std::function<void(FontFace)> apply) {
			auto *h = new QHBoxLayout();
			h->setSpacing(size::gapSmall);
			h->addWidget(innerLabel(controls_, label, 13, 500, color::cream), 1);
			auto *pick = button(controls_, QString::fromLatin1(faceName(current)), "laycueSecondary");
			connect(pick, &QAbstractButton::clicked, this, [this, pick, apply]() {
				QMenu menu(this);
				for (const FaceName &n : kFaces) {
					QAction *a = menu.addAction(QString::fromLatin1(n.name));
					connect(a, &QAction::triggered, this, [this, apply, face = n.face]() {
						apply(face);
						rebuild();
						refreshPreview();
					});
				}
				menu.exec(pick->mapToGlobal(QPoint(0, pick->height())));
			});
			h->addWidget(pick, 2);
			v->addLayout(h);
		};
		fontRow(lt("Laycue.Titles.FontMain"), t.display, [this](FontFace f) {
			o_.hasDisplay = true;
			o_.display = f;
		});
		fontRow(lt("Laycue.Titles.FontSecond"), t.body, [this](FontFace f) {
			o_.hasBody = true;
			o_.body = f;
		});
	}

	// Motion.
	{
		QVBoxLayout *v = group(lt("Laycue.Titles.CustomMotion"));
		const QStringList names = {lt("Laycue.Titles.MotionSlide"), lt("Laycue.Titles.MotionRise"),
					   lt("Laycue.Titles.MotionFade"), lt("Laycue.Titles.MotionPop"),
					   lt("Laycue.Titles.MotionWipe")};
		v->addWidget(innerLabel(controls_, lt("Laycue.Titles.MotionIn"), 12, 600, color::textSecondary));
		auto *in = new Segmented(controls_);
		in->setItems(names);
		in->setCurrent(motionIndex(t.motionIn));
		connect(in, &Segmented::changed, this, [this](int i) {
			o_.hasMotionIn = true;
			o_.motionIn = kMotions[i];
			refreshPreview();
			preview_->play();
		});
		v->addWidget(in);
		v->addWidget(innerLabel(controls_, lt("Laycue.Titles.MotionOut"), 12, 600, color::textSecondary));
		auto *out = new Segmented(controls_);
		out->setItems(names);
		out->setCurrent(motionIndex(t.motionOut));
		connect(out, &Segmented::changed, this, [this](int i) {
			o_.hasMotionOut = true;
			o_.motionOut = kMotions[i];
			refreshPreview();
		});
		v->addWidget(out);
	}

	// Shape and timing.
	{
		QVBoxLayout *v = group(lt("Laycue.Titles.CustomShape"));
		const auto stepper = [&](const QString &label, const QString &value, std::function<void(int)> change) {
			auto *h = new QHBoxLayout();
			h->setSpacing(size::gapSmall);
			h->addWidget(innerLabel(controls_, label, 13, 500, color::cream), 1);
			auto *minus = button(controls_, QStringLiteral("−"), "laycueSecondary");
			connect(minus, &QAbstractButton::clicked, this, [change]() { change(-1); });
			h->addWidget(minus);
			auto *val = innerLabel(controls_, value, 13, 600, color::cream);
			val->setAlignment(Qt::AlignCenter);
			val->setMinimumWidth(64);
			h->addWidget(val);
			auto *plus = button(controls_, QStringLiteral("+"), "laycueSecondary");
			connect(plus, &QAbstractButton::clicked, this, [change]() { change(+1); });
			h->addWidget(plus);
			v->addLayout(h);
		};
		stepper(lt("Laycue.Titles.CornerRadius"), QString::number(int(t.cornerRadius)), [this, t](int d) {
			o_.hasCornerRadius = true;
			o_.cornerRadius = std::clamp(t.cornerRadius + 4.0 * d, 0.0, 60.0);
			rebuild();
			refreshPreview();
		});
		stepper(lt("Laycue.Titles.HoldFor"),
			t.holdSeconds <= 0.0 ? lt("Laycue.Titles.HoldNever")
					     : lt("Laycue.Titles.HoldSeconds").arg(int(t.holdSeconds)),
			[this, t](int d) {
				o_.hasHold = true;
				o_.holdSeconds = std::clamp(
					t.holdSeconds + (t.holdSeconds < 3.0 && d > 0 ? 3.0 : double(d)), 0.0, 120.0);
				if (o_.holdSeconds < 3.0 && d < 0)
					o_.holdSeconds = 0.0;
				rebuild();
				refreshPreview();
			});
		if (t.tickerSpeed > 0.0) {
			stepper(lt("Laycue.Titles.ScrollSpeed"), QString::number(int(t.tickerSpeed)), [this, t](int d) {
				o_.hasSpeed = true;
				o_.tickerSpeed = std::clamp(t.tickerSpeed + 20.0 * d, 20.0, 600.0);
				rebuild();
				refreshPreview();
			});
		}
	}
	layout->addStretch(1);
}

void CustomizePage::save()
{
	sanitize(o_);
	if (o_.name.empty())
		o_.name = uniqueCustomName(library_, "My template");
	emit saved(o_, previousName_);
	emit backRequested();
}

} // namespace laycue::titles::ui
