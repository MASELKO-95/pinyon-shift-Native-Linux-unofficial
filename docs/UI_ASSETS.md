# FH1 UI assets

This page describes the formats of the title's UI archives as far as the host
UI (NP-1) and later texture and font work (NP-5, NP-10, NP-11) need them. It
contains no game data: every number below is a format fact or a count from the
supported revision, and the catalogue tool writes derived files only below a
`.local` directory.

## Catalogue tool

`tools/inspect-fh1-ui.py` lists the four UI archives (`media/UI.zip`,
`media/ui/Fonts.zip`, `media/ui/Textures.zip`,
`media/ui/textures/Horizon.zip`) and, with `--decode-assets`, extracts and
decodes every texture and vector font:

```powershell
python tools/inspect-fh1-ui.py --game-root .local/game/base `
  --output .local/ui-catalogue/ui-assets.json `
  --archive-extractor out/build/win-amd64-release/pinyon_shift_fh1_archive_extract.exe `
  --decode-assets --png-dir .local/ui-catalogue/png
```

`--png-pattern <glob>` limits the PNG export to matching members. Decoding is
pure Python and runs in one process per logical processor; the full export of
4,373 textures takes about seven minutes. The JSON records every member with
its sizes and hashes, each texture's header, each font's metrics and glyph
table, the parsed `fontmap.xml`, the loose CJK font files and a texture
summary by format and round-trip result.

## Archives

All four are ordinary ZIP files whose members use method 21 (Xbox XMem LZX),
which `pinyon_shift_fh1_archive_extract` decompresses; a few members are
stored. `UI.zip` holds the UI4 scenes (`Scenes/ui4/<name>.{bgf,bsg,fbf}`),
Lua, XML and three cube-map `.xpr` files. `Fonts.zip` holds the vector fonts.
`Textures.zip` (3,312 members) and `Horizon.zip` (1,061) hold only `.xds`
textures: map, thumbnail, icon, HUD, event-ticket, livery and loading-screen
art.

## Textures (`.xds`)

An `.xds` file is a 52-byte Direct3D texture header followed by the texture
data exactly as the GPU reads it:

| Offset | Content |
| --- | --- |
| `0x00` | `Common` = 3 (texture), reference count, fences, identifier, base and mip flush words |
| `0x1C` | the six-dword Xenos texture fetch constant (`xe_gpu_texture_fetch_t`), big-endian |
| `0x34` | base level, then mips at `mip_address` pages when there are any |

The fetch constant gives the format, size, pitch, tiling, guest endianness,
the component swizzle, gamma, the mip count and packed-mip flag. Every UI
texture is 2D and tiled. The decoder untiles with the SDK's
`GetTiledOffset2D`, places bases no larger than 16 texels at their packed
mip-tail offset, undoes the endianness, decodes the block format and applies
the swizzle, so the PNG shows what the title samples.

| Format | Count | Notes |
| --- | ---: | --- |
| `k_8_8_8_8_AS_16_16_16_16` | 2,671 | RGBA8, 8-in-32 endian, `ZYXW` swizzle, gamma |
| `k_DXT4_5_AS_16_16_16_16` | 1,251 | BC3 |
| `k_DXT1_AS_16_16_16_16` | 237 | BC1 |
| `k_8_8_8_8` | 154 | RGBA8, `ZYXW`, linear |
| `k_DXT5A` | 47 | BC4, one channel read in every component |
| `k_DXT2_3_AS_16_16_16_16` | 7 | BC2 |
| `k_DXT3A` | 6 | 4-bit explicit alpha, one channel read in every component; map masks with a `111W` swizzle |

Round trips: each written PNG is read back and compared with a fresh decode.
For the 32-bit formats the PNG is also re-encoded into guest storage
(`reencode_xds`: swizzle inverted, endianness reapplied, retiled) and must
reproduce the stored bytes exactly, which proves the address mapping in both
directions and is the base of texture replacement (NP-10.3). Block-compressed
textures are compared by pixels; re-encoding them needs a BC encoder.

## Fonts

`fontmap.xml` maps the names that UI4 scenes use (`Horizon_A`, the old
`A_Swiss_721_Bold_Condensed`, `DG2_lcd` and so on) to six targets: `A`, `B`,
`C`, `D`, `E` and `SYM`, plus five digital-gauge fonts `DG1` to `DG5`. It also
lists per-language fallback adjusters (`fallback_adjuster` and the
standard-definition `fallback_adjuster_sd`) with per-scene minimum sizes for
Chinese, Korean and Japanese.

Each target is a FontCompiler vector font, `<target>_vector_aa.dt`, with a
`<target>ru_vector_aa.dt` variant that adds Cyrillic. They are CAFF containers
(`CAFF21.11.05.0034`) with a `vfont` asset (`12.07.06.0035`):

- `.data` at file offset `0x190`: an open-addressed hash table of
  `(u16 code point, u16 glyph)` pairs, a metrics block (design size 14,
  ascent and descent in font units, a scale) and one 40-byte record per glyph:
  code point, advance in ems, the glyph's vertex range and triangle range, and
  a horizontal offset.
- `.gpu`: the glyph meshes. Vertices are four big-endian half floats
  `(x, y, u, v)`; a `u16` triangle list follows, with indices relative to the
  glyph's first vertex.

The Latin fonts cover U+0021–U+007E and Latin-1 and Latin Extended letters
(237 glyphs; 303 in the Cyrillic variants). The digital-gauge fonts carry
only digits and a few letters. Chinese, Japanese and Korean use separate
bitmap fonts loose in `media/ui/fonts` (`.sbm` glyph sheets, `.abc` metrics,
`.tex` textures and character-list `.ini` files), which the catalogue lists
but does not decode.

The catalogue records every glyph's advance and mesh size.

## How the title draws a vector font

One shader pair draws every vector-font glyph in the recorded frames (vertex
shader `9C19E3CACBE2E342`, pixel shader `D7524D5CA740AAE5` in `default.xex`),
one draw per glyph, straight to the screen; there is no glyph cache texture.
Variants for textured, outlined and bevelled text exist in the executable but
were not used in those frames.

- The vertex shader places a vertex at `(|x| + offset, y)` ems, where
  `offset` is the glyph record's horizontal offset. The sign of `x` is a flag,
  not a position: positive triangles are inside the outline, negative ones
  cover the outer half of the anti-aliasing edge. Folding by `|x|` is why a
  raw plot shows each glyph next to a mirrored "frame".
- `(u, v)` is a curve coordinate. Curve triangles use `(-1, 1)`, `(0, -1)`
  and `(1, 1)`; straight edges use `u = 0` with `v = 0` on the outline and
  `v = 1` at interior points.
- With `s` the interpolated sign, `f = u² - v` and `d = f·s / |∇(f·s)|` in
  pixels, coverage is `saturate(0.5 - d)`. Where `u` and `v` are constant
  the gradient is zero and only the sign of `f·s` counts (fully in or out).
- Glyphs blend with ordinary alpha, no culling, depth or stencil; the pen
  advances by the glyph's advance with no kerning.

`src/ui/hostui/vector_font.cpp` reproduces this on the CPU. Rendered at the
title's sizes it matches recorded frames to within the background
contribution at the edges. The pause menu's items use the heavy italic `E`;
`B` is the condensed slab used for the help bar, driver names and timers; `D`
is a heavier `B`; `A` is the sans body text; `C` is a digits-and-capitals
subset of `A`; `SYM` holds icons.

## Font decision for the host UI

The backlog asked whether the host UI should draw the game's bitmap fonts or a
metrically matched TTF. The Latin fonts are meshes rather than bitmaps, and
no installed Windows font matches their advance widths: fitting each of `A`
to `E` against every font in `C:\Windows\Fonts` with a free scale leaves at
least 5 % mean advance error. With the coverage rule reproduced, the host UI
uses the game's own fonts instead: at first use it reads `Fonts.zip` from the
player's disc and rasterizes the glyphs it needs into its atlas at the
current output size, so nothing derived is distributed and text stays sharp
at every internal scale. The Cyrillic `*ru_vector_aa.dt` variants are loaded
where present. Chinese, Japanese and Korean text needs the bitmap fonts and
waits for NP-5.
