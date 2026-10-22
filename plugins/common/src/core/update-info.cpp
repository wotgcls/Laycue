#include "core/update-info.hpp"

#include <algorithm>
#include <cctype>

namespace laycue::update {

namespace {

std::string lower(std::string s)
{
	std::transform(s.begin(), s.end(), s.begin(), [](unsigned char c) { return char(std::tolower(c)); });
	return s;
}

bool endsWith(const std::string &s, const std::string &suffix)
{
	return s.size() >= suffix.size() && s.compare(s.size() - suffix.size(), suffix.size(), suffix) == 0;
}

bool contains(const std::string &s, const std::string &part)
{
	return s.find(part) != std::string::npos;
}

bool isHex64(const std::string &s)
{
	return s.size() == 64 && std::all_of(s.begin(), s.end(), [](unsigned char c) { return std::isxdigit(c) != 0; });
}

} // namespace

Version parseVersion(const std::string &text)
{
	Version v;
	size_t i = 0;
	if (i < text.size() && (text[i] == 'v' || text[i] == 'V'))
		++i;
	int parts[3] = {0, 0, 0};
	for (int p = 0; p < 3; ++p) {
		if (i >= text.size() || !std::isdigit(static_cast<unsigned char>(text[i])))
			return Version();
		long n = 0;
		int digits = 0;
		while (i < text.size() && std::isdigit(static_cast<unsigned char>(text[i]))) {
			n = n * 10 + (text[i] - '0');
			if (++digits > 6)
				return Version();
			++i;
		}
		parts[p] = int(n);
		if (p < 2) {
			if (i >= text.size() || text[i] != '.')
				return Version();
			++i;
		}
	}
	if (i < text.size()) {
		if (text[i] != '-' || i + 1 >= text.size())
			return Version();
		v.prerelease = true;
	}
	v.major = parts[0];
	v.minor = parts[1];
	v.patch = parts[2];
	v.valid = true;
	return v;
}

bool isNewer(const Version &candidate, const Version &current)
{
	if (!candidate.valid || !current.valid)
		return false;
	if (candidate.major != current.major)
		return candidate.major > current.major;
	if (candidate.minor != current.minor)
		return candidate.minor > current.minor;
	if (candidate.patch != current.patch)
		return candidate.patch > current.patch;
	return current.prerelease && !candidate.prerelease; // 0.2.0 is newer than 0.2.0-rc1
}

std::string pickInstaller(const std::vector<std::string> &names, Platform platform)
{
	for (const std::string &name : names) {
		const std::string n = lower(name);
		if (contains(n, "dbgsym") || contains(n, "dsym") || contains(n, "source") || endsWith(n, ".sha256"))
			continue;
		switch (platform) {
		case Platform::Windows:
			if (endsWith(n, ".exe") && contains(n, "windows"))
				return name;
			break;
		case Platform::MacOS:
			if (endsWith(n, ".pkg") && contains(n, "macos"))
				return name;
			break;
		case Platform::Linux:
			if (endsWith(n, ".deb") && !endsWith(n, ".ddeb"))
				return name;
			break;
		}
	}
	return std::string();
}

std::string expectedSha256(const std::string &checksumText, const std::string &fileName)
{
	size_t pos = 0;
	while (pos <= checksumText.size()) {
		size_t end = checksumText.find('\n', pos);
		if (end == std::string::npos)
			end = checksumText.size();
		std::string line = checksumText.substr(pos, end - pos);
		pos = end + 1;
		while (!line.empty() && (line.back() == '\r' || line.back() == ' '))
			line.pop_back();
		size_t start = 0;
		while (start < line.size() && line[start] == ' ')
			++start;
		line = line.substr(start);
		if (line.size() < 64)
			continue;
		const std::string hash = lower(line.substr(0, 64));
		if (!isHex64(hash))
			continue;
		const std::string rest = line.substr(64);
		if (rest.empty())
			return hash; // a file holding only the hash
		if (contains(rest, fileName))
			return hash;
	}
	return std::string(); // hashes that name other files are never used for this one
}

bool isAllowedDownloadUrl(const std::string &url, bool allowLocal)
{
	const std::string u = lower(url);
	const auto hostOf = [](const std::string &rest) {
		const size_t slash = rest.find_first_of("/?#");
		return rest.substr(0, slash == std::string::npos ? rest.size() : slash);
	};
	if (u.rfind("https://", 0) == 0) {
		const std::string host = hostOf(u.substr(8));
		static const char *const allowed[] = {"api.github.com", "github.com", "objects.githubusercontent.com",
						      "release-assets.githubusercontent.com",
						      "github-releases.githubusercontent.com"};
		for (const char *a : allowed)
			if (host == a)
				return true;
		return false;
	}
	if (allowLocal && u.rfind("http://", 0) == 0) {
		const std::string host = hostOf(u.substr(7));
		return host == "127.0.0.1" || host.rfind("127.0.0.1:", 0) == 0 || host == "localhost" ||
		       host.rfind("localhost:", 0) == 0;
	}
	return false;
}

std::string safeFileName(const std::string &name)
{
	const size_t slash = name.find_last_of("/\\");
	std::string base = slash == std::string::npos ? name : name.substr(slash + 1);
	for (char &c : base)
		if (!(std::isalnum(static_cast<unsigned char>(c)) || c == '.' || c == '-' || c == '_'))
			c = '_';
	if (base.empty() || base == "." || base == "..")
		return "laycue-update";
	return base;
}

} // namespace laycue::update
