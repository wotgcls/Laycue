#include "ui/stage-splitter.hpp"

#include "ui/tokens.hpp"

#include <QEnterEvent>
#include <QFrame>
#include <QPainter>
#include <QScrollArea>
#include <QShowEvent>
#include <QSplitterHandle>

#include <algorithm>

namespace laycue::ui {

namespace {

// The bar: a short rounded line in the middle, so it reads as something to drag.
class GripHandle : public QSplitterHandle {
public:
	GripHandle(Qt::Orientation orientation, QSplitter *parent) : QSplitterHandle(orientation, parent)
	{
		setCursor(Qt::SizeVerCursor);
		setToolTip(QStringLiteral("Drag to give the templates more room, or less"));
	}

protected:
	void paintEvent(QPaintEvent *) override
	{
		QPainter p(this);
		p.setRenderHint(QPainter::Antialiasing, true);
		p.setPen(QPen(underMouse() ? color::cream : color::textTertiary, 3, Qt::SolidLine, Qt::RoundCap));
		const int cx = width() / 2, cy = height() / 2;
		p.drawLine(cx - 20, cy, cx + 20, cy);
	}
	void enterEvent(QEnterEvent *event) override
	{
		QSplitterHandle::enterEvent(event);
		update();
	}
	void leaveEvent(QEvent *event) override
	{
		QSplitterHandle::leaveEvent(event);
		update();
	}
};

class StageSplitter : public QSplitter {
public:
	StageSplitter(QWidget *natural, double maxShare, QWidget *parent)
		: QSplitter(Qt::Vertical, parent),
		  natural_(natural),
		  maxShare_(maxShare)
	{
		setChildrenCollapsible(false);
		setHandleWidth(12);
		connect(this, &QSplitter::splitterMoved, this, [this]() { userMoved_ = true; });
	}

protected:
	QSplitterHandle *createHandle() override { return new GripHandle(orientation(), this); }

	// The top gets the height it needs (up to its share), the rest goes below.
	void resizeEvent(QResizeEvent *event) override
	{
		QSplitter::resizeEvent(event);
		// Until the bar is dragged by hand, the split follows the panel's size (a panel often starts small and
		// is then given its real height); after that it is the operator's.
		if (userMoved_ || height() < 80 || count() < 2)
			return;
		const int total = height() - handleWidth();
		const int most = std::max(80, int(total * maxShare_));
		const int want = std::clamp(natural_ ? natural_->sizeHint().height() + 4 : total / 2, 80, most);
		setSizes({want, std::max(80, total - want)});
	}

private:
	QWidget *natural_;
	double maxShare_;
	bool userMoved_ = false;
};

QScrollArea *scrollFor(QWidget *inside, QWidget *parent)
{
	auto *scroll = new QScrollArea(parent);
	scroll->setWidgetResizable(true);
	scroll->setFrameShape(QFrame::NoFrame);
	scroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
	scroll->setWidget(inside);
	scroll->viewport()->setAutoFillBackground(false);
	scroll->setStyleSheet(QStringLiteral("QScrollArea { background: transparent; }"));
	return scroll;
}

} // namespace

QSplitter *stageSplitter(QWidget *top, QWidget *bottom, QWidget *parent, bool scrollBottom, double maxTopShare)
{
	auto *split = new StageSplitter(top, std::clamp(maxTopShare, 0.2, 0.9), parent);
	split->addWidget(scrollFor(top, split));
	split->addWidget(scrollBottom ? static_cast<QWidget *>(scrollFor(bottom, split)) : bottom);
	split->setStretchFactor(0, 0);
	split->setStretchFactor(1, 1);
	return split;
}

} // namespace laycue::ui
