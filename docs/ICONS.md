# Social icons in Laycue Titles

The YouTube, Instagram, TikTok, Facebook and Mixlr templates draw each platform's own icon.

- **Source:** [Simple Icons](https://simpleicons.org) v16.34.0, released under CC0 (public domain). The licence is in
  `plugins/titles/data/brand-icons/LICENSE-simple-icons-CC0.md`.
- **How they are used:** `plugins/titles/tools/svg2cpp.py` converts the SVG files in `plugins/titles/data/brand-icons/`
  into vector path data (`src/render/brand-icons-data.inc`), which Qt draws directly. No image files, no extra Qt
  module and no SVG parser run inside OBS. To update an icon, replace its SVG and run the script.
- **Whose they are:** the names and logos belong to YouTube, Instagram, TikTok and Meta. The templates show them only so
  an operator can point viewers to their own page. They are not a statement that Laycue is affiliated with those companies.
- **Mixlr:** no open icon set includes Mixlr's logo, so it is redrawn from the logo file the operator supplied
  (`data/brand-icons/mixlr-logo-source.png`): a red square with a ring of 19 dots, some half see-through. The dot
  positions were measured from that file and live in `src/render/brand-icons.cpp`.
- **Colours:** drawn as each platform presents itself: YouTube red, Instagram's gradient, TikTok black with its cyan and
  red offset, Facebook blue. The operator's brand colour setting does not change these tiles.
