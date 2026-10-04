// From a measured upload speed to a recommended stream setting. Pure logic.
//
// The reasoning is the one experienced streamers use by hand, written down so it is the same every time:
// never plan to use more than about three quarters of the speed you can count on, leave room for the audio
// and for the stream's own overhead, never go above what the platform takes, and drop to a lighter picture
// size before asking a thin connection for a bitrate it cannot hold.
#pragma once

#include "core/speed-analysis.hpp"

#include <string>
#include <vector>

namespace laycue::network {

struct Platform {
	std::string id;      // saved in settings: "twitch", "youtube", ...
	const char *nameKey; // locale key for its name
	int maxVideoKbps;    // the most video bitrate it is sensible to send there
};

const std::vector<Platform> &platforms();
const Platform &platformById(const std::string &id); // falls back to the first one
Platform customPlatform(int maxVideoKbps);

enum class Rating { Unknown, Excellent, Good, Tight, Poor };

struct Recommendation {
	bool valid = false;
	int videoKbps = 0;
	int audioKbps = 160;
	int width = 1280;
	int height = 720;
	int fps = 30;
	Rating rating = Rating::Unknown;
	bool limitedByPlatform = false; // the connection could carry more than the platform takes
	bool unstable = false;          // the speed moved a lot during the test
	bool tooSlow = false;           // even the lowest setting is a stretch
	double headroomPercent = 0.0;   // how much faster the connection is than the stream will need
};

// The bitrates it will pick from, lowest first.
const std::vector<int> &bitrateLadder();

Recommendation recommend(const UploadMeasure &measure, const Platform &platform, int audioKbps = 160);

// The picture size and frame rate that suits a video bitrate (H.264).
void sizeForBitrate(int videoKbps, int &width, int &height, int &fps);

} // namespace laycue::network
