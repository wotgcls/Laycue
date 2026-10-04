#include "obs/stream-stats.hpp"

#include <obs-frontend-api.h>
#include <obs.hpp>
#include <plugin-support.h>
#include <util/config-file.h>
#include <util/platform.h>

namespace laycue::network {

namespace {

std::string str(const char *s)
{
	return s ? std::string(s) : std::string();
}

} // namespace

bool readStreamSnapshot(StreamSnapshot &out)
{
	out = StreamSnapshot();
	if (!obs_frontend_streaming_active())
		return false;
	OBSOutputAutoRelease output = obs_frontend_get_streaming_output();
	if (!output)
		return false;
	out.streaming = true;
	out.totalBytes = obs_output_get_total_bytes(output);
	out.totalFrames = obs_output_get_total_frames(output);
	out.droppedFrames = obs_output_get_frames_dropped(output);
	out.congestion = obs_output_get_congestion(output);
	obs_encoder_t *encoder = obs_output_get_video_encoder(output); // not a new reference
	if (encoder) {
		out.hasEncoder = true;
		out.encoderId = str(obs_encoder_get_id(encoder));
		OBSDataAutoRelease settings = obs_encoder_get_settings(encoder);
		if (settings)
			out.videoKbps = int(obs_data_get_int(settings, "bitrate"));
	}
	return true;
}

bool setLiveVideoBitrate(int kbps)
{
	if (kbps <= 0 || !obs_frontend_streaming_active())
		return false;
	OBSOutputAutoRelease output = obs_frontend_get_streaming_output();
	if (!output)
		return false;
	obs_encoder_t *encoder = obs_output_get_video_encoder(output);
	if (!encoder)
		return false;
	OBSDataAutoRelease settings = obs_data_create();
	obs_data_set_int(settings, "bitrate", kbps);
	obs_encoder_update(encoder, settings); // merges into the encoder's settings and reconfigures it
	return true;
}

bool obsDynamicBitrateEnabled()
{
	config_t *config = obs_frontend_get_profile_config();
	return config && config_get_bool(config, "Output", "DynamicBitrate");
}

bool setObsDynamicBitrate(bool enabled)
{
	config_t *config = obs_frontend_get_profile_config();
	if (!config)
		return false;
	config_set_bool(config, "Output", "DynamicBitrate", enabled);
	return config_save_safe(config, "tmp", nullptr) == CONFIG_SUCCESS;
}

namespace {

// The profile's advanced streaming encoder settings file.
std::string advancedEncoderFile()
{
	char *dir = obs_frontend_get_current_profile_path();
	std::string path = dir ? str(dir) + "/streamEncoder.json" : std::string();
	bfree(dir);
	return path;
}

} // namespace

std::string applyBitrateToProfile(int kbps)
{
	config_t *config = obs_frontend_get_profile_config();
	if (!config || kbps <= 0)
		return std::string();
	const std::string mode = str(config_get_string(config, "Output", "Mode"));
	if (mode == "Advanced") {
		const std::string file = advancedEncoderFile();
		if (file.empty())
			return std::string();
		OBSDataAutoRelease data = os_file_exists(file.c_str())
						  ? obs_data_create_from_json_file_safe(file.c_str(), "bak")
						  : obs_data_create();
		if (!data)
			data = obs_data_create();
		obs_data_set_int(data, "bitrate", kbps);
		return obs_data_save_json_safe(data, file.c_str(), "tmp", "bak") ? "Advanced output" : std::string();
	}
	config_set_uint(config, "SimpleOutput", "VBitrate", uint64_t(kbps));
	return config_save_safe(config, "tmp", nullptr) == CONFIG_SUCCESS ? "Simple output" : std::string();
}

int profileVideoBitrate()
{
	config_t *config = obs_frontend_get_profile_config();
	if (!config)
		return 0;
	if (str(config_get_string(config, "Output", "Mode")) == "Advanced") {
		const std::string file = advancedEncoderFile();
		if (!file.empty() && os_file_exists(file.c_str())) {
			OBSDataAutoRelease data = obs_data_create_from_json_file_safe(file.c_str(), "bak");
			if (data && obs_data_has_user_value(data, "bitrate"))
				return int(obs_data_get_int(data, "bitrate"));
		}
		return 0;
	}
	return int(config_get_uint(config, "SimpleOutput", "VBitrate"));
}

} // namespace laycue::network
