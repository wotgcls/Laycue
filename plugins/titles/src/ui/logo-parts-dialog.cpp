#include "ui/logo-parts-dialog.hpp"

#include "core/library.hpp"
#include "ui/controls.hpp"
#include "ui/pages/helpers.hpp"
#include "ui/style.hpp"
#include "ui/text.hpp"
#include "ui/tokens.hpp"

#include <QHBoxLayout>
#include <QKeyEvent>
#include <QMouseEvent>
#include <QPainter>
#include <QVBoxLayout>

#include <algorithm>

namespace laycue::titles::ui {

using namespace laycue::ui;
using laycue::ui::page::innerLabel;

// The picture with a draggable line across it. The part that is the wordmark is dimmed and named, the mark
// is outlined, so it is clear which is which.
class PartsView : public QWidget {
public:
	explicit PartsView(const QImage &image, QWidget *parent) : QWidget(parent), image_(image)
	{
		setMinimumHeight(190);
		setFocusPolicy(Qt::StrongFocus);
		setCursor(Qt::SplitHCursor);
		setAccessibleName(lt("Laycue.Titles.LogoPartsTitle"));
	}

	int cut() const { return cut_; }
	void setCut(int cut)
	{
		cut_ = std::clamp(cut, kMinLogoCut, kMaxLogoCut);
		update();
	}
	void setStacked(bool stacked)
	{
		stacked_ = stacked;
		setCursor(stacked ? Qt::SplitVCursor : Qt::SplitHCursor);
		update();
	}
	void setMarkSecond(bool second)
	{
		markSecond_ = second;
		update();
	}
	bool stacked() const { return stacked_; }
	bool markSecond() const { return markSecond_; }

protected:
	void paintEvent(QPaintEvent *) override
	{
		QPainter p(this);
		p.setRenderHint(QPainter::Antialiasing, true);
		p.setRenderHint(QPainter::SmoothPixmapTransform, true);
		// Mid grey, so both white and black logos can be seen.
		p.setPen(Qt::NoPen);
		p.setBrush(QColor(0x6B, 0x68, 0x62));
		p.drawRoundedRect(rect(), size::radius, size::radius);
		const QRectF box = imageRect();
		p.drawImage(box, image_);
		const QRectF a = partA(box), b = partB(box);
		const QRectF mark = markSecond_ ? b : a, word = markSecond_ ? a : b;
		p.setBrush(QColor(0, 0, 0, 120));
		p.drawRect(word);
		p.setBrush(Qt::NoBrush);
		p.setPen(QPen(color::setup, 2));
		p.drawRect(mark.adjusted(1, 1, -1, -1));
		p.setFont(uiFont(11, 700));
		p.setPen(color::cream);
		p.drawText(word.adjusted(6, 4, -6, -4), Qt::AlignTop | Qt::AlignLeft,
			   lt("Laycue.Titles.LogoPartsWord"));
		p.setPen(color::setup);
		p.drawText(mark.adjusted(6, 4, -6, -4), Qt::AlignTop | Qt::AlignLeft,
			   lt("Laycue.Titles.LogoPartsMark"));
		// The dividing line, with a handle.
		const double f = cut_ / 100.0;
		QRectF handle;
		p.setPen(QPen(color::cream, 2, Qt::DashLine));
		if (!stacked_) {
			const double x = box.left() + box.width() * f;
			p.drawLine(QPointF(x, box.top() - 6), QPointF(x, box.bottom() + 6));
			handle = QRectF(x - 6, box.center().y() - 16, 12, 32);
		} else {
			const double y = box.top() + box.height() * f;
			p.drawLine(QPointF(box.left() - 6, y), QPointF(box.right() + 6, y));
			handle = QRectF(box.center().x() - 16, y - 6, 32, 12);
		}
		p.setPen(Qt::NoPen);
		p.setBrush(color::cream);
		p.drawRoundedRect(handle, 4, 4);
		if (hasFocus()) {
			p.setBrush(Qt::NoBrush);
			p.setPen(QPen(color::setup, 2));
			p.drawRoundedRect(QRectF(rect()).adjusted(1, 1, -1, -1), size::radius, size::radius);
		}
	}

	void mousePressEvent(QMouseEvent *e) override { move(e->position()); }
	void mouseMoveEvent(QMouseEvent *e) override
	{
		if (e->buttons() & Qt::LeftButton)
			move(e->position());
	}
	void keyPressEvent(QKeyEvent *e) override
	{
		const int back = stacked_ ? Qt::Key_Up : Qt::Key_Left,
			  forward = stacked_ ? Qt::Key_Down : Qt::Key_Right;
		if (e->key() == back)
			setCut(cut_ - 1);
		else if (e->key() == forward)
			setCut(cut_ + 1);
		else
			QWidget::keyPressEvent(e);
	}

private:
	QRectF imageRect() const
	{
		const QRectF area = QRectF(rect()).adjusted(12, 12, -12, -12);
		const double sc = std::min(area.width() / std::max(1, image_.width()),
					   area.height() / std::max(1, image_.height()));
		const double w = image_.width() * sc, h = image_.height() * sc;
		return QRectF(area.center().x() - w / 2, area.center().y() - h / 2, w, h);
	}
	QRectF partA(const QRectF &box) const
	{
		const double f = cut_ / 100.0;
		return stacked_ ? QRectF(box.left(), box.top(), box.width(), box.height() * f)
				: QRectF(box.left(), box.top(), box.width() * f, box.height());
	}
	QRectF partB(const QRectF &box) const
	{
		const double f = cut_ / 100.0;
		return stacked_
			       ? QRectF(box.left(), box.top() + box.height() * f, box.width(), box.height() * (1.0 - f))
			       : QRectF(box.left() + box.width() * f, box.top(), box.width() * (1.0 - f), box.height());
	}
	void move(const QPointF &pos)
	{
		const QRectF box = imageRect();
		const double f = stacked_ ? (pos.y() - box.top()) / std::max(1.0, box.height())
					  : (pos.x() - box.left()) / std::max(1.0, box.width());
		setCut(int(std::lround(f * 100.0)));
	}

