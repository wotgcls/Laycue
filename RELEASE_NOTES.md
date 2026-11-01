- **Fixed from 0.2.5:** OBS closed about a second into launching — the Scripture panel touched one of its parts
  before it was built. It never reaches that point now.
- Lower thirds: the gradient now **runs to the bottom edge of the frame** instead of stopping a little short
  and fading into black, so the band reads as one balanced piece from the accent line down.
- Scripture search: each result is a taller row that shows a **chunk of the verse itself**, not just the
  reference — the full text still rides along as the tool tip.
- Scripture's chapter list now carries **every verse of the chapter**, and the panel scrolls: to the first or
  the last verse as easily as to the middle; it always settles on the verse on screen.
- The golden line eases **quietly away at both ends** instead of stopping dead, and the sweep's shine is wider
  and softer — a gentle gleam rather than a hard flash. Both Scriptures and Lyrics, every template with a line.
- **Lyrics search that answers:** typing several words now finds the song even when the words don't stand
  together, and searching by the singer's name works; a listing without its words goes to fetch them from the
  bigger song book first, so picking a result no longer ends in "no words".
- **Your songs stay in sight:** with nothing typed the panel lists every song you've saved — the one on screen
  marked — instead of letting the list slip away when a song is open.
- **Click a Laycue source in the Sources list** and its panel comes to the front, its box ready for the next
  words — the panel is where every Laycue source is driven from, now one step away.
- Straight bands and the sunrise card **end on a smooth fade** now, so no border line cuts across the picture.
- The accent line's ends **follow a slow curve through many shades**, so there is no corner to point at —
  the fade and the middle brightening both read as one smooth sweep now.
- **Themes from pictures:** choosing a background picture reads its colours and lays them onto the slide —
  the gradient, the moving glow's third colour, the accent and the words all follow the picture, adapted to
  broadcast depth (dark enough for readable words, rich enough to be really there). "Match this picture"
  re-reads the colours any time, on the picture fill or the moving glow. A picture with no colour worth
  reading keeps Laycue's own deep blue and gold.
- **The big one: Laycue's own transitions are in, and you can add them right from the panel.**

- Go Live can use nine native Laycue transitions (Push, Brand Wipe, Zoom Through, Split Doors, Logo Sting, Card
  Stack, Move, Morph, Stripes). In the Laycue panel, under **Transition: … ▾**, pick **+ Add …**. The movement is
  the transition — brand colours are an opt-in layer (off by default, on for Logo Sting's panel, overridably), and
  an optional logo can ride along. They work with Studio Mode, hotkeys and the T-bar like OBS's own.
- Scripture and Lyrics: the thin line the templates draw can now be a **gradient**, take a quiet **light sweep**
  that travels along it, or be **taken away** entirely — under "Edit this look" in both panels.
- Scripture: the type box is now a choice — **Reference** or **Search**. In Search mode the words are found in
  every verse of the loaded version **as you type** (**Next** and **Previous** walk the results).
- Lyrics: songs can be found by **any lyric line**, not just the title or first line.
- Scenes: after Go Live, the shot that **was** live loads into Up next immediately, so the preview is never empty
  (Laycue → Settings → Working with OBS; can be switched off).
- Scenes: **failover** (opt-in, off by default). Laycue → Settings: watch a source — usually a camera — and if it
  goes blank or freezes while it is live, a shot of your choosing goes on the air at once, so the stream never
  shows an empty state. Nothing ever switches back by itself.
- **Check when OBS starts** (Laycue → Settings → Updates): the panel quietly looks for a newer release, so testers
  find updates without opening Settings first.
- Panels: one shared look with pinned headers, resize handles, and the transition picker in Shots. Titles grew to
  75 templates with nine new fonts and animated lower thirds.
- Updates download with their SHA-256 attached and, on Windows, install themselves when OBS quits, so nothing
  ever interrupts a stream.

Scenes and Titles are tested in real OBS; Network still needs its first test against a real streaming platform.
Problems: open an Issue, with the OBS log (**Help → Log Files**) attached.
