# Laycue — Product Requirements Document (MVP)

**Status:** Draft v0.6 · **Owner:** Laycue founder · **Last updated:** 27 Sep 2026

**Changes from v0.1:** Laycue is now a native OBS plugin (was a browser dock). Added the operator-control principle. Exclusivity scope is now chosen per group. Smart fit is instant and can be limited to chosen scenes. Studio mode, hotkeys, the "Reset all" button and scene-collection storage are now covered. The done-when criteria are rewritten so they can be tested.

**Changes in v0.3:** Added F6 Switcher mode (vision-mixer style Preview/Program buttons with Cut and Auto, no scene building) as milestone M3.5.

**Changes in v0.4:** Switcher mode has two views, **Shots** (default, for volunteers) and **Mixer** (for experienced operators), plus per-view customisation. Operators can upload a pushback design straight into Laycue (F1). Added a design direction (section 7a) and a clickable mockup link.

**Changes in v0.5:** F3 rewritten: overlay tags and position-aware exclusivity ("spots"). New-shot flow defined. Added section 3a (two roles: set-up person and Sunday operator) and section 10a (release plan: a smaller first release).

**Changes in v0.6:** Release plan approved and written into section 10. Shots view refined after a first-time-operator critique (Go Live/Cut, Up next clears after going live, still thumbnails for shots, overlay spot icons, explanations when overlays switch off, starter shots).

---

## 1. One-liner

Laycue is a layout and layer manager for OBS. It gives live producers one-button layouts, rules that stop overlays from colliding, and automatic fitting of cameras into frame graphics, so nobody has to resize sources by hand during a live show.

## 2. The problem

OBS gives you the building blocks but no "vision mixer" brain on top. In a live service or event today:

- **Every layout is a separate scene.** PIP, side-by-side and quad all have to be built by hand, one scene each, and rebuilt whenever a source changes.
- **Overlays collide.** Lyrics, scripture, lower thirds and announcements are independent toggles. Nothing stops two from showing at once, so operators hide and unhide layers manually, or duplicate scenes to work around it.
- **Frame graphics ("pushbacks") need manual resizing.** When a graphic with a green or transparent window goes on screen, the operator has to drag and scale the camera to fit the window, live, every time.

The result is slow, error-prone switching and a stressed operator. Hardware mixers (ATEM, Roland) solve this with presets. OBS users have nothing equivalent.

**Evidence of demand:** OBS forum threads request saveable layout presets, animated PIP swapping, multiple saved transforms per source, and "exclusive source visibility".

**Existing tools and where Laycue differs:**
- *Move Transition* (plugin) animates sources between scenes and positions. Laycue's layouts overlap with it; Laycue's edge is slot-based presets for non-technical operators.
- *Advanced Scene Switcher* (plugin) can imitate exclusive visibility with hand-built macros. Laycue makes it a two-click setup.
- **Nobody automatically fits a camera into a graphic's window.** Smart fit is Laycue's unique feature and leads the positioning.

## 3. Who it's for

- **Primary:** church and event media operators running OBS, often volunteers, under live pressure.
- **Secondary:** streamers, conference and podcast producers running multi-source shows.

**Primary persona: "The Sunday operator."** Runs OBS alone or with one helper, often in studio mode. Knows scenes and sources, doesn't write code. Needs things to work first time, live, with no fiddling.

## 3a. Two roles: who sets it up, who runs it

Most churches split the work, and Laycue is designed for both people:

- **The set-up person** (media lead, often the most technical volunteer) prepares Laycue once, during the week: tags overlays, adds pushbacks, builds shots, sets hotkeys. They need a **guided first-run setup**, a short checklist in the panel ("1. Tag your overlays · 2. Add a pushback · 3. Make your shots"), and empty states that explain what each area is for.
- **The Sunday operator** (a volunteer, sometimes new each week) only runs the show: taps shots, presses Cut or Auto, turns overlays on and off. They should never need to configure anything live. A **Lock setup** switch hides all editing controls during the service, so nothing can be changed by accident.

