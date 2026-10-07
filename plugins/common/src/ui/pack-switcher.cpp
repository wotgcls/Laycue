#include "ui/pack-switcher.hpp"

#include "ui/tokens.hpp"

#include <obs-frontend-api.h>
#include <obs-module.h>

#include <QDockWidget>
#include <QFont>
#include <QFontMetrics>
#include <QVariant>
#include <QMainWindow>
#include <QMouseEvent>
#include <QPainter>

#include <algorithm>

namespace laycue::ui {

namespace {

QDockWidget *dockAround(QWidget *w)
{
	for (QWidget *p = w; p; p = p->parentWidget()) {
		if (auto *d = qobject_cast<QDockWidget *>(p))
			return d;
	}
	return nullptr;
}

constexpr int kHeight = 24;

} // namespace

PackSwitcher::PackSwitcher(QWidget *parent) : QWidget(parent)
{
	setFixedHeight(kHeight);
	setCursor(Qt::PointingHandCursor);
}

QSize PackSwitcher::sizeHint() const
{
	return QSize(200, kHeight);
}

void PackSwitcher::showEvent(QShowEvent *event)
{
	QWidget::showEvent(event);
	layoutChips();
}

void PackSwitcher::layoutChips()
{
	// Every Laycue dock lives under OBS's main window; find them by the marker the menu code sets.
	chips_.clear();
	QWidget *self = dockAround(this);
	auto *window = qobject_cast<QMainWindow *>(static_cast<QWidget *>(obs_frontend_get_main_window()));
	if (!window)
		return;
	for (QDockWidget *dock : window->findChildren<QDockWidget *>()) {
		const QVariant name = dock->property("laycuePackName");
		if (!name.isValid() || !dock->widget())
			continue;
		chips_.push_back({name.toString(), dock->widget(), QRect(), dock == self});
	}
	std::sort(chips_.begin(), chips_.end(), [](const Chip &a, const Chip &b) { return a.name < b.name; });
	QFont font = this->font();
	font.setPixelSize(12);
	font.setWeight(QFont::DemiBold);
	const QFontMetrics fm(font);
	int x = size::shadowMargin;
	for (Chip &chip : chips_) {
		const int w = fm.horizontalAdvance(chip.name) + 20;
		chip.rect = QRect(x, 0, w, kHeight);
		x += w + 4;
	}
	update();
}

void PackSwitcher::paintEvent(QPaintEvent *)
{
	QPainter p(this);
	p.setRenderHint(QPainter::Antialiasing);
	QFont font = this->font();
	font.setPixelSize(12);
	font.setWeight(QFont::DemiBold);
	p.setFont(font);
	for (const Chip &chip : chips_) {
		if (chip.current) {
			p.setPen(Qt::NoPen);
			p.setBrush(color::control);
			p.drawRoundedRect(chip.rect, 6, 6);
		}
		p.setPen(chip.current ? color::cream : color::textSecondary);
		p.drawText(chip.rect, Qt::AlignCenter, chip.name);
	}
}

void PackSwitcher::mousePressEvent(QMouseEvent *event)
{
	try {
		for (const Chip &chip : chips_) {
			if (!chip.rect.contains(event->pos()) || chip.current || !chip.content)
				continue;
			QDockWidget *target = dockAround(chip.content);
			QDockWidget *self = dockAround(this);
			auto *window =
				qobject_cast<QMainWindow *>(static_cast<QWidget *>(obs_frontend_get_main_window()));
			if (!target || !self || !window)
				return;
			if (!target->isVisible() && !self->isFloating() && !target->isFloating())
				window->tabifyDockWidget(self, target); // sits beside this panel as a tab
			target->show();
			target->raise();
			return;
		}
	} catch (...) {
		blog(LOG_ERROR, "Laycue: could not switch panels");
	}
}

} // namespace laycue::ui
