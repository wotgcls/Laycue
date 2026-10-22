// Draws every Laycue Clock design (and each background) to PNG files so they can be looked at without OBS.
//   g++ -std=c++17 -fPIC plugins/clock/tools/preview.cpp plugins/clock/src/render/clock-renderer.cpp \
//     plugins/clock/src/core/clock-core.cpp -Iplugins/clock/src $(pkg-config --cflags --libs Qt6Gui) -o clock-preview
//   QT_QPA_PLATFORM=offscreen ./clock-preview plugins/clock/data/fonts out_folder
#include "render/clock-renderer.hpp"

#include <QDir>
#include <QGuiApplication>
#include <QLinearGradient>
#include <QPainter>
#include <QRadialGradient>

using namespace laycue::clock;

static QImage picture(int w, int h)
{
	QImage img(w, h, QImage::Format_ARGB32_Premultiplied);
	QPainter p(&img);
	QLinearGradient g(0, 0, 0, h);
	g.setColorAt(0, QColor(70, 90, 130));
	g.setColorAt(1, QColor(30, 34, 52));
	p.fillRect(0, 0, w, h, g);
	QRadialGradient glow(w * 0.6, h * 0.35, w * 0.4);
	glow.setColorAt(0, QColor(255, 236, 190, 150));
	glow.setColorAt(1, QColor(255, 236, 190, 0));
	p.fillRect(0, 0, w, h, glow);
	return img;
}

int main(int argc, char **argv)
{
	QGuiApplication app(argc, argv);
	if (argc < 3)
		return 1;
	ClockFonts fonts(QString::fromLocal8Bit(argv[1]));
	const QString out = QString::fromLocal8Bit(argv[2]);
	QDir().mkpath(out);
	for (int d = 0; d < kDesignCount; ++d) {
		for (int bg = 0; bg < 3; ++bg) {
			for (int lv = 0; lv < 3; ++lv) {
				if (lv > 0 && bg > 0)
					continue;
				RenderRequest r;
				r.design = Design(d);
				r.background = Background(bg);
				r.label = QStringLiteral("Service starts in");
				r.time = lv == 2 ? QStringLiteral("+00:12") : QStringLiteral("04:32");
				r.level = lv == 0 ? Level::Normal : lv == 1 ? Level::Warning : Level::Over;
				r.fraction = 0.62;
				const DesignDefaults def = defaultsFor(Design(d));
				r.align = def.align;
				r.vertical = def.vertical;
				QImage pic = picture(1920, 1080);
				const Rendered rendered = renderClock(fonts, r);
				QPainter p(&pic);
				p.drawImage(rendered.x, rendered.y, rendered.image);
				p.end();
				pic.save(QString("%1/d%2-bg%3-lv%4.png").arg(out).arg(d).arg(bg).arg(lv));
			}
		}
	}
	return 0;
}
