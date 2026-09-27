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
- **Qt 6 Widgets** for the dock UI, styled with Laycue's own design tokens (see PRD §7a and the mockup linked there): dark surfaces close to OBS's, system font, one meaning per colour (red = live, green = next, blue = setup). Keep all colours, radii and spacing in one tokens file in `src/ui/`; never scatter hex values through widgets. No translucency/blur (Qt docks can't do it reliably).
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
  plugin-main.cpp        # module load/unload: creates the dock, hooks OBS events
  plugin-support.*       # from template: PLUGIN_NAME, PLUGIN_VERSION, obs_log()
  obs/                   # the ONLY place that talks to libobs directly
    scene-watcher.*      # subscribes to scene/source signals, reports "something changed"
    scene-reader.*       # reads scenes, items and canvas into core structs
  core/                  # PURE C++17. No libobs, no Qt. Fully unit tested.
    source-kinds.*       # friendly names for OBS source types
    (later) geometry.*   # rects, fit/fill math, crop calculation, coordinate mapping
    (later) detect.*     # hole detection on an RGBA pixel buffer
    (later) layouts.*    # preset definitions and slot → transform maths
    (later) exclusivity.*# group rules + scope: given a change, which items to hide
    (later) tween.*      # easing and interpolation of transforms
  features/              # glue core logic to OBS (smartFit, layoutEngine, switcher, exclusivityEngine)
  store/                 # config types, version number, migrations, save/load
  ui/                    # Qt widgets: the dock and its panels
tests/                   # standalone CMake project testing src/core
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
- Detection renders the frame source *with its filters* into a `gs_texrender` via `obs_source_video_render`, then reads it back through a `gs_stagesurface`. Graphics calls only inside `obs_enter_graphics()/obs_leave_graphics()` or a graphics-thread task. Verify in M1 that filters (chroma key) are included.

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

## Coding rules

- Warnings are errors in CI. Keep code warning-free on GCC, Clang and MSVC.
- No raw `new` without a Qt parent or an owner. No global mutable state except the plugin's single top-level object.
- User-visible text comes from `data/locale/en-US.ini` via `obs_module_text`.
- Keep the dock usable at 320px wide.
- Small commits with clear messages, one milestone step at a time.

## Working process

- Work milestone by milestone, in order: M0 → M1 → M2 → M3 → M3.5 → M4 (see PRD).
- At the start of a task, state which milestone and requirement it serves.
- For new core logic, write tests first or alongside.
- After finishing, give: what changed, how to install the build and test it in OBS step by step, and anything that still isn't done.
- If a requirement in the PRD seems wrong or impossible, say so and propose an alternative instead of silently changing it.

## Definition of done

The plugin builds locally and in CI on all three platforms, tests pass, formatting passes, the feature works in real OBS following the test steps you wrote, and the PRD's "done when" criteria for that feature are met.
