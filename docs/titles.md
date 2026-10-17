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

## The 68 templates
Seven categories: **People** (name bars), **Worship** (series titles, song lyrics, quotes), **Announce**, **News** (tabbed
headline bars), **Live** (an on-air badge and bar with a pulsing light), **Social** (YouTube, Instagram, TikTok, Facebook,
Mixlr and a generic handle, each with the platform's icon, see `ICONS.md`; plus **Subscribe + Bell**, below) and **Tickers**. Scripture templates were
removed: the pack is for names and status, not verses.
They are data (`TitleTemplate`), not code per template, which keeps custom templates simple.

## Subscribe + Bell
A channel card (avatar, name, a line under it) with four buttons: Subscribe, a bell, Like and Share. While it is on air a
cursor clicks them one after another, every 10 seconds: Subscribe turns grey and reads Subscribed, the bell fills, swings and
says "Notifications on" with a red dot, the thumb pops with a +1, the share arrow nudges forward. Everything goes back to
how it started before the next round. Fields: channel name, message under it, button word (default "Subscribe"). It
draws everything itself (no image files) and makes no sound.

## Guest Minister and Giving
Two cards in the style of broadcast graphics with a logo, tabs above and below the bar and several columns.
- **Guest Minister** (People): an orange tab ("Now ministering"), the name on a blue bar with an optional logo, and a white tab
  under it for the church.
- **Giving** (Announce): a tab with a heart, the account holder and bank, then **one column per account** the operator adds
  (up to four; **Add account** in the panel), and a note tab under the bar for a sort code. The numbers shrink in steps until
  the whole bar fits inside the title-safe width, so two accounts and four accounts both look finished.
- **Logo:** the operator chooses a picture (PNG, JPG, WebP). It is read once and kept; its path is saved with the title.
- **Colours:** every template can be recoloured under **Customize** (main shape, accent shape, main text, small text), and
  templates with more shapes add a second shape, a third shape (logo block) and the text on the shapes. A template can name
  its own colours ("Tab above the bar", "Note tab") in the locale file.

## Cueing: the animation plays when the scene goes live
A title is a source in a scene. When the operator presses **Show on air** while that scene is not on the program
output yet (it is only in Preview in Studio Mode), the title is **cued**: it sits fully drawn in Preview so it can be
checked and positioned, and its arrival animation plays when the scene goes live (Cut or Transition). Before this,
the animation ran in Preview and was over by the time of the cut. If the scene is already live, or the title layer
is in every scene, Show plays the animation immediately.
The source polls `obs_source_active()` once per frame: going live starts the arrival, leaving the program output
ends it quietly, and a title that has finished leaving does not come back by itself. Hide cancels a cue.

## Time on screen
The panel has a **Stays on screen** control (template default, until hidden by hand, or 3 seconds to 2 minutes). The
time counts from the end of the arrival, so a cued title does not use it up while waiting in Preview.

## Position and size
Every title that does not span the picture (all but the tickers and Thank You) can sit on the left, in the centre or on
the right, and be made 50% to 150% of its normal size. The choice is remembered per template, so a social card can sit
on the right while the name bars stay on the left. The renderer measures the title once (cached) and places it by its real
edges, so the margin is the same whatever the words are. Changing it while a title is on air shows "Update on air".

## Transition speed and shape width

- **Transition speed** (Setup tab, or in the source's Properties): five steps from Slower to Faster that scale every
  template's arrival and exit times (about 1.6x slower to 0.55x faster). It is a small range on purpose.
- **Shape width** (Options row, name templates only: Clean Bar, Accent Edge, Split Name, Box Pop, Corner Tag, Stacked,
  Guest Minister, Modern Pill, Modern Card): *Template* keeps the design's own width, *Fit to text* makes the shape hug the
  name with a little extra padding, *Set width* fixes the width and the text shrinks to fit inside it. Saved per template.

## Logo animations

Eleven templates in the Live category animate a logo picture on a repeat: Silver Sweep (silver edges and a bright core, so it
shows on white logos too), Spin, Glow Pulse, Float, Flip, Ripple, Hop, Wipe Reveal, Orbit, Water Shimmer, Glass Cube and Glass Disc (the logo
ripples slightly like water), **Glass Cube** and **Glass Disc** (the logo floats inside a transparent cube or round glass coin that turns once every few seconds, then rests facing you). **Logo + Wordmark Slide** works with a logo that has two parts: choose the picture, press
*Set logo parts...*, drag the line to where the mark ends and the wordmark begins (side by side or stacked, mark first or
second). The wordmark then slides away behind the mark and slides back out. Until the parts are set, the logo stays still.

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

## Layers and the run order

**Layers.** Each title layer is its own OBS source, so a crawl and a lower third can be on and off air separately. The **Layer** row at the top of the panel chooses the layer the buttons act on. **New layer** makes another one. Each layer remembers its own template and text. The **Hide all** hotkey hides every layer.

**Run order (the Queue tab).** Add every title you plan to show with **+ Queue**, in order. Titles stay in the list after they play, so you can go back or play one again.
- **Play next** plays the title after the one played last. **Previous** goes back one. Both have OBS hotkeys (Settings > Hotkeys, "Laycue Titles").
- The row on air shows **ON AIR**, the one after it shows **NEXT**, and rows already played are dimmed.
- Each row remembers the layer it was queued on, and plays there.
- The **⋯** menu on a row: Edit (then press **Save row**), Move up, Move down, Remove.
- The **Run order** row at the top: **Save as…** keeps the list under a name such as "Sunday 9am". **Load** brings a saved one back. Also: Delete, Back to the top, Clear the list.
- Up to 50 titles in a list and 30 saved run orders.

## Tickers, layer names and saving a service

- **Tickers:** press Enter while typing the ticker text to start a new item. Items run one after another with a small accent diamond between them.
- **Layer names:** when you press Show on air, a layer that still has its automatic name is renamed after the content, for example `ttl. Pastor David Ok…`. Layers you named yourself are never renamed.
- **Save this service…** sits on the Queue tab. It saves the run order under a name you choose, and you can load it again later.
- **New templates:** Coming Up, Event Date, Prayer Request, Welcome, Celebrate, Question and Hashtag Bar.

## Points bank (Notes templates)

Pick a Notes template (Sermon Notes, Numbered Points, Point Columns) and the panel shows a **points bank** instead of a stack of rows.

- Type a point and press **Enter** (or **Keep**). Keep as many as you like, up to 200. They are saved with the scene collection.
- Tap a point to put **only that point** on screen, in any order. The sermon title and heading stay as they are in the boxes. Tapping another point makes the one on screen leave first, so points never pile up.
- The row on screen says **On screen**. The **✕** removes a point from the bank.
- Prefer the old behaviour (several points on one card)? Switch **One at a time** to **Stack them**.