	QImage image_;
	int cut_ = 30;
	bool stacked_ = false;
	bool markSecond_ = false;
};

LogoPartsDialog::LogoPartsDialog(const QString &logoPath, int cut, bool stacked, bool markSecond, QWidget *parent)
	: QDialog(parent)
{
	setWindowTitle(lt("Laycue.Titles.LogoPartsTitle"));
	setModal(true);
	setObjectName("LaycueContent");
	setStyleSheet(panelStyleSheet());
	setMinimumWidth(380);
	cut_ = cut;

	auto *v = new QVBoxLayout(this);
	v->setContentsMargins(size::margin + 4, size::margin + 4, size::margin + 4, size::margin + 4);
	v->setSpacing(size::gapSmall + 2);
	v->addWidget(innerLabel(this, lt("Laycue.Titles.LogoPartsTitle"), 16, 700, color::cream));
	v->addWidget(innerLabel(this, lt("Laycue.Titles.LogoPartsHelp"), 12, 400, color::textSecondary));

	QImage image(logoPath);
	if (image.isNull()) {
		v->addWidget(innerLabel(this,
					lt(logoPath.isEmpty() ? "Laycue.Titles.LogoPartsNeedLogo"
							      : "Laycue.Titles.LogoPartsCantRead"),
					13, 600, color::overlay));
		auto *close = new ActionButton(lt("Laycue.Titles.Cancel"), ActionButton::Kind::Secondary, this);
		connect(close, &QAbstractButton::clicked, this, &QDialog::reject);
		v->addWidget(close);
		return;
	}
	image = image.convertToFormat(QImage::Format_ARGB32_Premultiplied);

	view_ = new PartsView(image, this);
	view_->setStacked(stacked);
	view_->setMarkSecond(markSecond);
	view_->setCut(cut > 0 ? cut : (stacked ? 50 : 30));
	v->addWidget(view_, 1);

	orientation_ = new Segmented(this);
	orientation_->setItems({lt("Laycue.Titles.LogoPartsSideBySide"), lt("Laycue.Titles.LogoPartsStacked")});
	orientation_->setCurrent(stacked ? 1 : 0);
	orientation_->setAccessibleName(lt("Laycue.Titles.LogoStacked"));
	connect(orientation_, &Segmented::changed, this, [this](int i) {
		view_->setStacked(i == 1);
		view_->setCut(i == 1 ? 50 : 30);
		refreshChoices();
	});
	v->addWidget(orientation_);

	which_ = new Segmented(this);
	which_->setAccessibleName(lt("Laycue.Titles.LogoMarkSecond"));
	connect(which_, &Segmented::changed, this, [this](int i) { view_->setMarkSecond(i == 1); });
	v->addWidget(which_);
	refreshChoices();
	which_->setCurrent(markSecond ? 1 : 0);

	auto *row = new QHBoxLayout();
	row->setSpacing(size::gapSmall);
	auto *onePiece = new ActionButton(lt("Laycue.Titles.LogoPartsOnePiece"), ActionButton::Kind::Secondary, this);
	connect(onePiece, &QAbstractButton::clicked, this, [this]() {
		cut_ = 0;
		accept();
	});
	row->addWidget(onePiece, 1);
	auto *use = new ActionButton(lt("Laycue.Titles.LogoPartsUse"), ActionButton::Kind::Primary, this);
	connect(use, &QAbstractButton::clicked, this, [this]() {
		cut_ = view_->cut();
		accept();
	});
	row->addWidget(use, 1);
	v->addLayout(row);
}

bool LogoPartsDialog::stacked() const
{
	return view_ && view_->stacked();
}

bool LogoPartsDialog::markSecond() const
{
	return view_ && view_->markSecond();
}

void LogoPartsDialog::refreshChoices()
{
	const bool stacked = view_ && view_->stacked();
	which_->setItems(
		stacked ? QStringList{lt("Laycue.Titles.LogoPartsMarkTop"), lt("Laycue.Titles.LogoPartsMarkBottom")}
			: QStringList{lt("Laycue.Titles.LogoPartsMarkLeft"), lt("Laycue.Titles.LogoPartsMarkRight")});
	which_->setCurrent(view_ && view_->markSecond() ? 1 : 0);
}

} // namespace laycue::titles::ui