## 4. Guiding principle: the operator is in control

Laycue automates what the operator asks for, and nothing else.

- **Every automation is opt-in and adjustable.** Nothing happens until the operator sets it up, and every rule can be scoped, paused or turned off.
- **Laycue only touches the scenes and sources the operator has configured**, in the way they chose. It never reaches into other scenes or sources "helpfully".
- **Safe defaults.** When there's a choice, the default is the one least likely to surprise someone mid-service.
- **Always undoable.** A "Reset all" button puts every source Laycue has moved back where it was.
- **Never break the show.** If something fails, Laycue shows a plain-language message and leaves OBS running and sources in a sensible place.

## 5. Goals and non-goals

### Goals (MVP)
1. Switch between common layouts with one click (or hotkey), with smooth animation.
2. Let operators tag sources as mutually exclusive, with a scope they choose, so showing one hides the others automatically.
3. Drop in a frame graphic and have the camera appear in its window instantly.
4. Install in under five minutes with a normal installer.
5. Optionally, run a whole show from Laycue like a hardware vision mixer (pick the next shot, press Cut or Auto) without building a scene for every combination.

### Non-goals (MVP)
- Building a video engine or replacing OBS. OBS does the pixels.
- Broadcast lower-third graphics or animation templates.
- Content-aware reframing (subject detection). Planned for v2.
- Tracking moving windows inside animated graphics. v1 handles static windows only.
- vMix or other software support.
- Accounts, cloud sync, payments.
- Paid code signing for the beta. The unsigned installer warnings are accepted for now.

## 6. How it works (user view)

Laycue is a **native OBS plugin**. The user runs the installer for their system (Windows, macOS or Linux) and restarts OBS. A **Laycue panel** appears (Docks → Laycue) and Laycue's actions appear in **Settings → Hotkeys**. There's nothing to connect and no password to enter, because Laycue runs inside OBS.

## 7. Features and requirements

### F0 — Install and load
- Installers for Windows, macOS and Linux, built automatically on GitHub.
- After installing and restarting OBS, the Laycue panel is available and lists the scene collection's scenes and sources.
- Laycue never crashes OBS. Errors are shown in the panel in plain language and written to the OBS log.

**Done when:** a first-time user installs and sees the Laycue panel in under five minutes, and the panel's scene/source list stays correct when scenes and sources are added, renamed, removed or the scene collection is switched.

### F1 — Smart fit (frame graphics) · *build first*
- **Two ways to add a pushback:**
  - **Upload a design** in Laycue: the operator chooses an image file (PNG with a see-through window, or an image with a green window). Laycue shows the window it found straight away, outlined on the image, with a preview of the camera placed inside it. The operator names it, picks the camera and the fill mode, and adds it. Laycue then offers to add the image to the scenes the operator chooses (asking first) and it becomes a **shot** in Shots view.
  - **Use an existing source**: the operator marks a source already in OBS as a **frame** and picks its **subject** (usually a camera).
- **Green windows are keyed automatically** for pushbacks uploaded in Laycue: a "Remove green automatically" switch (on by default) adds OBS's Chroma Key filter to the image Laycue created, with the key colour taken from the green actually measured in the image. For a graphic already in the operator's scenes, Laycue asks once before adding the filter, and reuses an existing key filter if there is one.
- If no clean window is found, Laycue says so in plain language ("No window found" or "Check the edges") and the operator can switch between see-through and green, or adjust the edges by hand.
- The operator chooses **which scenes** this pairing is active in (default: all scenes containing both the frame and the subject).
- The operator says how the window is defined: **transparent** (alpha) or **green screen** (chroma). For green, Laycue offers to add OBS's Chroma Key filter to the frame, and asks before adding it. Detection always measures the frame *after* its filters, so Laycue measures exactly the hole viewers see.
- Laycue finds the largest window in the frame and fits the subject into it.
- Default fit is **fill**: the subject covers the window fully and is cropped to it, keeping its aspect ratio. **Fit** (letterbox) is an option.
- **The fit is instant, not animated.** In every active scene where the frame is enabled, the subject is kept fitted *ahead of time*, including scenes that aren't live. So when the operator cuts to that scene or shows the frame, the camera is already in place.
- When the frame is hidden, the subject returns to its original position (operator can turn this off per pairing). The original position is saved, so it survives restarts and crashes.
- The frame must be above the subject in the Sources list. If it isn't, Laycue shows a clear warning and does not reorder anything by itself.
- Manual nudge is always available and saved per frame.
- Detected windows are cached per frame, so fitting is instant after the first detection. A "Re-detect" button refreshes it.
- While a frame is showing, it owns its subject: layouts (F2) don't move that subject until the frame hides.

