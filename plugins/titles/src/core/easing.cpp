#include "core/easing.hpp"

#include <algorithm>
#include <cmath>

namespace laycue::titles {

double clamp01(double t)
{
	return std::clamp(t, 0.0, 1.0);
}

double lerp(double a, double b, double t)
{
	return a + (b - a) * t;
}

double easeOutCubic(double t)
{
	const double u = 1.0 - clamp01(t);
	return 1.0 - u * u * u;
}

double easeInCubic(double t)
{
	t = clamp01(t);
	return t * t * t;
}

double easeInOutCubic(double t)
{
	t = clamp01(t);
	return t < 0.5 ? 4.0 * t * t * t : 1.0 - std::pow(-2.0 * t + 2.0, 3.0) / 2.0;
}

double easeOutExpo(double t)
{
	t = clamp01(t);
	return t >= 1.0 ? 1.0 : 1.0 - std::pow(2.0, -10.0 * t);
}

double easeInExpo(double t)
{
	t = clamp01(t);
	return t <= 0.0 ? 0.0 : std::pow(2.0, 10.0 * t - 10.0);
}

double easeOutBack(double t)
{
	t = clamp01(t);
	constexpr double c1 = 1.70158 * 0.6; // gentler than the textbook value
	constexpr double c3 = c1 + 1.0;
	const double u = t - 1.0;
	return 1.0 + c3 * u * u * u + c1 * u * u;
}

double stage(double t, double start, double length)
{
	if (length <= 0.0)
		return t >= start ? 1.0 : 0.0;
	return clamp01((t - start) / length);
}

} // namespace laycue::titles
