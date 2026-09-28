# Laycue

A layout and layer manager for OBS Studio: one-button layouts, rules that stop overlays from colliding, and automatic fitting of cameras into frame graphics.

Laycue is a native OBS plugin. See [`PRD.md`](PRD.md) for what it does and why, and [`CLAUDE.md`](CLAUDE.md) for how it's built.

**Status:** working beta. Scene switching with live pictures (Go Live, Cut), Normal or Studio mode, overlay labels and rules, pushbacks with switchable cameras, layouts (split screen, picture in picture), an optional Laycue view that hides OBS's own Preview and Program, Settings, profiles, Lock and hotkeys.

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

## Using Laycue

1. **Normal or Studio.** In **Settings → Working with OBS**, **Normal** sends a scene straight to the stream. **Studio** turns on OBS's Studio Mode, and Laycue runs Preview (Up next) and Program (Live). Turn on **Laycue view** there to hide OBS's own Preview and Program screens so Laycue fills the window (turn it off to bring them back; you need them to drag things around on the canvas).
2. **Shots.** Tap a scene to get it ready (Up next). Press **Go Live** (uses your OBS transition) or **Cut** (instant). Nothing changes on the stream until you do. Space also presses Go Live while the panel is selected.
3. **Overlays.** Open the **Overlays** tab → **Set up overlays**. Label your sources (right-click one, or press **Auto-label**). Overlays that cover the same part of the screen replace each other; change that under **Overlay rules**.
4. **Layouts.** Open **Layouts** → **New layout** to put things on screen together: pick a shape (side by side, big and small, picture in picture, top and bottom, four squares, full screen), choose what goes in each part, name it. Laycue builds a scene for it; tap it and **Go Live**.
5. **Pushbacks.** Open **Layouts** → **Add pushback**. Choose the graphic (an image file, or a source already in OBS), say if its window is see-through or a colour, choose which cameras can show inside, and name it. Laycue makes one scene for it. Tap it, **Go Live**, then use the camera buttons under Live to switch the camera inside the window. **Unpin** goes back to what was on before.
6. **Settings** (the gear): cut or fade inside pushbacks, large buttons for a touchscreen, tips, **profiles** (save your setup under a name and load it in another scene collection), and **Lock setup for the service**.
7. **Hotkeys.** OBS **Settings → Hotkeys**, search for "Laycue": Go Live, Cut, Unpin, next/previous camera, clear Up next.

Your labels, rules, pushbacks and settings are saved with the OBS scene collection.

## Developing

See the **Commands** section of [`CLAUDE.md`](CLAUDE.md).