**Done when:** for a static frame graphic at 1920×1080, the subject lands within 2px of the window edges, with no visible wrong-position frame when the frame is shown or its scene is cut to, without the user touching a transform handle. Detection works on at least five real pushback graphics from our own services.

### F2 — Layout presets
- Built-in presets: **Full**, **PIP** (four corners, adjustable size), **Side-by-side**, **Two-box with background**, **Quad**.
- Each preset has named slots (A, B, C, D). The operator assigns sources to slots.
- One click (or a hotkey) applies the preset.
  - In **studio mode**, presets apply to the **Preview** scene by default, with a setting to apply to Program instead.
  - Outside studio mode, they apply to the current scene.
- **"Hide sources that aren't in a slot"** is an option per preset (default off). When it's on, it only affects sources assigned to a slot in any preset for that scene. Laycue never hides anything else.
- Changes animate smoothly, frame by frame, in step with OBS (default 400ms, ease-in-out), with a setting for instant.
- **Swap** button exchanges slots A and B, animated.
- Operators can save their own adjusted presets.
- Each preset and Swap can be triggered from OBS hotkeys (and so from a Stream Deck or keyboard).

**Done when:** switching between any two presets takes one click or one hotkey, animates without visible stutter at 30fps output, and never leaves a source half-positioned, even if interrupted by another preset mid-animation.

### F3 — Overlays: tags and position-aware exclusivity

**Tags.** The operator tags sources by what they are: **Lyrics, Scripture, Lower third, Announcement, Logo**, or a custom tag. Tags live in Laycue's settings only; OBS source names are never changed. Each overlay tag becomes a button in the panel's Overlays area. If two sources share a tag (for example two scripture styles), the button shows the chosen default and a small menu picks the other.

**The rule: overlays can share the screen unless they would cover each other.** When an overlay turns on, Laycue turns off only the overlays in scope whose **spot** overlaps its spot. Everything else stays up.

**Spots.**
- Each overlay has a spot: the part of the screen it covers, shown as a box on a small picture of the screen.
- Laycue measures it automatically from where the overlay actually draws pixels, not from its layer size. (Lyric and graphics tools often send a full-screen image with only a strip at the bottom filled; the spot is the strip.) Measuring reuses the smart-fit pixel reading (F1).
- The spot is calculated per scene from the item's position in that scene, so the same overlay placed differently in two scenes has two spots.
- The operator can adjust any spot by dragging, or pick a quick preset: Bottom, Top, Left, Right, Corner, Full screen. For overlays whose content changes size (lyrics), the spot should cover the largest area they use; Laycue offers "Re-measure" while the overlay is showing its largest content.
- Spots that only touch at an edge (overlap smaller than 2% of the smaller spot, or under 8 px) do not count as overlapping.

**Examples.**

| Turning on | Already showing | Result |
|---|---|---|
| Scripture (bottom) | Lyrics (bottom) | Lyrics turns off: they overlap |
| Logo (top right) | Lyrics (bottom) | Both stay: they don't touch |
| Name lower third (bottom left) | Lyrics (bottom, full width) | Lyrics turns off: they overlap |
| Announcement (full screen) | Logo, Lyrics | Both turn off: full screen covers everything |

