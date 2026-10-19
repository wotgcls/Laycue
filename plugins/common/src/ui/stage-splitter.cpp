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
	StageSplitter(QWidget *natural, QWidget *parent) : QSplitter(Qt::Vertical, parent), natural_(natural)
	{
		setChildrenCollapsible(false);
		setHandleWidth(12);
	}

protected:
	QSplitterHandle *createHandle() override { return new GripHandle(orientation(), this); }

	// The first time it has a size, the top gets the height it needs (up to two thirds), the rest goes below.
	void resizeEvent(QResizeEvent *event) override
	{
		QSplitter::resizeEvent(event);
		if (placed_ || height() < 80 || count() < 2)
			return;
		placed_ = true;
		const int total = height() - handleWidth();
		const int want =
			std::clamp(natural_ ? natural_->sizeHint().height() + 4 : total / 2, 80, total * 2 / 3);
		setSizes({want, std::max(80, total - want)});
	}

private:
	QWidget *natural_;
	bool placed_ = false;
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

QSplitter *stageSplitter(QWidget *top, QWidget *bottom, QWidget *parent, bool scrollBottom)
{
	auto *split = new StageSplitter(top, parent);
	split->addWidget(scrollFor(top, split));
	split->addWidget(scrollBottom ? static_cast<QWidget *>(scrollFor(bottom, split)) : bottom);
	split->setStretchFactor(0, 0);
	split->setStretchFactor(1, 1);
	return split;
}

} // namespace laycue::ui
