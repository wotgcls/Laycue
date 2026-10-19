# Laycue Scripture

A small panel that puts Bible verses on screen the way EasyWorship does: you type, it fills in, you press Enter.

## Using it

1. Open it: **Laycue → Scripture** (or the names at the top of any Laycue panel).
2. The reference box has three parts in one box: **Book | Chapter | Verse**.
3. Type the start of a book: `mat`. The rest fills in and is selected ("Mat**thew**"); keep typing to change it, or
   press **Space** to take it. Short forms work too (`jn` shows "→ John"). A number typed straight after the book
   starts the chapter.
4. Type the chapter, **Space**, the verse, **Enter**. The verse goes on screen with its reference,
   for example "Matthew 24:33 (KJV)".
5. Click any part (book, chapter or verse) and all of it is selected, so a new number can be typed straight over it.
6. **◀** and **▶** (or ↑ and ↓ in the box) go to the previous and next verse, on into the next chapter. The box
   always shows the verse on screen.
7. **Show on screen / Hide** fades the verse in and out. **Save this** keeps a reference under **Saved scriptures**.

Hotkeys (OBS Settings → Hotkeys): show or hide, next verse, previous verse.

## Templates and how they look

Scripture has **17 templates in four packs**, each pack suiting a theme. Choose a pack, then one of its templates:

- **Classic**: Bar, Card, Full screen and Clear (the words alone, outlined).
- **Modern**: Tab (the reference in a coloured tab above a dark band), Rule (words beside a thick line), Clean (a light
  band) and Pulpit (large quotation mark, centred words). Left-aligned.
- **Sanctuary**: Open Bible, Glow, Parchment and Royal. Warm serif type. Open Bible, Glow and Royal have a small
  **looping Bible** beside the words for as long as the verse is on screen: an open Bible whose page turns, or a
  closed Bible with a gold cross that floats in turning rays of light. It sits on the side opposite the words, and the
  words make room for it.
- **Living Word** (lower thirds that move): Sunrise (a sunrise tile, the reference on a cream block and the verse on
  a slate block), Torn (a torn purple band, the reference in large letters on the left), Dove (a white dove beating
  its wings), Cross (a gold cross in turning rays) and Aurora (soft colours rising from the bottom and drifting
  slowly; choose the three colours under **Edit this look > Behind the words > Moving**).

Lyrics has its own set of twelve in four packs. Each template can be changed under **Edit this look**, and
the changes are kept per template:

- **Where on screen**: bar, card, full screen, tab, rule, quote, split or torn.
- **Behind the words**: none, a colour, a faded (see-through) colour, a gradient (two colours), your own picture, or
  **Moving** (three colours that drift slowly past each other).
- The colours of the words and of the reference, and the text size.
- **Text alignment**: left, centre or right. The layout still decides where the box sits, so it stays tidy.
- **Font**: Modern (Outfit), Clean (Barlow), Compact (Barlow Condensed), Classic (Libre Baskerville) or Display
  (Bebas Neue). All are bundled, so a verse looks the same on every computer.
- **Between slides**: **Fade** (the old words fade out, the new fade in) or **Cut** (they swap at once).
- **Looping animation**: none, Open Bible, Glowing Bible, Scroll, Dove, Cross or Sunrise.
- **Left and Right arrow keys** step through the verses once one is on screen (click the panel first if a text box
  has the cursor).
- **Reset this template** goes back to how it came.

The pictures are drawn by Laycue itself (`slides/slide-render.cpp`): no video files, nothing to download, and they cost a
few milliseconds a frame, only while a verse with one is on screen.

## Versions

The **King James Version** is included (public domain). To add another, choose **Version → Add a version (XML
file)…**. Laycue reads Zefania XML, OpenSong XML and Beblia XML, the formats most free Bible files come in. The
file is checked, then copied into Laycue's own folder. Only add versions your church may use: most modern
translations are copyrighted.

## Where things are kept

`scripture.json` and the added versions are in OBS's plugin config folder (`plugin_config/laycue-scripture`),
so they are the same in every scene collection.

Code: `plugins/scripture/src/core` (books, references, the typing rules and the XML reader, all tested in
`tests/test-scripture.cpp`), `store/` (settings and versions), `ui/` (the panel). The on-screen part is shared
with Lyrics: `plugins/common/src/slides`.