**Groups and scope.**
- All overlay-tagged sources form one **Overlays** group automatically, so it works with no setup. Operators can make extra groups or take a source out of the group.
- Each group has a scope: **Same scene only** (default), **Chosen scenes** (tick existing scenes; unticked scenes are exceptions and are ignored), or **All scenes**. Laycue never creates, copies or deletes scenes for this; a group is only a rule Laycue watches.
- Each group has a pause switch, and an option **"Only one at a time, wherever they are"** (off by default) for people who want strict exclusivity regardless of position.
- A source can belong to more than one group. If two members turn on at nearly the same moment, the last one wins.
- Works whether the overlay is toggled in Laycue, in OBS, by a hotkey or a Stream Deck.
- The Overlays area shows the small screen picture with each live overlay's spot, so the operator can see why something turned off.

**Done when:** in a test with five tagged overlays (two bottom, one top-right logo, one bottom-left name, one full-screen announcement) across four scenes, for each scope setting: turning on any overlay by any normal OBS action turns off exactly the overlapping overlays in scope by the next rendered frame, leaves non-overlapping ones up, and never changes anything outside the scope.

### F4 — Settings and persistence
- All configuration (frames, presets, groups, hotkeys) is saved inside OBS **with the scene collection**, because it refers to that collection's scenes and sources. Switching scene collection switches Laycue's configuration with it.
- Export and import configuration as a JSON file.
- Renaming a source in OBS keeps Laycue's configuration pointing at it.

### F5 — Reset all (safety)
- A clearly visible **Reset all** button puts every source Laycue has moved back to its saved original position, and cancels running animations.

### F6 — Switcher mode (optional, operator turns it on)

Run the show like a hardware vision mixer (ATEM, Roland) instead of building one scene per shot.

**How it works**
- When the operator turns Switcher mode on, Laycue asks permission, then creates two scenes of its own: **"Laycue A"** and **"Laycue B"**. It only ever changes these two scenes, never the operator's own.
- The operator chooses which sources the switcher uses (cameras, slides, graphics, overlays). Laycue adds them to both of its scenes. The sources are shared, not copied, so their settings and audio stay the same.
- One Laycue scene is on air (**Live**). Laycue builds the next shot in the other one (**Next**).
- **Works with or without OBS studio mode; the operator chooses:**
  - *Studio mode off* (simplest): OBS's big view shows Live; the Laycue panel shows Next and the controls.
  - *Studio mode on*: OBS's two big views show Next (Preview) and Live (Program); the Laycue panel mirrors them small.
  - For a separate Live screen, OBS's own projector is used (right-click the view → Windowed/Fullscreen Projector). Laycue does not add its own pop-out window.
- The panel shows mixer-style controls:
  - **Preview row:** pick the source for the next shot.
  - **Program row:** shows what's on air. Clicking a Program button cuts to that source immediately.
  - **Layout row:** Full, PIP, Side-by-side, Two-box, Quad, and pushback frames (F1 smart fit).
  - **Slot picker:** for multi-box layouts, choose which source fills which slot (A, B, C, D).
  - **Overlays row:** toggles for lyrics, scripture, lower thirds and so on, following exclusivity groups (F3).
  - **Cut:** switches instantly. **Auto:** switches using the transition selected in OBS (fade, stinger, wipe…), with its duration.
- **Two views of the same switcher**, switched with one click at the top of the panel:
  - **Shots** (default): the operator taps a ready-made shot ("Pastor + slides", "Sermon pushback", "Band close") to cue it as Next, then presses Cut or Auto. Shots are set up in advance (a shot = layout + sources, or a pushback + camera), each with a small picture of how it looks and its hotkey. Built for volunteers: no slots or layouts to think about live.
  - **Mixer**: Next and Live rows of source buttons, layout picker, slot picker and swap, for experienced operators.
  - Both views share the same Live picture, Next picture, Cut, Auto and Overlays, so switching views never changes what's on air.
