// Turns the raw numbers of an upload test into one honest measurement. Pure logic.
#pragma once

#include <vector>

namespace laycue::network {

// Bytes sent during one slice of the test.
struct UploadSlice {
	double seconds = 0.0;
	double bytes = 0.0;
};

struct UploadMeasure {
	bool valid = false;      // enough good slices to say something
	double medianMbps = 0.0; // the typical speed
	double lowMbps = 0.0;    // the speed it dips to (10th percentile): what a stream must survive
	double jitter = 0.0;     // how much the speed moves, as a share of the typical speed (0 = rock steady)
	double rttMs = 0.0;      // typical round-trip time
	double rttJitterMs = 0.0;
	double failedShare = 0.0; // share of requests that failed (0..1), a sign of loss
	int slices = 0;           // how many slices were used
};

// `slices` are in time order. The first `warmupSeconds` are ignored (TCP is still speeding up). `rttMs` are
// round-trip samples; `requests` and `failed` count the small requests made to measure them.
UploadMeasure analyseUpload(const std::vector<UploadSlice> &slices, const std::vector<double> &rttMs, int requests,
			    int failed, double warmupSeconds = 1.5);

} // namespace laycue::network
