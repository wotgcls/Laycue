#include "core/stats.hpp"

#include <algorithm>
#include <cmath>

namespace laycue::network {

double percentile(std::vector<double> values, double p)
{
	if (values.empty())
		return 0.0;
	std::sort(values.begin(), values.end());
	p = std::clamp(p, 0.0, 100.0);
	const double position = p / 100.0 * double(values.size() - 1);
	const size_t lower = size_t(std::floor(position));
	const size_t upper = size_t(std::ceil(position));
	const double fraction = position - double(lower);
	return values[lower] + (values[upper] - values[lower]) * fraction;
}

double median(const std::vector<double> &values)
{
	return percentile(values, 50.0);
}

double mean(const std::vector<double> &values)
{
	if (values.empty())
		return 0.0;
	double sum = 0.0;
	for (double v : values)
		sum += v;
	return sum / double(values.size());
}

double stddev(const std::vector<double> &values)
{
	if (values.size() < 2)
		return 0.0;
	const double m = mean(values);
	double squares = 0.0;
	for (double v : values)
		squares += (v - m) * (v - m);
	return std::sqrt(squares / double(values.size()));
}

} // namespace laycue::network
