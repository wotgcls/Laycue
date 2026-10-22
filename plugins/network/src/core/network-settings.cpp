#include "core/network-settings.hpp"

#include "core/recommend.hpp"

#include <algorithm>

namespace laycue::network {

void sanitize(Settings &s)
{
	bool known = false;
	for (const Platform &p : platforms())
		known = known || p.id == s.platform;
	if (!known)
		s.platform = platforms().front().id;
	s.customMaxKbps = std::clamp(s.customMaxKbps, 600, 50000);
	s.audioKbps = std::clamp(s.audioKbps, 32, 512);
	s.floorKbps = std::clamp(s.floorKbps, 300, 50000);
	s.ceilingKbps = std::clamp(s.ceilingKbps, s.floorKbps, 50000);
	s.testSeconds = std::clamp(s.testSeconds, 4, 30);
	if (s.testServer.empty() || s.testServer.size() > 300)
		s.testServer = Settings().testServer;
	if (s.history.size() > kMaxHistory)
		s.history.erase(s.history.begin(), s.history.end() - static_cast<std::ptrdiff_t>(kMaxHistory));
}

void addHistory(Settings &s, const HistoryItem &item)
{
	s.history.push_back(item);
	while (s.history.size() > kMaxHistory)
		s.history.erase(s.history.begin());
}

} // namespace laycue::network
