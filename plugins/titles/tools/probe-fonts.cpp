#include "render/font-set.hpp"
#include <QGuiApplication>
#include <QFontDatabase>
#include <QFontInfo>
#include <cstdio>
int main(int argc, char **argv)
{
	QGuiApplication app(argc, argv);
	laycue::titles::FontSet fonts(argv[1]);
	std::printf("loaded %d\n", fonts.loadedCount());
	for (int i = 0; i < 8; ++i) {
		QFont f = fonts.font(static_cast<laycue::titles::FontFace>(i), 40);
		std::printf("%d family='%s' style='%s' weight=%d italic=%d exact=%d\n", i, f.family().toUtf8().constData(),
			    f.styleName().toUtf8().constData(), int(f.weight()), f.italic(), QFontInfo(f).family() == f.family());
	}
}
