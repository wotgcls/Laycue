- **Fixed from 0.2.5:** OBS closed about a second into launching — the Scripture panel touched one of its parts
  before it was built. It never reaches that point now.
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
