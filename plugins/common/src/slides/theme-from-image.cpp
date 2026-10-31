#include "slides/theme-from-image.hpp"

#include <QColor>
#include <QImage>

#include <algorithm>
#include <array>
#include <cmath>

namespace laycue::slides {

namespace {

constexpr int kBins = 24;      // hue buckets of 15 degrees
constexpr int kMinPixels = 24; // a picture must carry at least this much colour to theme from
constexpr int kSample = 56;    // the picture is squeezed to this square before reading

struct Bin {
	double weight = 0.0; // how much colour lives here
	double sumS = 0.0;   // weighted sums, so the bin's own saturation comes back out
	double sumV = 0.0;
};

// One pixel's say in the theme: strong, mid-bright colour counts most; grey, near-black and blown-white
// count for nothing — a grey photo must not make a grey slide.
double pixelWeight(int alpha, int saturation, int value)
{
	if (alpha < 40)
		return 0.0;
	if (saturation < 12 || value < 12 || value > 248)
		return 0.0;
	const double s = saturation / 255.0, v = value / 255.0;
	return s * s * v * (1.0 - std::abs(v - 0.55) * 0.9); // mid-bright colour speaks loudest
}

// A hue's opposite on the wheel, kept inside 0..360.
double across(double hue)
{
	return std::fmod(hue + 180.0, 360.0);
}

// The distance between two hues, around the wheel (never more than 180).
double hueGap(double a, double b)
{
	double d = std::abs(a - b);
	return d > 180.0 ? 360.0 - d : d;
}

// Sits a hue at a chosen depth: saturation and value clamped to the band a broadcast background needs —
// dark enough that white words read, rich enough that the colour is really there.
QColor shade(double hue, double satLow, double satHigh, double valLow, double valHigh, double f)
{
	const double s = satLow + (satHigh - satLow) * f;
	const double v = valLow + (valHigh - valLow) * f;
	return QColor::fromHsv(int(hue / 360.0 * 359.0) % 360, int(s * 255.0 + 0.5), int(v * 255.0 + 0.5));
}

// The fallback when a picture has no colour worth reading: the deep night blue and warm gold Laycue
// already wears — still a choice, never a shrug.
ImageTheme defaultTheme()
{
	ImageTheme t;
	t.ok = true;
	t.color1 = 0x0C101C;
	t.color2 = 0x1B2A4A;
	t.color3 = 0x3E5F9E;
	t.text = 0xFFFFFF;
	t.accent = 0xE0A91E;
	return t;
}

} // namespace

ImageTheme themeFromImage(const QString &pictureFile)
{
	QImage full(pictureFile);
	if (full.isNull())
		return {};
	const QImage small = full.scaled(kSample, kSample, Qt::IgnoreAspectRatio, Qt::SmoothTransformation);
	if (small.isNull())
		return {};

	std::array<Bin, kBins> bins{};
	double total = 0.0;
	for (int y = 0; y < small.height(); ++y) {
		for (int x = 0; x < small.width(); ++x) {
			const QColor c = small.pixelColor(x, y);
			const double w = pixelWeight(c.alpha(), c.saturation(), c.value());
			if (w <= 0.0)
				continue;
			const double hue = std::fmod(c.hsvHue() < 0 ? 0.0 : c.hsvHue(), 360.0);
			const int bin = int(hue / (360.0 / kBins)) % kBins;
			bins[bin].weight += w;
			bins[bin].sumS += w * c.saturation() / 255.0;
			bins[bin].sumV += w * c.value() / 255.0;
			total += w;
		}
	}
	if (total < kMinPixels)
		return defaultTheme(); // a nearly grey picture: the house colours, not a grey slide

	// The strongest colour family, and its runner-up: the next-best bin that sits at least 45 degrees
	// from the winner, so the third colour is a companion, not a twin.
	int best = 0;
	for (int i = 1; i < kBins; ++i)
		if (bins[i].weight > bins[best].weight)
			best = i;
	int second = -1;
	for (int i = 0; i < kBins; ++i) {
		if (i == best || bins[i].weight < total * 0.08)
			continue;
		const double gap = hueGap(i * (360.0 / kBins), best * (360.0 / kBins));
		if (gap < 45.0)
			continue;
		if (second < 0 || bins[i].weight > bins[second].weight)
			second = i;
	}
	if (bins[best].weight < total * 0.06)
		return defaultTheme(); // colour everywhere but in no family: the house colours hold

	const double hue = (best + 0.5) * (360.0 / kBins);
	const double hueS = bins[best].sumS / bins[best].weight; // the picture's own saturation and value
	const double hueV = bins[best].sumV / bins[best].weight;
	const double secondHue = second >= 0 ? (second + 0.5) * (360.0 / kBins) : across(hue);

	// The two backgrounds: one family, deep and quiet — the first a shade darker than the second, and
	// the second leans toward the runner-up so the gradient carries a little of the picture's story.
	const double depth = std::clamp(hueV, 0.0, 1.0); // a bright picture sits a little deeper still
	ImageTheme t;
	t.ok = true;
	const QColor c1 = shade(hue, 0.34, 0.58, 0.07, 0.12, depth);
	const QColor c2 = shade(hue * 0.72 + secondHue * 0.28, 0.30, 0.62, 0.11, 0.19, 1.0 - depth * 0.5);
	t.color1 = (uint32_t(c1.red()) << 16) | (uint32_t(c1.green()) << 8) | uint32_t(c1.blue());
	t.color2 = (uint32_t(c2.red()) << 16) | (uint32_t(c2.green()) << 8) | uint32_t(c2.blue());

	// The third colour of the moving glow: the runner-up at full voice, or the accent hue's bright
	// neighbour when the picture offered nothing.
	const QColor c3 = shade(hueS > 0.35 && second >= 0 ? secondHue : hue + 30.0, 0.45, 0.65, 0.55, 0.72,
				hueS > 0.35 ? 0.8 : 0.5);
	t.color3 = (uint32_t(c3.red()) << 16) | (uint32_t(c3.green()) << 8) | uint32_t(c3.blue());

	// The accent: the winning family, bright and confident — the line and the reference name.
	const QColor acc = shade(hue, 0.55, 0.85, 0.62, 0.85, hueS > 0.4 ? 0.85 : 0.55);
	t.accent = (uint32_t(acc.red()) << 16) | (uint32_t(acc.green()) << 8) | uint32_t(acc.blue());

	// The words: near-white warmed toward the accent, so they belong to the theme instead of sitting
	// on it. Dark words never survive a themed background, so text is always light here.
	const QColor txt = shade(hue, 0.04, 0.10, 0.94, 0.99, 0.5);
	t.text = (uint32_t(txt.red()) << 16) | (uint32_t(txt.green()) << 8) | uint32_t(txt.blue());
	return t;
}

void applyTheme(SlideStyle &style, const ImageTheme &theme)
{
	if (!theme.ok)
		return;
	style.color1 = theme.color1;
	style.color2 = theme.color2;
	style.color3 = theme.color3;
	style.text = theme.text;
	style.accent = theme.accent;
	if (style.fill == int(Fill::Moving)) // the theme's two colours are a gradient's; moving colours drift
		style.fill = int(Fill::Gradient);
}

} // namespace laycue::slides
