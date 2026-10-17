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

## Searching online

Type a song's name (adding the singer helps: "Excess Love Mercy Chinwo"). If it isn't in your songs, **Enter** (or
**Search online for "…"**) asks LRCLIB (https://lrclib.net), a free community lyrics site with millions of songs. When
LRCLIB has fewer than five matches, lyrics.ovh (another free site) is asked too; its songs get their words when you pick
one. Tap a
result and it is added to your songs and opened, ready to edit and show. After that it is in your library and works
without the internet.

- Laycue only asks LRCLIB when you search; nothing else is sent. It needs a secure (https) connection; OBS on Windows
  uses Windows' own web client for that.
- The words there are typed in by the community. Check them (**Edit**) before the service.
- The site has no licence for the words. The owner chose this source knowing that; churches should project songs
  under their own licence (CCLI in most countries), as the panel says under the results.

Code: `core/online.*` (the address, turning an answer into a song, choosing useful results; tested) and
`store/online-search.*` (the request, through the pack's web client `plugins/common/src/net`).

## Templates and how they look

Lyrics has its own four templates: **Black**, **White** (a solid band), **Faded** (see-through) and **No background**
(the words alone, outlined so they read over video). **Edit this look** changes any of them, as in Scripture:
where on screen (lower third, card, full screen), what is behind the words (none, colour, faded, gradient, picture),
the colours and the text size. Two things only songs have:

- **Letters**: as written, **ALL CAPS**, or **First letter** (a capital at the start of every line).
- **Use the Scripture background**: copies the background of the Scripture template in use, so songs and verses
  match.

Code: `plugins/lyrics/src/core/song.*` (parts, slides, search, OpenLyrics, tested in `tests/test-lyrics.cpp`),
`store/` (library), `ui/` (the panel).
