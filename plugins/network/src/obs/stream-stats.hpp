// Reads how the live stream is doing straight from OBS, and changes the video bitrate of the running
// encoder. The only place the Network plugin talks to libobs. UI thread only.
#pragma once

#include <cstdint>
#include <string>

namespace laycue::network {

struct StreamSnapshot {
	bool streaming = false;
	uint64_t totalBytes = 0; // sent since the stream started (video, audio and overhead)
	int totalFrames = 0;
	int droppedFrames = 0;    // dropped by the output because the network could not keep up
	double congestion = -1.0; // 0..1, or negative if this output cannot say
	int videoKbps = 0;        // the video encoder's bitrate setting right now
	std::string encoderId;    // "obs_x264", "jim_nvenc", ...
	bool hasEncoder = false;
};

// False if no stream is running (then `out` is left empty).
bool readStreamSnapshot(StreamSnapshot &out);

// Changes the running video encoder's bitrate. True if OBS accepted the call; whether the encoder really
// followed is checked by watching what it sends (see core/adaptive).
bool setLiveVideoBitrate(int kbps);

// OBS's own "Dynamically change bitrate to manage congestion" (Settings > Advanced > Network).
bool obsDynamicBitrateEnabled();
bool setObsDynamicBitrate(bool enabled);

// Writes a bitrate into the current OBS profile so it is used from the next stream. Returns a short
// description of where it was written ("Simple output", "Advanced output") or "" if it could not be.
std::string applyBitrateToProfile(int kbps);

// The video bitrate the profile will use for the next stream, or 0 if it cannot be read.
int profileVideoBitrate();

} // namespace laycue::network
