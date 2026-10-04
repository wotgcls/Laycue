// Small statistics helpers for network measurements. Pure logic: no libobs, no Qt.
#pragma once

#include <vector>

namespace laycue::network {

// The p-th percentile (0..100) of the values, by linear interpolation. 0 for an empty list.
double percentile(std::vector<double> values, double p);
double median(const std::vector<double> &values);
double mean(const std::vector<double> &values);
// Standard deviation of the values (population). 0 for fewer than 2 values.
double stddev(const std::vector<double> &values);

} // namespace laycue::network
