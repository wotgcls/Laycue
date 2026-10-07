# Laycue loop (portfolio animation)

- `prototype.html`: the whole animation in one self-contained file (fonts embedded). Open it in a browser to watch it loop.
- `laycue-loop.mp4` (H.264, 1280x720, 30 fps, 15 s, no audio, under 4 MB), `laycue-loop.webm` (VP9) and `laycue-loop.jpg` (poster, first frame).
- To change timings, edit the `--t-*` values at the top of `template.html`, then run `python3 build.py` (rebuilds `prototype.html`).
- To re-export: `node export.js <empty folder> 30` writes every frame as a PNG, then encode with ffmpeg (see the commands in the project notes). The animation is one function, `seek(t)`, so every frame is exact and the loop has no jump.
