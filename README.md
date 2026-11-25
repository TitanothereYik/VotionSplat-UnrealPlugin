# VotionSplat-UnrealPlugin 2.0

<p align="center">
  <img src="Docs/branding/votion-banner-seed-2.png" alt="VotionSplat" width="720">
</p>

Unreal Engine 5.5 plugin for the [VotionSplat](https://github.com/TitanothereYik/VotionSplat) app. It imports the `splat.ply` files that app exports and draws them with the same pixel rule the file was trained with. Windows 64-bit only. Version 2.0.0.

<p align="center">
  <a href="https://github.com/TitanothereYik/VotionSplat"><img src="Docs/branding/votion-logo-091-rounded.png" alt="VotionSplat" width="96"></a>
</p>

This repository is the Unreal plugin. [VotionSplat](https://github.com/TitanothereYik/VotionSplat) is the app that trains and exports the splats. Export with the app's Unreal / VotionSplat preset, then drag that `splat.ply` into Unreal. A PLY from another tool has no Votion tags, and this plugin rejects it.

There is no import dialog and no per-actor material. The PLY header names the rasterizer, the near plane, and the spherical-harmonic degree, and the renderer follows those.

## Install

The editor has to be closed while the plugin folder is copied. Unreal keeps the plugin DLL locked for the whole session.

### Prebuilt plugin

Use this when you already have a packaged `VotionSplat-UnrealPlugin` folder (it contains `VotionSplat-UnrealPlugin.uplugin` and `Binaries`).

1. Copy that folder to `<YourProject>/Plugins/VotionSplat-UnrealPlugin`.
2. Open the project. If Unreal asks to enable **VotionSplat-UnrealPlugin**, confirm.
3. Drag a Votion `splat.ply` into the Content Browser, then drag the new asset into the level.

The prebuilt binaries are compiled for Unreal Editor 5.5 on Win64. A different engine version will refuse to load them.

### From this repository

This repository is source. `Binaries/` is not in git, so the editor compiles the plugin on first open. That needs Visual Studio 2022 with the C++ desktop workload, and the same MSVC toolchain Unreal 5.5 uses.

1. Clone this repository to `<YourProject>/Plugins/VotionSplat-UnrealPlugin`. The folder name has to match the `.uplugin` file.
2. Open the `.uproject`. Enable **VotionSplat-UnrealPlugin** when asked, and let the editor compile it.
3. Drag a Votion `splat.ply` into the Content Browser, then drag the asset into the level.

To compile without opening the editor:

```bat
"<UE_5.5>\Engine\Build\BatchFiles\Build.bat" YourProjectEditor Win64 Development -Project="<YourProject>\YourProject.uproject" -WaitMutex
```

## Use

Drop the asset in the level and leave the defaults. The actor transform is honored. Training units are metres, Unreal units are centimetres, so a scale of 1 places the scan at 100 Unreal units per metre. Scale the actor to resize the scan.

The file has to be a binary little-endian PLY exported by [VotionSplat](https://github.com/TitanothereYik/VotionSplat). The header carries:

- `comment votion.version=1`
- `comment votion.raster=` one of `gsplat_antialiased`, `gsplat_classic`, `litegs_classic`
- `comment votion.near_plane=` the training near plane, in the PLY's own units
- `comment votion.sh_degree=` 0, 1, 2, or 3

It also needs the usual 3DGS vertex properties: `x y z`, `f_dc_0..2`, `opacity`, `scale_0..2`, `rot_0..3`, and `f_rest_*` when the degree is above 0. A plain PLY with no Votion tags is rejected. The importer accepts up to 16 million splats.

Meshes in the level occlude splats. Splats composite over the tonemapped image, so an empty scene and a scene with a floor look the same where the floor does not cover them.

## What is in the renderer

The draw runs inside a post-process view extension, after Tonemap, through Unreal's render graph.

**Import.** An editor factory reads the PLY on the game thread, packs each splat into a 64-byte record (mean, opacity, world covariance, DC colour), and stores higher spherical-harmonic bands in a second buffer. Both upload once as shader resources.

**Projection.** One compute pass per view. It builds the pinhole intrinsics from the Unreal projection matrix, transforms the 3D covariance into view space, and projects it with the Jacobian of the perspective divide (the standard EWA splat). It adds `0.3` to the screen-space variance, which is gsplat's `eps2d`. The antialiased profile then scales opacity by `sqrt(det_before / det_after)`. Gsplat profiles clamp the on-screen extent to 3.33 standard deviations and reject splats whose box misses the view. LiteGS keeps the wider extent, uses the 1/256 alpha floor, and drops splats whose centre sits outside NDC 1.3. Colour is spherical harmonics through degree 3, evaluated in the gsplat view direction, then offset by 0.5.

**Sort.** Depth keys are 32-bit and point along the view axis. Unreal's built-in GPU radix sort orders every splat back to front. A three-pass exclusive scan then compacts that order down to the splats the projection kept, and writes indirect draw arguments. The compacted record is 64 bytes: screen mean, opacity, clip depth, conic, colour, and the two ellipse axes.

**Tiles.** This is the 2.0 path, on by default (`r.VotionSplat.TileRaster 1`). Each visible splat is tested against the 16×16 tiles its ellipse touches. Those pairs are emitted in the compacted back-to-front order and sorted with a stable 4-bit radix sort on the tile id, so each tile's list stays in that order. A compute shader owns one tile per group, walks the list front to back, and blends in registers:

`alpha = min(alpha_max, opacity * exp(-sigma))`, discarded below the profile's alpha floor, and discarded when the scene depth is closer than the splat. Transmittance starts at 1. The pixel stops when the next splat would take it to `1e-4` or below, which is the cutoff gsplat uses. The pixel is written once, as premultiplied colour plus `1 - transmittance`, into an RGBA32F target.

**Composite.** A fullscreen pass reads that target. Display views blend in sRGB and encode for an sRGB render target. Display-linear views convert the splat colour with the sRGB curve. Scene-linear captures (final-color HDR) go through a baked inverse view LUT and then from Rec.709 into the engine working colour space. An HDR display output is drawn as SDR and logs a warning once.

**Cache.** While the camera, the actor transform, and the viewport size are unchanged, the projection, the sort, the compacted records, and the tile lists stay in GPU memory and the frame only runs the tile blend. `r.VotionSplat.TileRaster 0` draws the same records as instanced triangle-strip quads with premultiplied hardware blending, which is the path version 1 used.

## How this compares with a typical Unreal splat plugin

Most splat plugins in Unreal still do three things that dominate both the frame time and the difference against the training viewer.

They draw one quad per splat and let the hardware blend every pixel that quad covers, including splats hidden behind an opaque surface of nearer splats. They rebuild the whole sort every frame. And they shade a single approximate Gaussian, then composite it in whatever colour space the post-process buffer happens to be in.

VotionSplat-UnrealPlugin takes the other side of each of those.

The tile pass stops a pixel at the same transmittance the training rasterizer stops at, and it never shades the empty part of a quad. On an RTX 3090, at 1847×1237, with a still camera and 32-bit accumulation, the same scenes measured like this:

| Scene | Splats | Quad path | Tile path |
| --- | --- | --- | --- |
| alpine_01, LiteGS | 2.98M | 10.8–18 fps, raster 57–94 ms | 80–120 fps, raster 1.3–9.5 ms |
| alpine_02, gsplat | 2.45M | 31–54 fps, raster 17–31 ms | 120 fps, raster 1.5–2.1 ms |

120 fps is the editor's frame cap, so several of the tile numbers are the cap. With the camera moving, alpine_01 holds 40–51 fps and alpine_02 holds 90–111 fps, because the projection, sort, and tile lists rebuild every frame. The tile blend itself stays in the same 1–10 ms range.

The image is checked against gsplat reference renders of the same cameras, for all three profiles, including a stacked-splat termination case and two real scans. On the real scans the tile path is closer to those references than the quad path was, because each pixel accumulates in 32-bit registers and stops on the gsplat rule. 16-bit framebuffers, which many plugins use to recover speed, shift the dense alpine_01 views enough to fail that check, so 32-bit stays the default.

A still camera pays the sort once. After that, a frame is the tile blend plus a fullscreen composite.

## Limits

- Windows 64-bit and Unreal Engine 5.5 only.
- The first splat actor in a view uses tiles. Further splat actors in the same view blend quads over that result.
- A view that needs more than 64 million tile entries draws with quads for that frame while the buffer grows. alpine_01 at 1847×1237 needs about 19–24 million.
- Moving the camera pays the tile-list build: about 7–10 ms on alpine_01, 2.5–3.6 ms on alpine_02.
- HDR display output is drawn as SDR.
- A high-resolution screenshot whose scene depth still belongs to the editor view disables the depth test for that shot, so the capture is not rejected by a mismatched depth buffer.

## Console variables

Set these in the editor console or in `DefaultEngine.ini`.

| Variable | Default | |
| --- | --- | --- |
| `r.VotionSplat.Enable` | 1 | Draw splats. |
| `r.VotionSplat.TileRaster` | 1 | 1 is the tile blend. 0 is instanced quads. |
| `r.VotionSplat.TileStopT` | 0.0001 | Stop a pixel when transmittance would fall to this. 0 blends every splat in the tile. |
| `r.VotionSplat.DepthTest` | 1 | Occlude splats with scene depth. |
| `r.VotionSplat.AccumFormat` | 1 | 1 is RGBA32F. 0 is RGBA16F, which is faster and can shift dense scenes. |
| `r.VotionSplat.CompositeMode` | -1 | -1 follows the view. 0 display, 1 display-linear, 2 scene-linear. |
| `r.VotionSplat.ViewCache` | 1 | Reuse the projection while the view is unchanged. |
| `r.VotionSplat.SortReuse` | 1 | Reuse the sort while the view direction is unchanged. |

## License

MIT. See [LICENSE](LICENSE).
