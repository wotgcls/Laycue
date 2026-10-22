#include "check.hpp"
#include "core/update-info.hpp"

using namespace laycue::update;

TEST_CASE("update: versions are read and compared")
{
	CHECK(parseVersion("0.1.2").valid);
	CHECK(parseVersion("v1.20.3").minor == 20);
	CHECK(parseVersion("0.2.0-rc1").prerelease);
	CHECK(!parseVersion("").valid);
	CHECK(!parseVersion("1.2").valid);
	CHECK(!parseVersion("1.2.x").valid);
	CHECK(!parseVersion("1.2.3-").valid);
	CHECK(!parseVersion("1.2.3beta").valid);
	CHECK(!parseVersion("9999999.0.0").valid); // absurdly long numbers are refused, not overflowed

	CHECK(isNewer(parseVersion("0.1.1"), parseVersion("0.1.0")));
	CHECK(isNewer(parseVersion("0.2.0"), parseVersion("0.1.9")));
	CHECK(isNewer(parseVersion("1.0.0"), parseVersion("0.9.9")));
	CHECK(!isNewer(parseVersion("0.1.0"), parseVersion("0.1.0")));
	CHECK(!isNewer(parseVersion("0.1.0"), parseVersion("0.1.1")));
	CHECK(isNewer(parseVersion("0.2.0"), parseVersion("0.2.0-rc1")));
	CHECK(!isNewer(parseVersion("0.2.0-rc1"), parseVersion("0.2.0")));
	CHECK(!isNewer(parseVersion("nonsense"), parseVersion("0.1.0"))); // a broken tag is never an update
	CHECK(!isNewer(parseVersion("0.2.0"), parseVersion("nonsense")));
}

TEST_CASE("update: the installer for this computer is picked, never symbols or sources")
{
	const std::vector<std::string> names = {"CHECKSUMS.txt",
						"laycue-0.1.2-sources.tar.xz",
						"laycue-0.1.2-windows-x64.zip",
						"laycue-0.1.2-windows-x64-Installer.exe",
						"laycue-0.1.2-windows-x64-Installer.exe.sha256",
						"laycue-0.1.2-macos-universal.pkg",
						"laycue-0.1.2-macos-universal-dSYMs.tar.xz",
						"laycue-0.1.2-ubuntu-24.04-x86_64.deb",
						"laycue-0.1.2-ubuntu-24.04-x86_64-dbgsym.ddeb"};
	CHECK_EQ(pickInstaller(names, Platform::Windows), std::string("laycue-0.1.2-windows-x64-Installer.exe"));
	CHECK_EQ(pickInstaller(names, Platform::MacOS), std::string("laycue-0.1.2-macos-universal.pkg"));
	CHECK_EQ(pickInstaller(names, Platform::Linux), std::string("laycue-0.1.2-ubuntu-24.04-x86_64.deb"));
	CHECK_EQ(pickInstaller({"notes.txt"}, Platform::Windows), std::string());
	CHECK_EQ(pickInstaller({}, Platform::Linux), std::string());
}

TEST_CASE("update: the expected checksum is read from a checksum file")
{
	const std::string hash(64, 'a');
	const std::string other(64, 'B');
	CHECK_EQ(expectedSha256(hash + "\n", "x.exe"), hash);
	CHECK_EQ(expectedSha256(hash + "  x.exe\r\n", "x.exe"), hash);
	CHECK_EQ(expectedSha256(other + "  y.exe\n" + hash + "  x.exe\n", "x.exe"), hash);
	CHECK_EQ(expectedSha256(other + "  y.exe\n", "x.exe"), std::string()); // a hash for another file is not used
	CHECK_EQ(expectedSha256(other + "  y.exe\n" + hash + "  z.exe\n", "x.exe"), std::string());
	CHECK_EQ(expectedSha256("not a hash", "x.exe"), std::string());
	CHECK_EQ(expectedSha256("", "x.exe"), std::string());
	CHECK_EQ(expectedSha256(std::string(63, 'a'), "x.exe"), std::string());
	CHECK_EQ(expectedSha256(std::string(64, 'z'), "x.exe"), std::string()); // not hex
	CHECK_EQ(expectedSha256(other + "\n", "x.exe"), std::string(64, 'b'));  // lower-cased
}

TEST_CASE("update: only GitHub over https may be downloaded from")
{
	CHECK(isAllowedDownloadUrl("https://api.github.com/repos/a/b/releases/latest"));
	CHECK(isAllowedDownloadUrl("https://github.com/a/b/releases/download/1.0.0/x.exe"));
	CHECK(isAllowedDownloadUrl("https://objects.githubusercontent.com/abc?x=1"));
	CHECK(isAllowedDownloadUrl("https://release-assets.githubusercontent.com/abc"));
	CHECK(!isAllowedDownloadUrl("http://github.com/a/b"));
	CHECK(!isAllowedDownloadUrl("https://evil.example.com/github.com/x.exe"));
	CHECK(!isAllowedDownloadUrl("https://github.com.evil.example.com/x.exe"));
	CHECK(!isAllowedDownloadUrl("https://github.com@evil.example.com/x.exe"));
	CHECK(!isAllowedDownloadUrl("ftp://github.com/x"));
	CHECK(!isAllowedDownloadUrl(""));
	CHECK(!isAllowedDownloadUrl("http://127.0.0.1:8000/x")); // local only when a test asks for it
	CHECK(isAllowedDownloadUrl("http://127.0.0.1:8000/x", true));
	CHECK(isAllowedDownloadUrl("http://localhost/x", true));
	CHECK(!isAllowedDownloadUrl("http://127.0.0.1.evil.com/x", true));
}

TEST_CASE("update: a downloaded file name cannot escape its folder")
{
	CHECK_EQ(safeFileName("laycue-0.1.2-windows-x64-Installer.exe"),
		 std::string("laycue-0.1.2-windows-x64-Installer.exe"));
	CHECK_EQ(safeFileName("../../evil.exe"), std::string("evil.exe"));
	CHECK_EQ(safeFileName("C:\\\\Windows\\\\x y.exe"), std::string("x_y.exe"));
	CHECK_EQ(safeFileName(".."), std::string("laycue-update"));
	CHECK_EQ(safeFileName(""), std::string("laycue-update"));
}
