#include "core/frame-style.hpp"

#include <algorithm>
#include <cmath>

namespace laycue {

Rgb8 averageColor(const std::vector<uint8_t> &p, int width, int height)
{
	if (width <= 0 || height <= 0 || p.size() < size_t(width) * size_t(height) * 4)
		return {128, 128, 128};
	const size_t total = size_t(width) * size_t(height);
	const size_t step = std::max<size_t>(1, total / 40000);
	double r = 0, g = 0, b = 0, weight = 0;
	for (size_t i = 0; i < total; i += step) {
		const double a = p[i * 4 + 3] / 255.0;
		r += p[i * 4] * a;
		g += p[i * 4 + 1] * a;
		b += p[i * 4 + 2] * a;
		weight += a;
	}
	if (weight < 1e-6)
		return {128, 128, 128};
	return {uint8_t(std::lround(r / weight)), uint8_t(std::lround(g / weight)), uint8_t(std::lround(b / weight))};
}

Metal chooseMetal(Rgb8 c)
{
	const double r = c.r / 255.0, g = c.g / 255.0, b = c.b / 255.0;
	const double hi = std::max({r, g, b}), lo = std::min({r, g, b});
	const double lightness = (hi + lo) / 2.0;
	const double delta = hi - lo;
	const double saturation = delta < 1e-9 ? 0.0 : delta / (1.0 - std::fabs(2.0 * lightness - 1.0));
	double hue = 0.0; // degrees
	if (delta > 1e-9) {
		if (hi == r)
			hue = 60.0 * std::fmod((g - b) / delta, 6.0);
		else if (hi == g)
			hue = 60.0 * ((b - r) / delta + 2.0);
		else
			hue = 60.0 * ((r - g) / delta + 4.0);
		if (hue < 0)
			hue += 360.0;
	}
	if (saturation > 0.25 && hue >= 20.0 && hue <= 75.0 && lightness > 0.35)
		return Metal::Silver; // warm yellow and orange: gold would vanish
	if (lightness > 0.72)
		return Metal::Gold; // a bright picture: silver would vanish
	if (saturation < 0.12 && lightness < 0.5)
		return Metal::Silver; // dark grey or black
	return Metal::Gold;
}

MetalTones metalTones(Metal metal)
{
	if (metal == Metal::Silver)
		return {{0xF4, 0xF6, 0xF9}, {0xB4, 0xBA, 0xC4}, {0x80, 0x87, 0x93}};
	return {{0xFF, 0xE9, 0x9A}, {0xE0, 0xA9, 0x1E}, {0xA8, 0x72, 0x0C}};
}

double frameGap(double w, double h)
{
	return std::round(std::min(w, h) * 0.045);
}

double frameThickness(double w, double h)
{
	return std::max(2.0, std::round(std::min(w, h) * 0.017));
}

} // namespace laycue
