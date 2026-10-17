# Laycue Scripture

A small panel that puts Bible verses on screen the way EasyWorship does: you type, it fills in, you press Enter.

## Using it

1. Open it: **Laycue → Scripture** (or the names at the top of any Laycue panel).
2. Type the start of a book: `jn`, `gen`, `1 cor`, `ps`. The hint under the box says which book it will be.
3. Press **Space**. The book's name is filled in (`John `). Type the chapter, press **Space** again and a colon
   appears (`John 3:`). Type the verse and press **Enter**. The verse goes on screen with its reference,
   for example "John 3:16 (KJV)".
4. **◀** and **▶** (or the ↑ and ↓ keys while typing) go to the previous and next verse, on into the next chapter.
   The list shows the verses around the one on screen; tap any of them.
5. **Show on screen / Hide** fades the verse in and out. The first time you show a verse, a "Laycue Scripture"
   layer is added to the live scene. After that it stays where you put it.
6. **Save this** keeps the reference under **Saved scriptures**, for verses you read every week. Tap one to show it.

Hotkeys (OBS Settings → Hotkeys): show or hide, next verse, previous verse.

## How it looks

**Bar**, **Card** and **Clean** are lower thirds. **Full screen** fills the screen; **Background picture…** puts
your own picture behind the words (it is darkened a little so the words always read).

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