- **New shot** (three steps, no blank canvas): (1) pick a layout (Full, Picture-in-picture, Side by side, Two-box, Quad, Pushback); (2) tap each box and choose its source, with a live preview; (3) save — Laycue names it from its contents ("Pastor + Slides") and assigns the next hotkey. **Adjust** lets the operator drag and resize boxes (saved as their own version); **Duplicate** copies a shot to change one thing. A free-form layout editor is out of scope.
- **Shots view details (from the first-time-operator critique):**
  - The main button is **Go Live** (fades, using the OBS transition) with **Cut** (instant) beside it. No "Auto"/"Program"/"Preview" jargon in the Shots view.
  - Tapping a shot only puts it in **Up next**; nothing changes on the stream until Go Live. The panel says so ("Tap to get ready · nothing goes live until Go Live").
  - After Go Live, Up next **clears** (rather than swapping the old shot back in), so pressing Go Live twice can never jump back by accident. Go Live and Cut are disabled until a shot is cued.
  - **Thumbnails:** the Live and Up next pictures are live video. Each shot card shows a real still picture built from the actual sources, refreshed every few seconds and when the shot is edited. Not live video, to save computer power during the stream.
  - Overlay buttons show a tiny screen with the overlay's spot and say "On Live" or "Off". When one overlay switches another off, a short message explains it ("Scripture replaced Lyrics · same spot").
  - Every shot card shows its hotkey; Space = Go Live.
  - When setup is locked, the Mixer switch, New shot and all editing are hidden; a Locked pill in the header unlocks it.
- **Starter shots:** on first run, Laycue makes one full-screen shot per camera plus Slides, and guesses overlay tags from source names (Lyrics, Scripture, Name, Logo), so the panel is useful before any setup.
- **Customisation** (per scene collection): which sources appear and in what order, show or hide sections (for example hide Overlays), normal or large buttons (touchscreens), rename and reorder shots.
- After each Cut or Auto, the two scenes swap roles and Laycue prepares the next shot in the one now off air.
- Every button can be triggered by an OBS hotkey (and so by a Stream Deck or keyboard).
- The operator's own scenes keep working. They can be switched to directly (for a countdown or pre-service loop), and Switcher mode picks up again when a Laycue scene is next taken.
- Turning Switcher mode off leaves the two Laycue scenes in place (the operator can delete them).

**Known limitations**
- Media sources set to "restart when becomes active" restart when switched in. Normal scenes behave the same way.
- Overlays toggled while a transition is running apply to the incoming shot.

**Done when:** a four-camera service with slides, one pushback frame and three overlays can be run entirely from the Laycue panel (or hotkeys), without the operator building any scene. Cut is instant, Auto uses the OBS transition, and the on-air picture never shows a half-built shot.

## 7a. Design direction

- **Premium, calm, fast.** The panel should feel better made than OBS itself while clearly belonging inside it: same dark family, quieter chrome, more deliberate spacing and type.
- **The hero is the Live picture and the Next shot.** Everything else is quiet and near-monochrome.
- **Colour has meaning only:** red = on air, green = next (broadcast tally convention), blue = setup actions. No decorative colour.
- **Follows Apple's Human Interface principles:** system font, clear hierarchy, rounded surfaces, instant press feedback, grouped settings lists, plain-language labels, and reduced-motion support.
- **Keyboard-first:** every shot and button shows its hotkey.
- **Clickable mockup:** https://claude.ai/artifact/RnVjdtYU11GCujsmbVrb7x (Shots view, Mixer view, pushback upload, and the panel docked in OBS).

## 8. Technical approach (summary)

Full detail lives in `CLAUDE.md`.

