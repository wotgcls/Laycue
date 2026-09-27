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
1. Unzip the download. Inside is another zip, `laycue-…-windows-x64.zip`. Unzip that too.
2. You'll see a folder called `laycue`. Copy it into `C:\ProgramData\obs-studio\plugins\`. Create the `plugins` folder if it doesn't exist. `ProgramData` is hidden: paste the path into File Explorer's address bar.
3. The result should be `C:\ProgramData\obs-studio\plugins\laycue\bin\64bit\laycue.dll`.

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
