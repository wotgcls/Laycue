// titles-preview <out folder> [template id]: renders each template (or one) to PNG files:
//   <id>-in.png (halfway through arriving), <id>-hold.png (on air), <id>-out.png (halfway out),
// and sheet.png, a contact sheet of every template on air over a sample picture.
#include "core/title-clock.hpp"
#include "core/title-templates.hpp"
#include "render/font-set.hpp"
#include "render/title-renderer.hpp"

#include <QDir>
#include <QFile>
#include <QGuiApplication>
#include <QImage>
#include <QLinearGradient>
#include <QPainter>
#include <QRadialGradient>
#include <QTextStream>

#include <QElapsedTimer>
#include <algorithm>
#include <cstdio>
#include <map>

using namespace laycue::titles;

namespace {

std::map<QString, QString> loadLocale(const QString &path)
{
	std::map<QString, QString> values;
	QFile file(path);
	if (!file.open(QIODevice::ReadOnly | QIODevice::Text))
		return values;
	QTextStream in(&file);
	while (!in.atEnd()) {
		const QString line = in.readLine();
		const int eq = line.indexOf('=');
		if (line.startsWith(';') || eq < 0)
			continue;
		QString value = line.mid(eq + 1).trimmed();
		if (value.startsWith('"') && value.endsWith('"') && value.size() >= 2)
			value = value.mid(1, value.size() - 2);
		values[line.left(eq).trimmed()] = value;
	}
	return values;
}

// A stand-in for a camera picture: stage-like light on a dark room, so panel-less text is judged on
// something that is not flat black.
QImage samplePicture(int w, int h, int variant)
{
	QImage img(w, h, QImage::Format_ARGB32_Premultiplied);
	QPainter p(&img);
	QLinearGradient base(0, 0, 0, h);
	if (variant == 0) {
		base.setColorAt(0.0, QColor(54, 62, 84));
		base.setColorAt(1.0, QColor(22, 24, 34));
	} else {
		base.setColorAt(0.0, QColor(196, 168, 120));
		base.setColorAt(1.0, QColor(96, 70, 52));
	}
	p.fillRect(0, 0, w, h, base);
	QRadialGradient glow(w * 0.62, h * 0.35, w * 0.45);
	glow.setColorAt(0.0, QColor(255, 236, 190, 120));
	glow.setColorAt(1.0, QColor(255, 236, 190, 0));
	p.fillRect(0, 0, w, h, glow);
	p.setPen(Qt::NoPen);
	p.setBrush(QColor(0, 0, 0, 60));
	p.drawEllipse(QPointF(w * 0.62, h * 0.95), w * 0.3, h * 0.22); // a pulpit-ish shadow
	return img;
}

QString text(const std::map<QString, QString> &loc, const QString &key)
{
	const auto it = loc.find(key);
	return it == loc.end() ? QString() : it->second;
}

} // namespace

