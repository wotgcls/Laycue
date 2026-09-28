# Laycue

A layout and layer manager for OBS Studio: one-button layouts, rules that stop overlays from colliding, and automatic fitting of cameras into frame graphics.

Laycue is a native OBS plugin. See [`PRD.md`](PRD.md) for what it does and why, and [`CLAUDE.md`](CLAUDE.md) for how it's built.

**Status:** M0, the plugin skeleton. It loads in OBS and adds a Laycue panel that lists your scenes and sources and keeps up to date. No automation yet.

**Requires:** OBS Studio 31 or newer.

## Getting a build

Every push to GitHub builds Laycue for Windows, macOS and Linux automatically.

1. On GitHub, open the **Actions** tab.
2. Click the latest **Push** run with a green tick.
3. Scroll down to **Artifacts** and download the one for your system:
   - Windows: `laycue-0.1.0-windows-x64-…`
   - macOS: `laycue-0.1.0-macos-universal-…`
   - Linux: `laycue-0.1.0-ubuntu-24.04-x86_64-…` (not the `-dbgsym` or `-sources` ones)

## Installing

Close OBS first.

**Windows**
1. Unzip the download and double-click `laycue-…-windows-x64-Installer.exe`.
2. If Windows shows a blue "Windows protected your PC" box, click **More info**, then **Run anyway**. This appears because the beta isn't code-signed yet.
3. Click **Yes** when Windows asks for permission, then **Next → Install → Finish**. If OBS is open, the installer offers to close it.
4. To remove Laycue later: Windows **Settings → Apps → Installed apps → Laycue for OBS Studio → Uninstall**.

The download also contains a plain zip (`laycue-…-windows-x64.zip`) for manual installs: copy its `laycue` folder into `C:\ProgramData\obs-studio\plugins\`.

**macOS**
1. Unzip the download and double-click the `.pkg` installer.
2. macOS will warn that it's from an unidentified developer, because the beta isn't code-signed. Right-click the `.pkg` → **Open** → **Open**.

**Linux (Ubuntu 24.04)**
1. Unzip the download and install the `.deb`: `sudo apt install ./laycue-…-x86_64-linux-gnu.deb`

## Opening the panel

1. Start OBS.
2. In the top menu, click **Docks** → **Laycue**.
3. The Laycue panel appears floating. Drag it by its title bar to wherever you like, for example next to Sources. OBS remembers where you put it.

To check that it loaded: **Help** → **Log Files** → **View Current Log**, and search for `[laycue] plugin loaded successfully`.

## Developing

See the **Commands** section of [`CLAUDE.md`](CLAUDE.md).
