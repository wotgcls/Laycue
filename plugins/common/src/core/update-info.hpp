// The pure rules of "Check for updates": which version is newer, which file of a release is the installer for
// this computer, what its checksum should be, and which web addresses may be downloaded from. No Qt, no OBS.
#pragma once

#include <string>
#include <vector>

namespace laycue::update {

struct Version {
	int major = 0;
	int minor = 0;
	int patch = 0;
	bool valid = false;
	bool prerelease = false; // "0.2.0-rc1"
};

// "0.1.2", "v0.1.2" or "0.1.2-rc1". Anything else is not valid.
Version parseVersion(const std::string &text);

// True if `candidate` is a higher release than `current`. A pre-release is lower than the same release
// without the suffix; invalid versions are never newer.
bool isNewer(const Version &candidate, const Version &current);

enum class Platform { Windows, MacOS, Linux };

// The installer among a release's file names for this platform: "laycue-0.1.2-windows-x64-Installer.exe",
// "...macos-universal.pkg", "...ubuntu-24.04-x86_64.deb" (never a debug-symbols or source file). Empty if none.
std::string pickInstaller(const std::vector<std::string> &names, Platform platform);

// The expected SHA-256 (64 lower-case hex digits) from the text of a checksum file, which holds either just the
// hash or "<hash>  <file name>" lines. For several lines the one naming `fileName` is used. Empty if none fits.
std::string expectedSha256(const std::string &checksumText, const std::string &fileName);

// Downloads are followed only from GitHub's own hosts over https. `allowLocal` additionally accepts
// http://127.0.0.1 / localhost, used only by tests.
bool isAllowedDownloadUrl(const std::string &url, bool allowLocal = false);

// A file name safe to create in a folder: the part after the last slash, with anything odd replaced.
std::string safeFileName(const std::string &name);

} // namespace laycue::update
