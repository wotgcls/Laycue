#include "render/font-set.hpp"

#include <QDir>
#include <QFontDatabase>
#include <QStringList>

namespace laycue::titles {

namespace {

struct FileSpec {
	FontFace face;
	const char *file;
	const char *style; // the style name inside the family, as the font file calls it
};

const FileSpec kFiles[] = {
	{FontFace::BarlowCondensedMedium, "BarlowCondensed-Medium.ttf", "Medium"},
	{FontFace::BarlowCondensedSemiBold, "BarlowCondensed-SemiBold.ttf", "SemiBold"},
	{FontFace::BarlowCondensedBold, "BarlowCondensed-Bold.ttf", "Bold"},
	{FontFace::BarlowRegular, "Barlow-Regular.ttf", "Regular"},
	{FontFace::BarlowSemiBold, "Barlow-SemiBold.ttf", "SemiBold"},
	{FontFace::BebasNeue, "BebasNeue-Regular.ttf", "Regular"},
	{FontFace::Baskerville, "LibreBaskerville.ttf", "Regular"},
	{FontFace::BaskervilleItalic, "LibreBaskerville-Italic.ttf", "Italic"},
};

} // namespace

FontSet::FontSet(const QString &folder)
{
	const QDir dir(folder);
	for (const FileSpec &spec : kFiles) {
		const int id = QFontDatabase::addApplicationFont(dir.filePath(QString::fromLatin1(spec.file)));
		if (id < 0)
			continue;
		const QStringList families = QFontDatabase::applicationFontFamilies(id);
		if (families.isEmpty())
			continue;
		Entry &entry = entries_[static_cast<size_t>(spec.face)];
		entry.family = families.first();
		entry.style = QString::fromLatin1(spec.style);
		++loaded_;
	}
}

QFont FontSet::font(FontFace face, double pixelSize) const
{
	const Entry &entry = entries_[static_cast<size_t>(face)];
	QFont font;
	if (!entry.family.isEmpty()) {
		font = QFontDatabase::font(entry.family, entry.style, 12);
		if (font.family().isEmpty())
			font = QFont(entry.family);
	}
	font.setPixelSize(static_cast<int>(pixelSize + 0.5));
	font.setHintingPreference(QFont::PreferNoHinting); // smooth, resolution-independent glyph placement
	font.setStyleStrategy(QFont::PreferAntialias);
	return font;
}

namespace {
const FontSet *g_shared = nullptr;
}

const FontSet *sharedFonts()
{
	return g_shared;
}

void setSharedFonts(const FontSet *fonts)
{
	g_shared = fonts;
}

} // namespace laycue::titles
