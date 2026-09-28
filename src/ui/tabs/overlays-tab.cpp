#include "ui/tabs/overlays-tab.hpp"

#include "ui/controls.hpp"
#include "ui/labels.hpp"
#include "ui/tokens.hpp"

#include <QAbstractButton>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QPainter>
#include <QPushButton>
#include <QVBoxLayout>

namespace laycue::ui {

namespace {

// One overlay as a button: a little screen showing where it sits, its name, and On / Off.
class OverlayButton : public QAbstractButton {
public:
	OverlayButton(const OverlayRow &row, QWidget *parent) : QAbstractButton(parent), row_(row)
	{
		setCheckable(true);
		setChecked(row.visible);
		setCursor(Qt::PointingHandCursor);
		setFocusPolicy(Qt::TabFocus);
		setMinimumHeight(size::touch + 8);
		setAccessibleName(row.name + QStringLiteral(", ") + row.tag);
		setToolTip(row.tag);
	}

	QSize sizeHint() const override { return {150, size::touch + 8}; }

protected:
	void paintEvent(QPaintEvent *) override
	{
		QPainter p(this);
		p.setRenderHint(QPainter::Antialiasing);
		const bool on = isChecked();
		QRectF card = QRectF(rect()).adjusted(1, 1, -1, -1);
		if (isDown()) {
			const QPointF c = card.center();
			card = QRectF(c.x() - card.width() * 0.49, c.y() - card.height() * 0.49, card.width() * 0.98,
				      card.height() * 0.98);
		}
		p.setPen(QPen(on ? color::overlay : QColor(255, 255, 255, 20), on ? 1.5 : 1));
		p.setBrush(on ? QColor(0x2A, 0x21, 0x12) : color::stage);
		p.drawRoundedRect(card, size::radius + 2, size::radius + 2);

		// Mini screen with the overlay's spot.
		const QRectF screen(card.left() + 10, card.center().y() - 10, 32, 18);
		p.setPen(Qt::NoPen);
		p.setBrush(color::control);
		p.drawRoundedRect(screen, 3, 3);
		p.setBrush(on ? color::overlay : color::textDisabled);
		const QRectF spot(screen.left() + row_.spot.x * screen.width(),
				  screen.top() + row_.spot.y * screen.height(),
				  std::max(2.0, row_.spot.w * screen.width()),
				  std::max(2.0, row_.spot.h * screen.height()));
		p.drawRoundedRect(spot.intersected(screen), 1.5, 1.5);

		// Text
		const QRectF textArea(screen.right() + 10, card.top() + 4, card.right() - screen.right() - 16,
				      card.height() - 8);
		p.setPen(color::cream);
		p.setFont(uiFont(13, 600));
		const QString name =
			QFontMetrics(p.font()).elidedText(row_.name, Qt::ElideRight, int(textArea.width()));
		p.drawText(textArea.adjusted(0, 0, 0, -textArea.height() / 2 + 1), Qt::AlignLeft | Qt::AlignBottom,
			   name);
		p.setPen(on ? color::overlay : color::textTertiary);
		p.setFont(uiFont(11, 600));
		p.drawText(textArea.adjusted(0, textArea.height() / 2 + 1, 0, 0), Qt::AlignLeft | Qt::AlignTop,
			   on ? lt("Laycue.Overlays.On") : lt("Laycue.Overlays.Off"));

		if (hasFocus()) {
			p.setBrush(Qt::NoBrush);
			p.setPen(QPen(color::setup, 2));
			p.drawRoundedRect(QRectF(rect()).adjusted(1, 1, -1, -1), size::radius + 2, size::radius + 2);
		}
	}

private:
	OverlayRow row_;
};

QLabel *label(QWidget *parent, const QString &text, int px, int weight, const QColor &colour)
{
	auto *l = new QLabel(text, parent);
	l->setFont(uiFont(px, weight));
	l->setStyleSheet(labelStyle(colour, l->font()));
	l->setWordWrap(true);
	return l;
}

} // namespace

OverlaysTab::OverlaysTab(QWidget *parent) : QWidget(parent)
{
	root_ = new QVBoxLayout(this);
	root_->setContentsMargins(0, 0, 0, 0);
	root_->setSpacing(size::gap);
}

void OverlaysTab::setRows(const std::vector<OverlayRow> &rows, const QString &sceneName, bool anyLabelled, bool locked)
{
	if (built_ && rows == rows_ && sceneName == sceneName_ && anyLabelled == anyLabelled_ && locked == locked_)
		return;
	rows_ = rows;
	sceneName_ = sceneName;
	anyLabelled_ = anyLabelled;
	locked_ = locked;
	rebuild();
}

void OverlaysTab::rebuild()
{
	built_ = true;
	while (QLayoutItem *item = root_->takeAt(0)) {
		if (item->widget())
			item->widget()->deleteLater();
		delete item;
	}

	auto addSetup = [&](const QString &text, const char *object) {
		if (locked_)
			return;
		const auto kind = std::string(object) == "laycuePrimary" ? ActionButton::Kind::Primary
									 : ActionButton::Kind::Link;
		auto *b = new ActionButton(text, kind, this);
		connect(b, &QAbstractButton::clicked, this, &OverlaysTab::setupRequested);
		root_->addWidget(b, 0, Qt::AlignLeft);
	};

	if (!anyLabelled_) {
		root_->addWidget(label(this, lt("Laycue.Overlays.EmptyTitle"), 15, 700, color::cream));
		root_->addWidget(label(this, lt("Laycue.Overlays.EmptyBody"), 12, 400, color::textSecondary));
		addSetup(lt("Laycue.Overlays.SetUp"), "laycuePrimary");
		root_->addStretch(1);
		return;
	}

	auto *header = new QHBoxLayout();
	header->addWidget(label(this, lt("Laycue.Overlays.Title"), 15, 700, color::cream), 1);
	root_->addLayout(header);
	root_->addWidget(label(this, lt("Laycue.Overlays.OnScene").arg(sceneName_), 11, 400, color::textTertiary));

	if (rows_.empty()) {
		root_->addWidget(label(this, lt("Laycue.Overlays.NoneInScene"), 12, 400, color::textSecondary));
	} else {
		auto *grid = new QGridLayout();
		grid->setHorizontalSpacing(size::gapSmall);
		grid->setVerticalSpacing(size::gapSmall);
		for (size_t i = 0; i < rows_.size(); ++i) {
			auto *button = new OverlayButton(rows_[i], this);
			const OverlayRow row = rows_[i];
			connect(button, &QAbstractButton::clicked, this,
				[this, row](bool checked) { emit toggled(row.sceneUuid, row.itemId, checked); });
			grid->addWidget(button, int(i) / 2, int(i) % 2);
		}
		root_->addLayout(grid);
		root_->addWidget(label(this, lt("Laycue.Overlays.RuleHint"), 11, 400, color::textTertiary));
	}
	addSetup(lt("Laycue.Overlays.EditSetup"), "laycueLink");
	root_->addStretch(1);
}

} // namespace laycue::ui
