# Laycue Titles: how it works and why

## Why a native source, not a browser source
Browser-source lower thirds need a browser engine per title, a web server or a separate app, and break when
the network or a port is blocked. A native OBS source runs on OBS's own graphics thread, costs one texture,
works offline, and shows up in every scene like any other source. That is the main reason Titles exists as
a plugin rather than an HTML pack.

## Rendering
- The panel and the source share one renderer (`src/render`). QPainter draws the title into an image at a
  1920-wide design space and scales it; the source uploads it as a texture. One renderer means the preview
  in the panel is exactly what goes on air.
- Animation is a small state machine (`TitleClock`: hidden, in, hold, out) with easing that arrives fast and
  settles slowly (`easeOutExpo`) and leaves faster than it arrives (`easeInCubic`), the broadcast convention,
  so the title never feels slow to clear.
- Fonts are bundled (OFL licensed) so a title looks the same on every machine.

## The 28 templates
Seven categories: **People** (name bars), **Worship** (series titles, song lyrics, quotes), **Announce**, **News** (tabbed
headline bars), **Live** (an on-air badge and bar with a pulsing light), **Social** (YouTube, Instagram, TikTok, Facebook,
Mixlr and a generic handle, each with the platform's icon, see `ICONS.md`) and **Tickers**. Scripture templates were
removed: the pack is for names and status, not verses.
They are data (`TitleTemplate`), not code per template, which keeps custom templates simple.

## Position and size
Every title that does not span the picture (all but the tickers and Thank You) can sit on the left, in the centre or on
the right, and be made 50% to 150% of its normal size. The choice is remembered per template, so a social card can sit
on the right while the name bars stay on the left. The renderer measures the title once (cached) and places it by its real
edges, so the margin is the same whatever the words are. Changing it while a title is on air shows "Update on air".

## Custom templates
A custom template stores only the fields the operator changed (colours, size, position, hold time) over a base
template. Updating a base template therefore never breaks a custom one.

## Operator control (guiding principle)
Nothing appears on air by itself. Show and hide are explicit; "hide by itself" is an opt-in time per title.
"Add to every scene" asks for confirmation because it touches the operator's scenes.

## Known limits
- Cost per frame (measured, `titles-preview --bench`): 1 to 4 ms for every template, tickers about 1 ms,
  so a 30 fps stream has a 33 ms budget with room to spare. A static title is drawn once and costs nothing.
  Large text with a shadow used to take 73 ms (Sermon Series, Thank You) and would have stalled OBS during
  the arrival; the text and its shadow are now drawn once and re-used on each frame of the animation.
- Not yet verified: brand-colour override and hotkeys on real hardware.
