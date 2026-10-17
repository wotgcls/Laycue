# Laycue Lyrics

A small panel that puts song words on screen, two lines at a time.

## Using it

1. Open it: **Laycue → Lyrics**.
2. Type part of the song's name ("amaz", "well"). Matching songs appear; press **Enter** for the first or tap one.
   A song can also be found by its first line.
3. The song opens as a list of two-line slides, grouped by part (Verse 1, Chorus...). **Show on screen** puts the
   first slide up; **▶** / **◀** (or ↓ / ↑) move through it; tap any slide to jump.
4. **Hide** fades the words out. The first time you show a song, a "Laycue Lyrics" layer is added to the live scene.

Hotkeys (OBS Settings → Hotkeys): show or hide, next lines, previous lines.

## Songs

- **New song**: type or paste the words. A blank line starts a new part; a line such as `Verse 1`, `Chorus`,
  `Bridge` or `[Chorus]` names it.
- **Edit**: change the open song (or delete it).
- **Import…**: OpenLyrics `.xml` files (what OpenLP and other church software export) and plain `.txt` files. Several
  at once is fine.

15 public-domain hymns are included (Amazing Grace, Holy Holy Holy, It Is Well with My Soul, Blessed Assurance and
more). Your library is kept in `songs.json` in OBS's plugin config folder.

## Why it doesn't download lyrics

Modern worship songs are copyrighted. Churches project them under a licence (CCLI in most countries) and get the
words from the licensed source. A free "all the lyrics on the internet" source does not exist legally, so Laycue
does not download lyrics: you type, paste or import them once, and they stay in your library.

## How it looks

The same four looks as Scripture: **Bar**, **Card** (with the song name), **Clean** (words only, with an outline, for
over video) and **Full screen** with your own background picture.

Code: `plugins/lyrics/src/core/song.*` (parts, slides, search, OpenLyrics, tested in `tests/test-lyrics.cpp`),
`store/` (library), `ui/` (the panel).
