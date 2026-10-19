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
#include <QPalette>
#include <QPainter>
#include <QPointer>
#include <QTimer>
#include <QVBoxLayout>

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

constexpr int kHeader = 30; // the Laycue mark and name
constexpr int kChips = 24;  // the row of panel names
constexpr int kHeight = kHeader + kChips;

} // namespace

PackSwitcher::PackSwitcher(QWidget *parent) : QWidget(parent)
{
	setFixedHeight(kHeight);
	setCursor(Qt::PointingHandCursor);
	const bool darkTheme = palette().color(QPalette::Window).lightness() < 128;
	char *path = obs_module_file(darkTheme ? "brand/laycue-mark-on-dark.png" : "brand/laycue-mark-on-light.png");
	if (path) {
		mark_ = QPixmap(QString::fromUtf8(path));
		bfree(path);
	}
	setAccessibleName(QStringLiteral("Laycue"));
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
		chip.rect = QRect(x, kHeader, w, kChips);
		x += w + 4;
	}
	update();
}

void PackSwitcher::paintEvent(QPaintEvent *)
{
	QPainter p(this);
	p.setRenderHint(QPainter::Antialiasing);
	p.setRenderHint(QPainter::SmoothPixmapTransform);
	// The Laycue mark and name.
	int x = size::shadowMargin + 4;
	if (!mark_.isNull()) {
		const int h = 22;
		const int w = mark_.height() > 0 ? mark_.width() * h / mark_.height() : h;
		p.drawPixmap(QRect(x, (kHeader - h) / 2, w, h), mark_);
		x += w + 8;
	}
	QFont title = this->font();
	title.setPixelSize(17);
	title.setWeight(QFont::Bold);
	p.setFont(title);
	p.setPen(color::cream);
	p.drawText(QRect(x, 0, width() - x, kHeader), Qt::AlignVCenter | Qt::AlignLeft, QStringLiteral("Laycue"));
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
			if (target->isVisible()) {
				// Already open somewhere (the operator put it there): just bring it forward.
				target->raise();
				target->activateWindow();
				return;
			}
			// Not open: it takes this panel's exact place, so there is no new window and no new tab.
			if (self->isFloating()) {
				target->setFloating(true);
				target->resize(self->size());
				target->move(self->pos());
			} else {
				target->setFloating(false); // it may have been floating when it was last closed
				window->tabifyDockWidget(self, target);
			}
			target->show();
			target->raise();
			QPointer<QDockWidget> leaving(self);
			// Hidden after the click is handled; a hidden panel leaves the tab group, so no tab bar appears.
			QTimer::singleShot(0, target, [leaving]() {
				if (leaving)
					leaving->hide();
			});
			return;
		}
	} catch (...) {
		blog(LOG_ERROR, "Laycue: could not switch panels");
	}
}

QWidget *packHeader(QWidget *parent)
{
	auto *strip = new QWidget(parent);
	auto *v = new QVBoxLayout(strip);
	v->setContentsMargins(size::margin - size::shadowMargin, 6, size::margin - size::shadowMargin, 2);
	v->setSpacing(0);
	v->addWidget(new PackSwitcher(strip));
	return strip;
}

} // namespace laycue::ui
