#include "store/settings-io.hpp"

#include <obs-module.h>
#include <obs.hpp>
#include <plugin-support.h>

#include <algorithm>
#include <util/platform.h>

namespace laycue::network {

namespace {

std::string str(const char *s)
{
	return s ? std::string(s) : std::string();
}

std::string filePath()
{
	char *path = obs_module_config_path("settings.json");
	std::string result = str(path);
	bfree(path);
	return result;
}

OBSData measureToData(const UploadMeasure &m)
{
	OBSData d = obs_data_create();
	obs_data_set_bool(d, "valid", m.valid);
	obs_data_set_double(d, "median", m.medianMbps);
	obs_data_set_double(d, "low", m.lowMbps);
	obs_data_set_double(d, "jitter", m.jitter);
	obs_data_set_double(d, "rtt", m.rttMs);
	obs_data_set_double(d, "rttJitter", m.rttJitterMs);
	obs_data_set_double(d, "failed", m.failedShare);
	obs_data_set_int(d, "slices", m.slices);
	return d;
}

UploadMeasure measureFromData(obs_data_t *d)
{
	UploadMeasure m;
	if (!d)
		return m;
	m.valid = obs_data_get_bool(d, "valid");
	m.medianMbps = obs_data_get_double(d, "median");
	m.lowMbps = obs_data_get_double(d, "low");
	m.jitter = obs_data_get_double(d, "jitter");
	m.rttMs = obs_data_get_double(d, "rtt");
	m.rttJitterMs = obs_data_get_double(d, "rttJitter");
	m.failedShare = obs_data_get_double(d, "failed");
	m.slices = int(obs_data_get_int(d, "slices"));
	return m;
}

} // namespace

Settings loadSettings()
{
	Settings s;
	try {
		const std::string path = filePath();
		if (path.empty() || !os_file_exists(path.c_str()))
			return s;
		OBSDataAutoRelease root = obs_data_create_from_json_file(path.c_str());
		if (!root)
			return s;
		obs_data_set_default_string(root, "platform", s.platform.c_str());
		obs_data_set_default_int(root, "customMax", s.customMaxKbps);
		obs_data_set_default_int(root, "audio", s.audioKbps);
		obs_data_set_default_bool(root, "adaptive", s.adaptive);
		obs_data_set_default_int(root, "floor", s.floorKbps);
		obs_data_set_default_bool(root, "ceilingFromProfile", s.ceilingFromProfile);
		obs_data_set_default_int(root, "ceiling", s.ceilingKbps);
		obs_data_set_default_string(root, "server", s.testServer.c_str());
		obs_data_set_default_bool(root, "consent", s.testConsent);
		obs_data_set_default_int(root, "testSeconds", s.testSeconds);
		s.platform = str(obs_data_get_string(root, "platform"));
		s.customMaxKbps = int(obs_data_get_int(root, "customMax"));
		s.audioKbps = int(obs_data_get_int(root, "audio"));
		s.adaptive = obs_data_get_bool(root, "adaptive");
		s.floorKbps = int(obs_data_get_int(root, "floor"));
		s.ceilingFromProfile = obs_data_get_bool(root, "ceilingFromProfile");
		s.ceilingKbps = int(obs_data_get_int(root, "ceiling"));
		s.testServer = str(obs_data_get_string(root, "server"));
		s.testConsent = obs_data_get_bool(root, "consent");
		s.testSeconds = int(obs_data_get_int(root, "testSeconds"));
		{
			OBSDataAutoRelease last = obs_data_get_obj(root, "last");
			if (last) {
				s.last.valid = obs_data_get_bool(last, "valid");
				s.last.when = obs_data_get_int(last, "when");
				s.last.platformId = str(obs_data_get_string(last, "platform"));
				OBSDataAutoRelease measure = obs_data_get_obj(last, "measure");
				s.last.measure = measureFromData(measure);
			}
		}
		{
			OBSDataArrayAutoRelease history = obs_data_get_array(root, "history");
			const size_t n = history ? obs_data_array_count(history) : 0;
			for (size_t i = 0; i < n; ++i) {
				OBSDataAutoRelease item = obs_data_array_item(history, i);
				HistoryItem h;
				h.when = obs_data_get_int(item, "when");
				h.report.seconds = long(obs_data_get_int(item, "seconds"));
				h.report.averageKbps = obs_data_get_double(item, "avgKbps");
				h.report.dropPercent = obs_data_get_double(item, "dropPercent");
				h.report.bitrateChanges = int(obs_data_get_int(item, "changes"));
				h.report.lowestKbps = int(obs_data_get_int(item, "lowestKbps"));
				h.report.worstMinuteStart = long(obs_data_get_int(item, "worstStart"));
				h.report.worstMinuteDropPercent = obs_data_get_double(item, "worstDrop");
				h.report.verdict =
					Verdict(std::clamp<long long>(obs_data_get_int(item, "verdict"), 0, 2));
				s.history.push_back(h);
			}
		}
		sanitize(s);
	} catch (...) {
		obs_log(LOG_WARNING, "Laycue Network: could not read the saved settings");
		return Settings();
	}
	return s;
}

bool saveSettings(const Settings &s)
{
	try {
		const std::string path = filePath();
		if (path.empty())
			return false;
		OBSDataAutoRelease root = obs_data_create();
		obs_data_set_int(root, "version", 1);
		obs_data_set_string(root, "platform", s.platform.c_str());
		obs_data_set_int(root, "customMax", s.customMaxKbps);
		obs_data_set_int(root, "audio", s.audioKbps);
		obs_data_set_bool(root, "adaptive", s.adaptive);
		obs_data_set_int(root, "floor", s.floorKbps);
		obs_data_set_bool(root, "ceilingFromProfile", s.ceilingFromProfile);
		obs_data_set_int(root, "ceiling", s.ceilingKbps);
		obs_data_set_string(root, "server", s.testServer.c_str());
		obs_data_set_bool(root, "consent", s.testConsent);
		obs_data_set_int(root, "testSeconds", s.testSeconds);
		{
			OBSDataAutoRelease last = obs_data_create();
			obs_data_set_bool(last, "valid", s.last.valid);
			obs_data_set_int(last, "when", s.last.when);
			obs_data_set_string(last, "platform", s.last.platformId.c_str());
			OBSData measure = measureToData(s.last.measure);
			obs_data_set_obj(last, "measure", measure);
			obs_data_set_obj(root, "last", last);
		}
		{
			OBSDataArrayAutoRelease history = obs_data_array_create();
			for (const HistoryItem &h : s.history) {
				OBSDataAutoRelease item = obs_data_create();
				obs_data_set_int(item, "when", h.when);
				obs_data_set_int(item, "seconds", h.report.seconds);
				obs_data_set_double(item, "avgKbps", h.report.averageKbps);
				obs_data_set_double(item, "dropPercent", h.report.dropPercent);
				obs_data_set_int(item, "changes", h.report.bitrateChanges);
				obs_data_set_int(item, "lowestKbps", h.report.lowestKbps);
				obs_data_set_int(item, "worstStart", h.report.worstMinuteStart);
				obs_data_set_double(item, "worstDrop", h.report.worstMinuteDropPercent);
				obs_data_set_int(item, "verdict", int(h.report.verdict));
				obs_data_array_push_back(history, item);
			}
			obs_data_set_array(root, "history", history);
		}
		char *dir = obs_module_config_path("");
		if (dir) {
			os_mkdirs(dir);
			bfree(dir);
		}
		return obs_data_save_json_safe(root, path.c_str(), "tmp", "bak");
	} catch (...) {
		obs_log(LOG_WARNING, "Laycue Network: could not save the settings");
		return false;
	}
}

} // namespace laycue::network
