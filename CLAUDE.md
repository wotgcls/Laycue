# CLAUDE.md — Laycue

Read `PRD.md` first. It defines what we're building and why. This file defines how.

## What Laycue is

A layout and layer manager for OBS Studio, built as a **native OBS plugin** (C++17, libobs, OBS frontend API, Qt 6). It adds a **Laycue dock** (panel) to OBS and registers **OBS hotkeys**. It is not a browser dock and does not use obs-websocket. Never propose converting it into one unless explicitly asked.

## About the owner

The owner builds with Claude Code and does not write code by hand. So:
- Explain what you changed in plain language at the end of each task.
- When something needs doing in OBS or on GitHub (install a build, open a menu), give exact click-by-click steps.
- Prefer simple, readable code over clever code.
- Ask before adding any new dependency (library, tool, GitHub Action), and say why it's needed.

## The guiding principle (from the PRD)

The operator is in control. Every automation is opt-in, scoped and pausable. Laycue only touches the scenes and sources the operator has configured, in the way they chose. When in doubt, pick the default least likely to surprise someone mid-service, and ask before doing anything to the user's setup (adding filters, reordering sources).

## Stack

- **C++17**, **CMake**, built from the official **obs-plugintemplate** (its `cmake/`, `.github/` and `buildspec.json` came from it).
- **libobs** + **obs-frontend-api** for everything OBS.
- **Qt 6 Widgets** for the dock UI, styled to the **Laycue brand system** (`assets/brand/README.md`, source https://claude.ai/artifact/H435m1pPCfoLBUsLWPA1rj): Ink `#111110` base, Graphite `#1C1B19`/`#2C2A27`, Cream `#F1EBDD` for text, output and primary actions; one job per colour: Tally Red `#FF4D2E` = on air only, Cue Green `#2FD18B` = up next, Overlay Amber `#FFB31A` = overlays and notices, Signal Blue `#2E8BFF` = setup/edit. Archivo (bundled, OFL) with system UI font fallback; JetBrains Mono for hotkeys. Controls: 8 px radius, 36 px default / 44 px touch, spacing 4/8/14/24, motion 120 ms ease-out. Keep all tokens in one tokens file in `src/ui/`; never scatter hex values through widgets. Logo images live in `data/brand/` (loaded with `obs_module_file`). No translucency/blur (Qt docks can't do it reliably).
- **Tests:** pure C++ in `tests/`, a standalone CMake project with a tiny in-repo check helper (`tests/check.hpp`). No test library.
- Target OBS version is pinned in `buildspec.json` (currently 31.1.1). Only use APIs available in that version.
- No network, no backend, no accounts.

## Commands

Local (Linux; this is how Claude checks work in its own environment):
- `cmake --preset ubuntu-x86_64 && cmake --build --preset ubuntu-x86_64`: build the plugin
- `cmake -S tests -B build_tests && cmake --build build_tests && ctest --test-dir build_tests --output-on-failure`: run unit tests (no OBS needed)
- `clang-format -i src/**/*.cpp src/**/*.hpp tests/*.cpp tests/*.hpp`: format C++ (CI uses clang-format **19**)
- `gersemi -i CMakeLists.txt tests/CMakeLists.txt`: format CMake files (CI checks this too)

CI (GitHub Actions, from the template): every push builds Windows, macOS and Linux installers. The owner downloads them from the run's **Artifacts** section.

Always build the plugin, run the tests and run both formatters before saying a task is done.

## Project structure

```
src/
  plugin-main.cpp        # module load/unload: creates the App, hotkeys and the dock, hooks OBS events
  plugin-support.*       # from template: PLUGIN_NAME, PLUGIN_VERSION, obs_log()
  obs/                   # the ONLY place that talks to libobs directly
    scene-watcher.*      # subscribes to scene/source signals (incl. item_visible, groups)
    scene-reader.*       # reads scenes, items and canvas into core structs
    overlay-items.*      # overlay items: read, show/hide, measure the visible-pixel spot
    pixel-reader.*       # image files (OBS's decoder) and sources drawn WITH their filters
    pushback-scene.*     # builds/drives a pushback's own scene, camera fit, Chroma Key tuning
    layout-scene.*       # builds a layout's own scene (split screen etc.), one source per slot
    scene-util.*         # helpers shared by pushback/layout scenes (place item, canvas size)
    frame-grabber.*      # Live/Up next/shot pictures on the graphics thread
    switcher.*           # Go Live / Cut, studio mode
  core/                  # PURE C++17. No libobs, no Qt. Fully unit tested.
    detect.*             # window detection on an RGBA pixel buffer
    geometry.*           # rects, overlap rule, spot mapping, fill/fit, picture placement
    autotag.*            # suggests an overlay label from name and position
    exclusivity.*        # overlay rules: which items to hide (scope, strict, pause)
    layouts.*            # layout templates (slots as canvas fractions) and slot rectangles
    config.*             # everything Laycue remembers (tags, rules, pushbacks, settings)
    cue-state.*          # Live / Up next rules
    source-kinds.*       # friendly names for OBS source types
  features/              # glue core logic to OBS
    app.*                # the one top-level object: owns the Config, saves it with the collection
    overlay-engine.*     # applies the overlay rules when an item is shown
    profiles.*           # named setups, matched by name, export/import
    hotkeys.*            # OBS hotkeys (hotkey thread -> UI thread)
  store/config-io.*      # Config <-> obs_data (saved under modules.laycue) and JSON
  ui/                    # Qt widgets: the dock, controls, pages, tabs
    laycue-dock.*        # the panel: header tabs, stage, tabs, pages
    controls.*           # Surface (depth), Segmented, Switch, ActionButton, GearButton, TipCard
    widgets.*            # PictureView, ShotCard, FeedButton
    pages/               # Set up overlays, Add pushback, New layout, Settings, PageFrame
    view-takeover.*      # opt-in "Laycue view": hides OBS's central Preview/Program widget
    tabs/                # Overlays tab
tests/                   # standalone CMake project testing src/core (+ the real pushback fixtures)
data/locale/en-US.ini    # all user-visible text
```

**Rule:** anything mathematical or rule-based goes in `src/core` as pure functions with tests. `features/` only glues core logic to OBS.

## OBS facts you must respect

**Identity**
- Scene items are identified by scene + `obs_sceneitem_get_id()`. The same **source** can appear in many scenes as different items.
- Store sources by **UUID** (`obs_source_get_uuid`) plus the last known name for display. UUIDs survive renames.
- Canvas size comes from `obs_get_video_info()` → `base_width`, `base_height`. Never hardcode 1920×1080.

**Reading scenes**
- `obs_frontend_get_scenes()` (free with `obs_frontend_source_list_free`), `obs_scene_from_source`, `obs_scene_enum_items` (bottom-to-top order), `obs_sceneitem_is_group` + `obs_sceneitem_group_enum_items`.
- Program scene: `obs_frontend_get_current_scene()`. Studio mode: `obs_frontend_preview_program_mode_active()`, preview scene `obs_frontend_get_current_preview_scene()`. Both return new references: release them.

**Changing things**
- Visibility: `obs_sceneitem_set_visible`. Transform: `obs_sceneitem_set_info2` + `obs_sceneitem_set_crop`.
- Always use top-left alignment (`OBS_ALIGN_TOP | OBS_ALIGN_LEFT`) and **bounds** so positions are predictable.
- For an exact fill: crop the subject to the window's aspect ratio, then `OBS_BOUNDS_STRETCH` with bounds = window size.
- Wrap multi-property changes to one item in `obs_sceneitem_defer_update_begin/end` so no half-updated frame renders.

**Signals and events**
- Frontend events (`obs_frontend_add_event_callback`) run on the UI thread: `FINISHED_LOADING`, `SCENE_LIST_CHANGED`, `SCENE_CHANGED`, `PREVIEW_SCENE_CHANGED`, `STUDIO_MODE_ENABLED/DISABLED`, `SCENE_COLLECTION_CLEANUP`, `SCENE_COLLECTION_CHANGED`, `EXIT`.
- Per-scene signals (on the scene source's signal handler): `item_add`, `item_remove`, `item_visible`, `reorder`, `refresh`, `item_transform`. Global signals (`obs_get_signal_handler()`): `source_rename`, `source_destroy`.
- **Signals can fire on any thread.** Never touch Qt widgets from a signal callback. Post to the UI thread (`QMetaObject::invokeMethod(..., Qt::QueuedConnection)` or `obs_queue_task(OBS_TASK_UI, ...)`).
- Disconnect every signal you connect. Hold scenes by **weak reference** so you can safely disconnect a scene that may already be gone.
- Per-frame work (animations) uses `obs_add_tick_callback`; remove it on unload.

**Pixels (M1+)**
- Detection renders the frame source *with its filters* into a `gs_texrender` via `obs_source_video_render`, then reads it back through a `gs_stagesurface`. Graphics calls only inside `obs_enter_graphics()/obs_leave_graphics()`, even in a graphics-thread task (`obs_queue_task(OBS_TASK_GRAPHICS)` does not enter the context for you; `gs_texrender_create` returns null without it). Clear to transparent and use separate alpha blending so see-through areas stay see-through. Verified in M1: filters (Chroma Key) are included. Image files are decoded with OBS's own `gs_image_file_init` (PNG, JPG, WebP on every platform), not QImage.

**Saving**
- Config is saved with the scene collection via `obs_frontend_add_save_callback` (an `obs_data_t` under key `laycue`). One JSON object with a `version` number. Write a migration whenever the schema changes.
- Hotkeys: `obs_hotkey_register_frontend`; save/load their bindings in the same save callback.

## Crash safety (non-negotiable)

A crash here takes down a live stream.
- Every callback OBS calls into (events, signals, ticks, hotkeys, save) catches all exceptions: `try { ... } catch (...) { obs_log(LOG_ERROR, ...); }`. Nothing throws across into OBS.
- Check every pointer returned by libobs for null.
- Every `addref`/`get_ref`/`get_*` that returns a reference is released. Prefer the RAII types in `<obs.hpp>` (`OBSSourceAutoRelease`, `OBSWeakSourceAutoRelease`, …).
- Tear down in reverse order: on `SCENE_COLLECTION_CLEANUP` and `EXIT`, disconnect signals and stop animations before OBS frees the scenes.
- Never leave a source half-moved. If an animation is interrupted, jump to its final transform.
- Never delete or rename the user's scenes or sources. Never add filters or reorder sources without the operator's confirmation.
- Errors go to the OBS log (`obs_log`) and to the dock in plain language.

## Key algorithms

### Hole detection (`core/detect`)
1. Input: RGBA pixels of the frame source, rendered by OBS *after* its filters.
2. Binary mask: pixel is "hole" if alpha < threshold (default 16). For green frames, OBS's Chroma Key filter makes the green transparent first. A raw chroma-distance mode (CbCr distance, default tolerance 28) exists as a fallback and for the upload preview. **Never assume pure green**: sample the key colour as the most common strongly-green colour in the image (g > r+60 and g > b+60), and use that same colour for the Chroma Key filter Laycue adds. Real designs use e.g. `#00B13F`, which pure-green matching misses entirely (verified on the owner's pushbacks). On upload, try see-through first and fall back to green automatically.
   **Chroma Key strength:** OBS's default Similarity (400) keys out far more than the green for darker greens like `#00B13F`: on a dark navy design it made the *whole* image transparent (found in M1). When Laycue adds a Chroma Key filter (M2), it must choose a lower Similarity (about 150 worked) and verify with detection that the window is not ≥95% of the image.
3. Find the largest connected region of hole pixels.
4. Return its bounding rectangle and fill ratio (hole pixels ÷ rectangle area). Under 0.9 → low-confidence flag.
5. Detect on a downscaled copy (~640px wide), scale the rectangle back, refine edges at full size.

### Mapping to canvas (`core/geometry`)
The detected rectangle is in the frame source's own pixels. Convert to canvas coordinates using the frame item's transform (position, scale/bounds, crop). Then compute the subject transform:
- **fill:** crop the subject to the window's aspect ratio (evenly), stretch to the window.
- **fit:** scale the subject to fit inside the window, centred.

Smart fit is **instant** (no animation) and is applied ahead of time in every scene where the pairing is active and the frame is enabled.

### Animation (`core/tween`)
Interpolate position, bounds and crop between start and end with ease-in-out, one step per rendered frame (tick callback), default 400ms. Always apply the exact final transform last. A new animation on the same item replaces the old one, starting from where it currently is.

### Exclusivity (`core/exclusivity`)
Pure function. Input: "item X (source S) became visible in scene N", every group containing S (members, scope: same scene / chosen scenes / all scenes, paused flag, strict flag), and each in-scope visible member's **spot** (a canvas rect). Output: the items to hide.
- Non-strict (default): hide a member only if its spot overlaps X's spot. Ignore overlaps smaller than 2% of the smaller spot's area or under 8 px in either direction.
- Strict ("only one at a time, wherever they are"): hide every other visible member in scope.
- Never return items outside the scope. Paused groups return nothing.
- A **spot** = the source's visible-pixel bounding box (measured with the same readback as hole detection, alpha > threshold) mapped to canvas through that scene item's transform, or the operator's manual rect if set. Rect intersection and the tolerance rule live in `core/geometry` with tests.
- Ignore changes Laycue caused itself (reentrancy guard around Laycue's own `set_visible` calls).

## Lessons that cost time (read before touching the UI)

- **Never call `tr()` inside a QObject class for Laycue text.** It resolves to Qt's `QObject::tr` and returns the key. Use `ui::lt("Laycue.Key")` (`ui/labels.hpp`).
- **OBS's theme distorts QSS-styled `QPushButton`s** (height, offset inside their cell, clipping). Laycue draws its own buttons (`ActionButton`, `FeedButton`, `GearButton`, `Segmented`, `Switch`, `ShotCard`). Do not add new QSS-styled buttons. Labels are fine.
- **Qt ignores zero margins on nested layouts** here; prefer separate widgets that each own a top-level layout.
- **`obs_queue_task(OBS_TASK_GRAPHICS)` does not enter the graphics context.** Wrap graphics work in `obs_enter_graphics()/obs_leave_graphics()`.
- **A source nothing references is destroyed when you release it.** Hold it (`SourceHold`) until a scene has it.
- **Frontend save data lands under `modules` in the collection file.** Laycue's config is `modules.laycue`, hotkeys `modules.laycue_hotkeys`.
- **OBS swaps Preview and Program after a transition.** After Go Live the panel ignores Preview changes for a moment (`kSuppressGraceMs`), so that swap isn't read as the operator cueing something.
- **Never name a member `slots`, `signals` or `emit` in a header the UI includes**: Qt defines them as macros. Use `sources`, `shapes`.
- **Laycue view hides OBS's central widget** (`QMainWindow::centralWidget()`), not a widget found by name: OBS's inner widget names differ between versions. It restores it on turning off and on exit.
- **Hotkey callbacks run on OBS's hotkey thread**; post to the UI thread. Signals can fire on any thread too.

## Coding rules

- Warnings are errors in CI. Keep code warning-free on GCC, Clang and MSVC.
- No raw `new` without a Qt parent or an owner. No global mutable state except the plugin's single top-level object.
- User-visible text comes from `data/locale/en-US.ini` via `obs_module_text`.
- Keep the dock usable at 320px wide.
- Small commits with clear messages, one milestone step at a time.

## Working process

- Work milestone by milestone, in the order of PRD section 10: M0 → M1 → M2 → M3 → M4 → Sunday test 1 → M5 → M6 → Sunday test 2 → M7 → M8. Each milestone ends with a Windows build and a test checklist for the owner.
- At the start of a task, state which milestone and requirement it serves.
- For new core logic, write tests first or alongside.
- After finishing, give: what changed, how to install the build and test it in OBS step by step, and anything that still isn't done.
- If a requirement in the PRD seems wrong or impossible, say so and propose an alternative instead of silently changing it.

## Definition of done

The plugin builds locally and in CI on all three platforms, tests pass, formatting passes, the feature works in real OBS following the test steps you wrote, and the PRD's "done when" criteria for that feature are met.
