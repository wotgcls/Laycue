#include "ui/brand-mark.hpp"

#include <obs-module.h>

#include <QGuiApplication>
#include <QLabel>
#include <QPalette>
#include <QPixmap>

namespace laycue::ui {

QLabel *brandMark(QWidget *parent)
{
	auto *mark = new QLabel(parent);
	const bool darkTheme = parent->palette().color(QPalette::Window).lightness() < 128;
	char *path = obs_module_file(darkTheme ? "brand/laycue-mark-on-dark.png" : "brand/laycue-mark-on-light.png");
	if (!path)
		return mark;
	QPixmap image(QString::fromUtf8(path));
	bfree(path);
	if (image.isNull())
		return mark;
	const qreal ratio = qGuiApp ? qGuiApp->devicePixelRatio() : 1.0;
	QPixmap scaled = image.scaledToHeight(qRound(19 * ratio), Qt::SmoothTransformation);
	scaled.setDevicePixelRatio(ratio);
	mark->setPixmap(scaled);
	mark->setAccessibleName(QStringLiteral("Laycue"));
	return mark;
}

} // namespace laycue::ui
