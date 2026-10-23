# 04 — Visuals & Gallery Pipeline

Every `examples/*.lms` (23 in 0.2) is self-executing: stage 1 (`cli="lmaudio"`) synthesizes its own
input, asserts a known answer, writes `audio/<name>.wav` (16-bit mono 22.05 kHz, <= 200000 bytes,
so <= ~4.4 s) and CSV/`.gp` data under `build/`; stage 2 (`cli="lmdraw"`, optional) renders a diagram
to `build/rendered/*.png` (lmdraw only writes there); stage 3 (`cli="lmplot"`) draws
`images/<name>.png` and places the diagram with `set pixmap`. Each stage runs with
`system ${dsl_cli(x)} --dsl build/...dslb`, prints host stdout lines (not the JSON `{...}` line) and
prints stderr before `assert(r.code == 0)`.

Numbers flow to gnuplot through `audio.save_text("build/x.gp", "a = 1.23; b = ...")` + `load`.
`examples/render-all.sh` runs all examples (lmplot/lmdraw from their releases, like autorun) and
copies images and WAVs to `docs/`. README gallery groups: synthesis, physical models, rhythm,
filters/dynamics/effects, rooms/reverb, analysis/delivery; each has image, audio link, autorun line.

Gotchas met while building the 0.2 gallery:
- gnuplot keys with `box opaque` and long titles drew boxes far wider than the text: use `nobox`.
- A gnuplot error inside `plot.cmd` (e.g. a duplicated `font` option) can leave a truncated PNG
  while the program still exits 0 (lmplot 0.1.1): always view the image.
- `pm3d map` labels need `front` to show over the heatmap.
- `lmctl autorun` installs the *released* lmaudio: examples using new API only autorun after a
  release that contains it.
