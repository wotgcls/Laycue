# Updates: "Check for updates" in all three plugins

Each plugin has an **Updates** card: Titles in the **Setup** tab, Scenes in **Settings**, Network at the bottom of the
**Test** tab. It checks the newest release in a small *public* repository (`wotgcls/laycue-releases`) that holds only the
installers, so the source repository can stay private.

## What the button does

1. **Check for updates** asks GitHub for the newest release and compares its version with the installed one.
2. **Download update** saves the installer to a temporary folder and checks its SHA-256 against the checksum file published
   with it. A damaged or unchecked file is deleted and never run.
3. **Install when I close OBS** (Windows) starts a small hidden helper that waits until this OBS has closed, then runs the
   installer quietly. Windows asks for permission as it starts. Nothing is installed while OBS is open, so a stream or
   recording is never interrupted. On macOS and Linux **Show the file** opens the folder and you install it yourself.

It only talks to the network when you press a button, and only to GitHub (any other address is refused, including after a
redirect). If this OBS cannot make secure connections it says so and offers **Open download page** instead.

## One-time setup (the owner)

1. **Create the public repository.** GitHub > the **+** at the top right > **New repository**. Name: `laycue-releases`.
   Choose **Public**, tick **Add a README file**, then **Create repository**.
2. **Create a token that may only write to that repository.** GitHub > your profile picture > **Settings** > **Developer
   settings** (bottom of the left menu) > **Personal access tokens** > **Fine-grained tokens** > **Generate new token**.
   - Token name: `laycue-releases`. Expiration: 1 year (put a reminder in your calendar).
   - Repository access: **Only select repositories** > `laycue-releases`.
   - Permissions > Repository permissions > **Contents: Read and write**.
   - **Generate token** and copy it now (GitHub shows it only once).
3. **Give the token to the build.** Open the `Laycue` repository > **Settings** > **Secrets and variables** > **Actions** >
   **New repository secret**. Name: `RELEASES_TOKEN`. Secret: paste the token. **Add secret**.
4. **Install a build that has the button** once, the usual way. After that, updates come through the button.

## Publishing an update

1. Edit `RELEASE_NOTES.md` (what's new, a few short lines; it is what **What's new** shows).
2. In the `Laycue` repository: **Releases** > **Draft a new release** > **Choose a tag** > type the new version, for example
   `0.2.0` > **Create new tag: 0.2.0 on publish** > **Publish release**.
3. The build runs (about 15 minutes), stamps `0.2.0` into the plugins as their version, and publishes the installers and
   their checksum files to `laycue-releases`. Anyone with an older Laycue then sees the update.

Versions look like `0.2.0`; a release with `-rc1` or `-beta1` after it is marked a pre-release and is not offered.
