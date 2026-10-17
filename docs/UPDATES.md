# Updates: "Check for updates" in every plugin

Each plugin has an **Updates** card: Titles in the **Setup** tab, Scenes in **Settings**, Network at the bottom of the
**Test** tab. It checks the newest published release of the public `wotgcls/Laycue` repository.

## What the button does

1. **Check for updates** asks GitHub for the newest release and compares its version with the installed one.
2. **Download update** saves the installer to a temporary folder and checks its SHA-256 against the checksum file published
   with it. A damaged or unchecked file is deleted and never run.
3. **Install when I close OBS** (Windows) starts a small hidden helper that waits until this OBS has closed, then runs the
   installer quietly. Windows asks for permission as it starts. Nothing is installed while OBS is open, so a stream or
   recording is never interrupted. On macOS and Linux **Show the file** opens the folder and you install it yourself.

It only talks to the network when you press a button, and only to GitHub (any other address is refused, including after a
redirect). If this OBS cannot make secure connections it says so and offers **Open download page** instead.

## Publishing an update (the owner)

The panels check the newest **published** release of this repository (`wotgcls/Laycue`, public). Every panel has the card:
Scripture and Lyrics at the bottom of the main page, Titles in **Setup**, Scenes in **Settings**, Network at the bottom of
the **Test** tab, Clock at the bottom.

1. Edit `RELEASE_NOTES.md` (what's new, a few short lines; it is what **What's new** shows).
2. Push a version tag, for example `0.2.0`. On GitHub: **Releases** > **Draft a new release** > **Choose a tag** > type
   `0.2.0` > **Create new tag: 0.2.0 on publish**, then **Save draft**. The build runs (about 15 minutes), stamps `0.2.0`
   into the plugins and attaches the installers and their `.sha256` checksum files to the draft.
3. When the build is done, open the draft and press **Publish release**. Anyone with an older Laycue then sees the update
   in the card.

Versions look like `0.2.0`; a release with `-rc1` or `-beta1` after it is marked a pre-release and is not offered. The
`test-build` pre-release (newest Windows build of any branch) is not an update and is never offered.

## Secure connections on Windows

Some OBS installs on Windows have no TLS part in their copy of Qt, so Qt cannot make https connections (the Updates card used to say "This OBS cannot make secure connections", and the speed test failed). Laycue now checks, and when Qt cannot do https it uses Windows' own web client (WinHTTP, part of Windows, nothing to download) for the update check, the download and the speed test. Everything else is unchanged: redirects are still checked against the allowed hosts, and the download is still checked against its SHA-256. The code is `plugins/common/src/net/` (`http.cpp` for Qt, `http-win.cpp` for WinHTTP). macOS and Linux still use Qt only.
