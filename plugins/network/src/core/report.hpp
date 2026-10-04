// A summary of one stream, kept while it runs and shown when it ends. Pure logic.
#pragma once

#include "core/health.hpp"

#include <string>

namespace laycue::network {

enum class Verdict { Smooth, Bumpy, Rough };

struct StreamReport {
	long seconds = 0;
	double averageKbps = 0.0;
	double dropPercent = 0.0;
	int bitrateChanges = 0;
	int lowestKbps = 0;
	long worstMinuteStart = 0; // seconds from the start of the worst minute
	double worstMinuteDropPercent = 0.0;
	Verdict verdict = Verdict::Smooth;
};

class ReportBuilder {
public:
	void start();
	void add(const LiveSample &sample);
	void bitrateChanged(int newKbps);
	StreamReport finish() const;
	bool running() const { return running_; }

private:
	bool running_ = false;
	long seconds_ = 0;
	double sentSum_ = 0.0;
	long dropped_ = 0;
	long frames_ = 0;
	int changes_ = 0;
	int lowest_ = 0;
	// per-minute buckets
	struct Minute {
		long dropped = 0;
		long frames = 0;
	};
	std::vector<Minute> minutes_;
};

} // namespace laycue::network
