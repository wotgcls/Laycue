// One layout template drawn as a small picture of the screen with its parts. Used on the Layouts tab and in
// the New layout page, so a shape looks the same wherever it is offered.
#pragma once

#include "core/layouts.hpp"
#include "ui/labels.hpp"
#include "ui/tokens.hpp"

#include <QAbstractButton>
#include <QEnterEvent>
#include <QPainter>

namespace laycue::ui {

class TemplateCard : public QAbstractButton {
public:
	TemplateCard(const LayoutTemplate &layout, QWidget *parent) : QAbstractButton(parent), layout_(layout)
	{
		setCheckable(true);
		setCursor(Qt::PointingHandCursor);
		setFocusPolicy(Qt::TabFocus);
		setFixedHeight(96);
		setAccessibleName(lt(layout.nameKey));
		setToolTip(lt(layout.hintKey));
	}

	QSize sizeHint() const override { return {110, 96}; }
	QSize minimumSizeHint() const override { return {78, 96}; }

protected:
	void enterEvent(QEnterEvent *) override
	{
		hovered_ = true;
		update();
	}
	void leaveEvent(QEvent *) override
	{
		hovered_ = false;
		update();
	}
	void paintEvent(QPaintEvent *) override
	{
		QPainter p(this);
		p.setRenderHint(QPainter::Antialiasing);
		const QRectF outer = QRectF(rect()).adjusted(1, 1, -1, -1);
		p.setPen(Qt::NoPen);
		p.setBrush(isChecked() ? color::stage : hovered_ ? color::graphiteHover : color::graphite);
		p.drawRoundedRect(outer, size::radius, size::radius);
		if (isChecked()) {
			p.setBrush(Qt::NoBrush);
			p.setPen(QPen(color::setup, 2));
			p.drawRoundedRect(outer, size::radius, size::radius);
		}

		// The screen.
		const double pad = 8;
		const double h = outer.height() - 2 * pad - 20;
		const double w = std::min(outer.width() - 2 * pad, h * 16 / 9);
		const QRectF screen(outer.x() + (outer.width() - w) / 2, outer.y() + pad, w, w * 9 / 16);
		p.setPen(Qt::NoPen);
		p.setBrush(color::recess);
		p.drawRoundedRect(screen, 4, 4);
		const double inset = 2;
		for (size_t i = 0; i < layout_.shapes.size(); ++i) {
			const SlotShape &s = layout_.shapes[i];
			const QRectF r(screen.x() + s.x * screen.width() + inset,
				       screen.y() + s.y * screen.height() + inset, s.w * screen.width() - 2 * inset,
				       s.h * screen.height() - 2 * inset);
			// The first slot is the main one; later slots are quieter.
			p.setBrush(i == 0 ? color::cream : color::textTertiary);
			p.drawRoundedRect(r, 3, 3);
		}

		p.setPen(isChecked() ? color::cream : color::textSecondary);
		p.setFont(uiFont(11, 600));
		p.drawText(QRectF(outer.x() + 4, screen.bottom() + 3, outer.width() - 8, 22), Qt::AlignCenter,
			   lt(layout_.nameKey));
	}

private:
	const LayoutTemplate &layout_;
	bool hovered_ = false;
};

} // namespace laycue::ui
