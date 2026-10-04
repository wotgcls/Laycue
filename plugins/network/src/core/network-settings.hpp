// What the Network panel remembers, as plain data. Pure logic: no libobs, no Qt.
#pragma once

#include "core/report.hpp"
#include "core/speed-analysis.hpp"

#include <string>
#include <vector>

namespace laycue::network {

struct LastTest {
	bool valid = false;
	long long when = 0; // seconds since 1970
	UploadMeasure measure;
	std::string platformId;
};

struct HistoryItem {
	long long when = 0; // when the stream ended
	StreamReport report;
};

struct Settings {
	std::string platform = "twitch";
	int customMaxKbps = 8000; // the ceiling of the "Custom" platform
	int audioKbps = 160;

	bool adaptive = false;          // auto-adjust is OFF until the operator turns it on
	int floorKbps = 1000;           // never go below
	bool ceilingFromProfile = true; // the ceiling is whatever bitrate OBS is set to when the stream starts
	int ceilingKbps = 6000;         // used when ceilingFromProfile is false

	std::string testServer = "https://speed.cloudflare.com/__up";
	bool testConsent = false; // the operator has agreed to send test data to the server above
	int testSeconds = 8;

	LastTest last;
	std::vector<HistoryItem> history; // newest last, at most kMaxHistory
};

constexpr size_t kMaxHistory = 10;

// Keeps numbers in range and the history short. Never throws.
void sanitize(Settings &settings);

// Adds a finished stream to the history, dropping the oldest beyond kMaxHistory.
void addHistory(Settings &settings, const HistoryItem &item);

} // namespace laycue::network