- Native OBS plugin in C++17, built from the official `obs-plugintemplate`, using libobs, the OBS frontend API and Qt 6 for the panel.
- GitHub Actions builds installers for Windows, macOS and Linux on every push.
- Hole detection renders the frame source (with its filters) inside OBS and reads its pixels directly. There are no screenshots and no network.
- Layout animation updates transforms once per rendered frame, in step with OBS.
- Exclusivity and smart fit react to OBS's own signals, inside OBS, before the next frame is drawn.
- Configuration is saved with the scene collection via the frontend save callback.
- All geometry, detection, layout and exclusivity logic is pure C++ with no OBS dependency, fully unit tested.

## 9. Risks

| Risk | Impact | Mitigation |
|---|---|---|
| A plugin bug crashes OBS mid-service | Stream goes down | Defensive coding rules in CLAUDE.md; no exceptions cross into OBS; pure logic is unit tested; M5 Sunday test before public beta |
| OBS updates break the plugin | Plugin fails to load | Pin the OBS version in `buildspec.json`; rebuild and test on each major OBS release |
| Unsigned installers show security warnings | Users hesitate to install | Clear install guide with screenshots; consider signing after beta |
| Detection fails on soft or feathered edges | Camera doesn't fit | Adjustable threshold, manual nudge, cached result, detect after OBS's own chroma key |
| Exclusivity loops (Laycue reacting to its own changes) | Flicker or freezes | Ignore signals caused by Laycue's own changes |
| Features fight over the same source | Sources jump around | Ownership rule: a showing frame owns its subject |
| Slower build/test loop than a web app | Slower development | Pure logic tested without OBS; GitHub builds installers automatically |
| Low willingness to pay | No revenue | Release free first, measure adoption before pricing |

## 10. Release plan and milestones (approved)

Every milestone ends with a **Windows build** the owner installs, plus a short test checklist. Windows is the primary test platform (the owner's church PC); macOS and Linux builds are produced by CI but tested less often.

**Release 1 · Sunday-ready core** (works inside the operator's existing scenes)

| # | Milestone | Outcome |
|---|---|---|
| M0 | Plugin skeleton ✓ | Installers build on GitHub; plugin loads in OBS; panel lists scenes and sources |
| M1 | Window finder | In the panel, pick a pushback image or an OBS source and see the detected window. Passes on the three real pushbacks in `tests/fixtures/pushbacks`. Confirms detection works after OBS's Chroma Key |
| M2 | Smart fit | Pushback upload with auto chroma key (measured green), camera and scene choice, instant fit, restore on hide, saved with the scene collection, Reset all |
| M3 | Overlays | Tags, spots and position-aware exclusivity, scope, pause (F3) |
| M4 | Setup and safety | Guided first-run checklist, Lock setup, plain-language errors; Windows installer if approved |
| S1 | Sunday test 1 | Used live for 2 services; bugs fixed before Release 2 |

**Release 2 · Switcher**

| # | Milestone | Outcome |
|---|---|---|
| M5 | Layouts | Presets, slots, animated moves, swap (F2) |
| M6 | Shots view | Switcher mode, Shots view, starter shots, New shot flow, hotkeys (F6) |
| S2 | Sunday test 2 | Used live for 2 services |

**Release 3 · Pro and public beta**

| # | Milestone | Outcome |
|---|---|---|
| M7 | Mixer and customisation | Mixer view, customisation options, wide bottom-dock layout |
| M8 | Beta | 3–5 other churches test it, then a free public beta on the OBS forums |

## 11. Success metrics

- **Personal:** a full service run with zero manual transform edits.
- **Beta:** 100 installer downloads in the first month (counted from GitHub release download numbers), and at least 10 users who report using it live.
- **Quality:** zero crashes or stuck states during live use across the Sunday test.

## 12. Future (post-MVP)

- Moving-window tracking for animated frames.
- Frames with more than one window (two cameras in one pushback).
- Content-aware reframing, so PIP boxes frame the person, not a random crop.
- Stream Deck / Companion actions beyond hotkeys.
- Code-signed installers.
- vMix support using the same core logic.
