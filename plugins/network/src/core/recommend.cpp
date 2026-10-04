#include "core/recommend.hpp"

#include <algorithm>

namespace laycue::network {

const std::vector<Platform> &platforms()
{
	static const std::vector<Platform> all = {
		{"twitch", "Laycue.Network.Platform.Twitch", 6000},
		{"youtube", "Laycue.Network.Platform.YouTube", 9000},
		{"facebook", "Laycue.Network.Platform.Facebook", 6000},
		{"kick", "Laycue.Network.Platform.Kick", 8000},
		{"custom", "Laycue.Network.Platform.Custom", 8000},
	};
	return all;
}

const Platform &platformById(const std::string &id)
{
	for (const Platform &p : platforms()) {
		if (p.id == id)
			return p;
	}
	return platforms().front();
}

Platform customPlatform(int maxVideoKbps)
{
	Platform p = platformById("custom");
	p.maxVideoKbps = std::clamp(maxVideoKbps, 600, 50000);
	return p;
}

const std::vector<int> &bitrateLadder()
{
	static const std::vector<int> ladder = {600,  900,  1200, 1500, 2000, 2500,  3000,  3500,  4000, 4500,
						5000, 6000, 7000, 8000, 9000, 10000, 12000, 15000, 20000};
	return ladder;
}

void sizeForBitrate(int videoKbps, int &width, int &height, int &fps)
{
	if (videoKbps >= 6000) {
		width = 1920;
		height = 1080;
		fps = 60;
	} else if (videoKbps >= 4500) {
		width = 1920;
		height = 1080;
		fps = 30;
	} else if (videoKbps >= 3000) {
		width = 1280;
		height = 720;
		fps = 60;
	} else if (videoKbps >= 1800) {
		width = 1280;
		height = 720;
		fps = 30;
	} else if (videoKbps >= 1000) {
		width = 854;
		height = 480;
		fps = 30;
	} else {
		width = 640;
		height = 360;
		fps = 30;
	}
}

namespace {

// One step down in picture size and frame rate (for a connection that is not steady).
void oneStepLighter(int &width, int &height, int &fps)
{
	if (fps == 60) {
		fps = 30;
	} else if (height == 1080) {
		width = 1280;
		height = 720;
	} else if (height == 720) {
		width = 854;
		height = 480;
	} else if (height == 480) {
		width = 640;
		height = 360;
	}
}

} // namespace

Recommendation recommend(const UploadMeasure &m, const Platform &platform, int audioKbps)
{
	Recommendation r;
	r.audioKbps = audioKbps;
	if (!m.valid || m.medianMbps <= 0.0)
		return r;
	r.valid = true;
	r.unstable = m.jitter > 0.35 || m.failedShare > 0.05;

	// What you can count on: three quarters of the typical speed, but never more than 90% of the dips.
	double countOn = std::min(m.medianMbps * 0.75, m.lowMbps * 0.90);
	if (r.unstable)
		countOn *= 0.85;
	const double rawVideoKbps = countOn * 1000.0 - double(audioKbps);

	const std::vector<int> &ladder = bitrateLadder();
	int video = ladder.front();
	bool fits = rawVideoKbps >= double(ladder.front());
	for (int rung : ladder) {
		if (double(rung) <= rawVideoKbps)
			video = rung;
	}
	r.tooSlow = !fits;
	if (video > platform.maxVideoKbps) {
		int best = ladder.front();
		for (int rung : ladder) {
			if (rung <= platform.maxVideoKbps)
				best = rung;
		}
		video = best;
		r.limitedByPlatform = true;
	}
	r.videoKbps = video;
	sizeForBitrate(video, r.width, r.height, r.fps);
	if (r.unstable)
		oneStepLighter(r.width, r.height, r.fps);

	const double needMbps = double(video + audioKbps) / 1000.0;
	r.headroomPercent = needMbps > 0.0 ? (m.medianMbps / needMbps - 1.0) * 100.0 : 0.0;
	const double ratio = needMbps > 0.0 ? m.medianMbps / needMbps : 0.0;
	if (r.tooSlow || ratio < 1.25)
		r.rating = Rating::Poor;
	else if (ratio < 1.5 || r.unstable)
		r.rating = Rating::Tight;
	else if (ratio < 2.0)
		r.rating = Rating::Good;
	else
		r.rating = Rating::Excellent;
	return r;
}

} // namespace laycue::network
