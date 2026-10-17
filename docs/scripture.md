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

Scripture has its own three templates: **Bar**, **Card** and **Full screen** (Lyrics has its own, different set).
Each can be changed under **Edit this look**, and the changes are kept per template:

- **Where on screen**: lower third, card, or full screen.
- **Behind the words**: none, a colour, a faded (see-through) colour, a gradient (two colours), or your own picture.
- The colours of the words and of the reference, and the text size.
- **Reset this template** goes back to how it came.

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
