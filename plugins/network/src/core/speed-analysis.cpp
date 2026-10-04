#include "core/speed-analysis.hpp"

#include "core/stats.hpp"

#include <algorithm>

namespace laycue::network {

UploadMeasure analyseUpload(const std::vector<UploadSlice> &slices, const std::vector<double> &rttMs, int requests,
			    int failed, double warmupSeconds)
{
	UploadMeasure m;
	double elapsed = 0.0;
	std::vector<double> speeds;
	for (const UploadSlice &s : slices) {
		const double start = elapsed;
		elapsed += std::max(s.seconds, 0.0);
		if (start < warmupSeconds || s.seconds < 0.25)
			continue; // still warming up, or too short to trust
		speeds.push_back(s.bytes * 8.0 / s.seconds / 1.0e6);
	}
	m.slices = int(speeds.size());
	if (speeds.size() >= 3) {
		m.valid = true;
		m.medianMbps = median(speeds);
		m.lowMbps = percentile(speeds, 10.0);
		const double avg = mean(speeds);
		m.jitter = avg > 0.0 ? stddev(speeds) / avg : 0.0;
	}
	if (!rttMs.empty()) {
		m.rttMs = median(rttMs);
		m.rttJitterMs = stddev(rttMs);
	}
	if (requests > 0)
		m.failedShare = std::clamp(double(failed) / double(requests), 0.0, 1.0);
	return m;
}

} // namespace laycue::network