int main(int argc, char **argv)
{
	QGuiApplication app(argc, argv);
	if (argc > 1 && QString::fromLocal8Bit(argv[1]) == "--bench") {
		// How long one frame takes to draw: every template on air, then tickers for 120 scrolling frames.
		FontSet benchFonts(QString::fromLocal8Bit(TITLES_DATA_DIR) + "/fonts");
		for (const TitleTemplate &tpl : builtInTemplates()) {
			RenderRequest req;
			req.tpl = tpl;
			req.text.line1 = "Pastor Grace Okafor, welcome to our Sunday celebration of faith and family";
			req.text.line2 = "Senior Pastor";
			QElapsedTimer t;
			t.start();
			const int frames = 120;
			for (int i = 0; i < frames; ++i) {
				req.frame = {1.0, 0.0, double(i * 6)};
				renderTitle(benchFonts, req);
			}
			std::printf("%-16s %.2f ms/frame\n", tpl.id.c_str(), t.nsecsElapsed() / 1e6 / frames);
		}
		return 0;
	}
	if (argc > 2 && QString::fromLocal8Bit(argv[1]) == "--phases") {
		// Each listed repeating template at eight points of its repeat: phase-<id>-<n>.png (cropped top-left)
		const QString dir = QString::fromLocal8Bit(argv[2]);
		QDir().mkpath(dir);
		const QString dataDir = QString::fromLocal8Bit(TITLES_DATA_DIR);
		FontSet fonts(dataDir + "/fonts");
		for (int a = 3; a < argc; ++a) {
			const TitleTemplate *tpl = findTemplate(argv[a]);
			if (!tpl || (tpl->pulseSeconds <= 0.0 && tpl->tickerSpeed <= 0.0))
				continue;
			const int steps = std::max(2, qEnvironmentVariable("LAYCUE_PREVIEW_PHASES", "8").toInt());
			for (int n = 0; n < steps; ++n) {
				RenderRequest req;
				req.tpl = *tpl;
				req.text.line1 = qEnvironmentVariable("LAYCUE_PREVIEW_L1");
				req.text.line2 = qEnvironmentVariable("LAYCUE_PREVIEW_L2");
				req.text.tag = qEnvironmentVariable("LAYCUE_PREVIEW_TAG");
				req.text.logoPath = qEnvironmentVariable("LAYCUE_PREVIEW_LOGO");
				req.text.logoCut = qEnvironmentVariable("LAYCUE_PREVIEW_CUT").toInt();
				req.text.logoStacked = qEnvironmentVariable("LAYCUE_PREVIEW_STACKED") == "1";
				req.text.logoMarkSecond = qEnvironmentVariable("LAYCUE_PREVIEW_MARK2") == "1";
				req.frame = {1.0, 0.0,
					     tpl->pulseSeconds > 0.0 ? tpl->pulseSeconds * 100.0 * (n + 0.5) / steps
								     : 260.0 * n};
				QImage pic = samplePicture(1920, 1080, 0);
				const RenderedTitle r = renderTitle(fonts, req);
				QPainter p(&pic);
				p.drawImage(r.x, r.y, r.image);
				const QStringList box =
					qEnvironmentVariable("LAYCUE_PREVIEW_CROP", "40,0,760,300").split(',');
				pic.copy(box[0].toInt(), box[1].toInt(), box[2].toInt(), box[3].toInt())
					.save(QString("%1/phase-%2-%3.png").arg(dir, argv[a]).arg(n));
			}
		}
		return 0;
	}
	if (argc > 2 && QString::fromLocal8Bit(argv[1]) == "--fit") {
		// Each listed template with a short and a long name in each shape-width mode: fit-<id>-<name>-<mode>.png
		const QString dir = QString::fromLocal8Bit(argv[2]);
		QDir().mkpath(dir);
		const QString dataDir = QString::fromLocal8Bit(TITLES_DATA_DIR);
		FontSet fonts(dataDir + "/fonts");
		const auto loc = loadLocale(dataDir + "/locale/en-US.ini");
		const QString names[] = {"Jo Li", "Reverend Doctor Bartholomew Oluwaseun Adeyemi-Thompson"};
		for (int a = 3; a < argc; ++a) {
			const TitleTemplate *tpl = findTemplate(argv[a]);
			if (!tpl)
				continue;
			for (int n = 0; n < 2; ++n) {
				for (int mode = 0; mode < 3; ++mode) {
					RenderRequest req;
					req.tpl = *tpl;
					const QString nk = QString::fromStdString(nameKey(*tpl));
					req.text.line1 = names[n];
					req.text.line2 = text(loc, nk + ".S2");
					req.text.tag = text(loc, nk + ".S3");
					req.fit = mode;
					req.fitWidth = 640;
					QImage pic = samplePicture(1920, 1080, 0);
					const RenderedTitle r = renderTitle(fonts, req);
					QPainter p(&pic);
					p.drawImage(r.x, r.y, r.image);
					pic.save(QString("%1/fit-%2-%3-%4.png").arg(dir, argv[a]).arg(n).arg(mode));
				}
			}
		}
		return 0;
	}
	if (argc > 2 && QString::fromLocal8Bit(argv[1]) == "--placement") {
		// Each listed template at several positions and sizes: placement-<id>-<align>-<percent>.png
		const QString dir = QString::fromLocal8Bit(argv[2]);
		QDir().mkpath(dir);
		const QString dataDir = QString::fromLocal8Bit(TITLES_DATA_DIR);
		FontSet fonts(dataDir + "/fonts");
		const auto loc = loadLocale(dataDir + "/locale/en-US.ini");
		const int combos[][2] = {{0, 100}, {1, 100}, {2, 100}, {1, 150}, {2, 60}, {0, 130}};
		for (int a = 3; a < argc; ++a) {
			const TitleTemplate *tpl = findTemplate(argv[a]);
			if (!tpl)
				continue;
			for (const auto &c : combos) {
				RenderRequest req;
				req.tpl = *tpl;
				const QString nk = QString::fromStdString(nameKey(*tpl));
				req.text.line1 = text(loc, nk + ".S1");
				req.text.line2 = text(loc, nk + ".S2");
				req.text.tag = text(loc, nk + ".S3");
				req.align = c[0];
				req.scale = c[1] / 100.0;
				QImage pic = samplePicture(1920, 1080, 0);
				const RenderedTitle r = renderTitle(fonts, req);
				QPainter p(&pic);
				p.drawImage(r.x, r.y, r.image);
				pic.save(QString("%1/placement-%2-%3-%4.png").arg(dir, argv[a]).arg(c[0]).arg(c[1]));
			}
		}
		return 0;
	}
	if (argc < 2) {
		std::fprintf(stderr, "usage: titles-preview <out folder> [template id]\n");
		return 2;
	}
	const QString out = QString::fromLocal8Bit(argv[1]);
	const QString only = argc > 2 ? QString::fromLocal8Bit(argv[2]) : QString();
	const QString dataDir = QString::fromLocal8Bit(TITLES_DATA_DIR);
	QDir().mkpath(out);

	FontSet fonts(dataDir + "/fonts");
	std::printf("fonts loaded: %d\n", fonts.loadedCount());
	const auto loc = loadLocale(dataDir + "/locale/en-US.ini");

	const int W = 1920, H = 1080;
	const auto &all = builtInTemplates();
	const int cols = 4;
	const int cellW = 640, cellH = 360;
	const int rows = (int(all.size()) + cols - 1) / cols;
	QImage sheet(cols * cellW, rows * cellH, QImage::Format_ARGB32_Premultiplied);
	sheet.fill(QColor(17, 17, 16));
	QPainter sp(&sheet);
	sp.setRenderHint(QPainter::SmoothPixmapTransform, true);

	int index = 0;
	for (const TitleTemplate &tpl : all) {
		const QString id = QString::fromStdString(tpl.id);
		TitleText txt;
		const QString nk = QString::fromStdString(nameKey(tpl));
		txt.line1 = text(loc, nk + ".S1");
		txt.line2 = text(loc, nk + ".S2");
		txt.tag = text(loc, nk + ".S3");
		txt.note = text(loc, nk + ".S4");
		for (int r = 1; r <= 4; ++r) {
			const QString v = text(loc, nk + QString(".R%1V").arg(r));
			if (!v.isEmpty())
				txt.rows.append({text(loc, nk + QString(".R%1L").arg(r)), v});
		}
		txt.logoPath = qEnvironmentVariable("LAYCUE_PREVIEW_LOGO");

		const auto frame = [&](double in, double out, double tick, int variant) {
			RenderRequest req;
			req.tpl = tpl;
			req.text = txt;
			req.frame = {in, out, tick};
			req.canvasWidth = W;
			req.canvasHeight = H;
			QImage pic = samplePicture(W, H, variant);
			const RenderedTitle r = renderTitle(fonts, req);
			QPainter p(&pic);
			p.drawImage(r.x, r.y, r.image);
			return pic;
		};

		const int variant = (index % 3 == 2) ? 1 : 0;
		if (only.isEmpty() || only == id) {
			frame(0.35, 0, 0, variant).save(out + "/" + id + "-in.png");
			frame(1.0, 0, 400, variant).save(out + "/" + id + "-hold.png");
			frame(1.0, 0.4, 400, variant).save(out + "/" + id + "-out.png");
		}
		const QImage hold = frame(1.0, 0, 400, variant)
					    .scaled(cellW, cellH, Qt::IgnoreAspectRatio, Qt::SmoothTransformation);
		sp.drawImage((index % cols) * cellW, (index / cols) * cellH, hold);
		++index;
	}
	sp.end();
	sheet.save(out + "/sheet.png");
	std::printf("wrote %d templates to %s\n", int(all.size()), out.toUtf8().constData());
	return 0;
}
