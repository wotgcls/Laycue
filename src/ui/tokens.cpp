#include "ui/tokens.hpp"

#include <obs-module.h>
#include <plugin-support.h>

#include <QFontDatabase>
#include <QStringList>

namespace laycue::ui {

namespace {

// Loads one font file from the plugin's data folder; returns its family name or "".
QString loadFont(const char *file)
{
	char *path = obs_module_file(file);
	if (!path) {
		obs_log(LOG_WARNING, "Font not found: %s", file);
		return QString();
	}
	const int id = QFontDatabase::addApplicationFont(QString::fromUtf8(path));
	bfree(path);
	if (id < 0) {
		obs_log(LOG_WARNING, "Font could not be loaded: %s", file);
		return QString();
	}
	const QStringList families = QFontDatabase::applicationFontFamilies(id);
	return families.isEmpty() ? QString() : families.first();
}

struct BrandFonts {
	QString ui;
	QString mono;
};

// Loaded once, the first time it is needed, and never changed afterwards.
const BrandFonts &brandFonts()
{
	static const BrandFonts fonts{loadFont("fonts/Archivo.ttf"), loadFont("fonts/JetBrainsMono.ttf")};
	return fonts;
}

} // namespace

void loadBrandFonts()
{
	brandFonts();
}

QFont uiFont(int pixelSize, int weight)
{
	QFont font;
	if (!brandFonts().ui.isEmpty())
		font.setFamily(brandFonts().ui);
	font.setPixelSize(pixelSize);
	font.setWeight(static_cast<QFont::Weight>(weight));
	return font;
}

QString uiFamily()
{
	return brandFonts().ui;
}

QFont monoFont(int pixelSize)
{
	QFont font;
	font.setFamily(brandFonts().mono.isEmpty() ? QStringLiteral("monospace") : brandFonts().mono);
	font.setStyleHint(QFont::Monospace);
	font.setPixelSize(pixelSize);
	font.setWeight(QFont::Medium);
	return font;
}

QString hex(const QColor &color)
{
	return color.name(QColor::HexRgb);
}

} // namespace laycue::ui
