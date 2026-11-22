# iOS 26 GPU acceleration on the Galaxy S10: overview

Updated: 2026-11-22 (section 9, B1L-0352); sections 1–8 describe B1L-0268. The build-by-build tracking stays in `metal-progress.md`; this document gives the overview:
how the acceleration works, what works, what is missing compared with an iPhone, and in what order we move forward.

## 1. The chain, from the app to the Mali

```
App / SpringBoard / backboardd (Metal, CoreAnimation, RenderBox)
   │  Apple's Metal API (MTLDevice, command buffers, pipelines)
   ▼
libhakiosmetal.dylib  — Metal driver (userspace/metal/scion/hakiosmetal_air.m, based on Scion, MIT license)
   │  translates each Metal object (buffer, texture, pass, draw) into "rwa_*" calls
   ▼
libhakiosair.dylib    — modified Mesa 25.0.7 (userspace/mesa/air-b2l/apply.py)
   │  air_to_nir: Apple shaders (AIR, LLVM) → NIR
   │  air_metal.c: Metal state → Gallium state (pipe_context)
   │  panfrost + Bifrost compiler: NIR → Mali code, GPU jobs
   ▼
XNU kernel: HakiosMali (IOAcceleratorES, GPU memory, MMU, jobs)  →  Mali-G76 MP12 (~676 MHz)
```

- iOS loads our plug-in as a real driver: `MTLCreateSystemDefaultDevice()` returns "Mali-G76 (Panfrost) (Hakios)".
- Apple's shaders are not recompiled from their source: we translate the already compiled AIR (the system's `.metallib` files).
  `newLibraryWithSource` goes through Apple's compiler on the phone, then through the same translation.
- One `pipe_context` per process, protected by a global lock since B1L-0259.
- The final display goes through backboardd (CoreAnimation on Metal) then through the screen.

## 2. What works (Dictaphone Metal test, B1L-0267: 43 OK, 1 FAILURE, 1 SKIPPED)

| Area | Status |
|---|---|
| Device, command queue, command buffers, handlers, `MTLSharedEvent` | OK |
| Buffers: shared, private, 64 MB, no copy (`NoCopy`), blit, fill | OK |
| Compute: 1 M additions, threadgroup + atomics, texture write | OK |
| Textures: 8 formats, mipmaps, buffer → texture blit, nearest/linear sampling | OK |
| Rendering: clear, vertex descriptor, indexed + instances, alpha blending, viewport/scissor, depth, MSAA 4x + resolve | OK |
| Drawing into an IOSurface and into the app's memory (RenderBox icon path) | OK |
| Derivatives `dfdx`/`dfdy`/`fwidth`, including inside an `if` | OK (B1L-0264) |
| Compilation from source | OK, but 5.2 s |
| Multiple targets (R8, RG16F, 4 targets), target read (framebuffer fetch), pixel center | OK (B1L-0265/0266) |
| `CAMetalLayer` on screen | OK, only **9.5 to 10.9 frames/s** |

## 3. What does not work yet

- **Icons that break as soon as there is movement** (and sometimes come back correct). The half-pixel (fixed in 0266) and the multiple
  targets pass the tests, but not the home screen in motion. Probable causes, to be checked one by one:
  1. iOS 26 redraws the icons at every frame during a movement (glass reflection, "live" RenderBox in SpringBoard),
     whereas at rest it reuses the image from the cache. This per-frame path is the one that breaks.
  2. The texture "live copies" (`hakiosRefreshLiveCopy`, `hakiosWriteBack`): copies by the processor between the app's
     memory and the GPU texture, which can cross with a drawing or a read in progress.
  3. Order between processes: the kernel runs GPU jobs one at a time, in arrival order. It is not yet verified that
     backboardd waits for the end of a surface write by SpringBoard before reading it (ios-pipa had to add this wait, section 8).
- **Control Center, Clock, Calendar**: they use RenderBox effects (blur, vibrancy, target read) and
  the same "live" paths; same probable causes as above, plus the blur (successive reduction passes).
- Liquid Glass: white on a light background. It depends on the same RenderBox passes and on the filters (blur, distance).
- Widgets stuck on "Waiting…", top of the home screen beige with a yellow edge.
- Announcements: `supportsFamily` only answers A1 and C1, and MSAA 4x is not announced although it works.

## 4. Gap with an iPhone (Metal features)

What an Apple GPU provides and that apps and the system assume:

1. **GPU families** (`MTLGPUFamilyApple7+`, `Metal3`): many apps and frameworks choose their path according to the family.
   We must announce what is really supported, no more.
2. **Reading the target in the shader** (programmable blending, `[[color(n)]]` as input): native on Apple GPUs, heavily used
   by RenderBox and CoreAnimation. On the Mali, panfrost does it by reading the tile buffer.
3. **Tile memory, imageblocks, memoryless storage** (`MTLStorageModeMemoryless`): to be emulated.
4. **MSAA** announced (4x already works), compressed formats (ASTC is native on the Mali), 3D/array/cubemap textures.
5. **Argument buffers, indirect command buffers, heaps, fences/events**: partly done (heaps, events), the rest to be tested.
6. **Raytracing, mesh shaders**: absent from the Mali-G76; we do not announce them.
7. **Fast compilation and pipeline cache**: on iPhone, pipelines are cached; here every process retranslates.

## 5. Optimization (memory, rendering, frames/s): measured findings

| Measurement | Value | Source |
|---|---|---|
| `CAMetalLayer` full screen | 10.9 frames/s (target 40) | B1L-0264 |
| 200 empty command buffers | 0.82 ms each | B1L-0264 |
| Total GPU memory at 3 min | ~290 MB (backboardd ~230 MB) | B1L-0247 |
| Wait on the global lock | high at startup (8 s expected for 5 s in SpringBoard), low afterwards | B1L-0262 |
| Lock held by pipeline creation | up to 18.3 s (iconservicesagent), 0.8 s (backboardd), 0.6 s (SpringBoard) | B1L-0267 |
| Lock held by `-commit` in backboardd | 10.3 s cumulative, 2.0 s in a single hold | B1L-0267 |
| `newLibraryWithSource` | 5.2 s | B1L-0264 |

Leads, in order:

0. **Shader translation outside the lock** (B1L-0268), then panfrost compilation outside the lock if it dominates.
1. **Stop waiting for the GPU under the lock**: `rwa_dispatch` (compute) ends each dispatch with a full wait
   (`finish_or_say_why "dispatch"`); texture reads and write-backs also wait.
2. **Cache of shader translations** on disk, shared between processes (like Apple's pipeline cache).
3. **Avoided copies**: `newBufferWithBytesNoCopy` answers with a copy, and buffer textures are "live copies"
   recopied at every use.
4. **GPU frequency and load**: DVFS close to the maximum, measurement of the real GPU time per frame.
5. **Memory**: free the disposable BOs, avoid RenderBox double buffers.

## 6. Work order (decided by the owner, 2026-11-15)

1. Repair the icons: multi-target passes R8/RG16F, target read, then Clock, Calendar, Control Center, glass.
2. Features of an iPhone's Metal GPU (section 4), each verified by the Dictaphone Test Metal app.
3. Optimization: memory, rendering, frames/s (section 5), measured at each build.

## 7. Diagnostic tools

- **Test Metal app**: `userspace/metal/b2l_metal_suite.m`, injected into Dictaphone by the kernel (boot-arg `b2l_metal_app=0` to turn it off).
  Results on screen and in the kernel log (`b2l-mtest:`).
- Driver lines in the kernel log: `B2L PASS`, `B2L DRAW`, `B2L LOCK`, `B2L WRITEBACK`, `B2L BUFTEX`.
- Images drawn by the GPU: `B2LMOD l-img-…` (kernel log or iOS logs from USERDATA), then `scripts/extract-b2lmod.py`
  and `scripts/b2limg-to-png.py`.
- Translator on the Mac: `air2nir [--ll|--list|--report] FILE [FUNCTION]` (build-mesa/build-host).

## 8. Why acceleration works on ios-pipa (review of 2026-11-15)

ios-pipa (MIT repository, Leo Malik, local clone from 12 November) is not a more advanced version of the same path:

- **Other iOS, other path**: its accelerated interface runs on iOS 16 (20H392). CoreAnimation draws there in OpenGL ES through
  an `EAGLServer` bridge (eagl.c, taken from HackiOS, MIT). **iOS 26 no longer has EAGLServer** (B1L-0031, `docs/gpu/plan.md` 4c): CoreAnimation,
  RenderBox and liquid glass only have Metal now. This bridge cannot be taken over.
- **Its icons are drawn by the processor**, not by the GPU: it does not have the iOS 26 RenderBox shaders to translate.
- Useful lessons for us, drawn from its `userland/metal/README.md` (hakios-metal driver for Adreno):
  1. **Surface write barriers tracked by the kernel**: display waits for the GPU to finish writing a surface before reading it. Without this, the image is half-drawn or old (our lead 3 in section 3).
  2. **Draws grouped into one pass**: 200 draws in 591 µs instead of 8,576 µs when each draw was a pass.
  3. **Suballocation per command buffer** of tables and programs: from ~200 µs to ~13 µs per draw.
  4. The Control Center (vibrancy) only worked with the **target read** in the shader: with us it passes the test (0265).
  5. Finding also on his side: "CoreAnimation in backboardd is limited by the processor"; the i/s are gained first by removing the work from the processor (copies, waits) around the GPU.
- Order adopted: 0268 (lock), then surface write barriers between processes, then draw grouping and suballocation, each measured by `B2L HOLD` and the Test Metal app.

## 9. OpenGL ES for apps (B1L-0342–0352)

```
Geometry Dash (OpenGL ES, CAEAGLLayer)
   │  DYLD_INSERT_LIBRARIES set by the kernel (turn off with b2l_gles_app=0)
   ▼
libb2l-gl-app.dylib: EAGL bridge (HakiOS) + userspace/b2l_eagl_app.m
   ▼
Mesa 25.0.7 / panfrost  →  Mali-G76
   │  present: GPU copy, turned over, into one of three IOSurfaces
   ▼
backboardd displays the IOSurface as the layer's contents
```

Geometry Dash 2.208, 3040x1440, 10-second windows:

| Build | Scene | Game frames made | Frames shown | GL work per frame | What changed |
| --- | --- | --- | --- | --- | --- |
| 0346 | first playable | – | ~12 fps | – | Frames copied by the CPU |
| 0349 | median | 30 fps | 30 fps | 5–14 ms, 17–33 ms in heavy scenes | Stable, no copy at display time |
| 0350 | one full window | 46 fps | 39 fps | 15.3 ms at 50 draws | Present is a GPU copy |
| 0351 | median | 57 fps | 38.5 fps | – | IOSurface cache cleaned only after CPU use |
| **0352** | simple (17 draws) | **60.0 fps** | **57.8 fps** | 11.2 ms | Diagnostic read-back removed |
| 0352 | medium (49 draws) | 58.2 fps | 56.7 fps | 11.5 ms | |
| 0352 | busy (57 draws) | 31.9 fps | 28.9 fps | 17.4 ms | |
| 0352 | very busy (74 draws) | not measured | 29.1 fps | 23.8 ms | |

The bridge library is built by `scripts/build-gl-app.sh` from the sources and Mesa library prepared by the GL build; an app
bundle is staged by `scripts/stage-app.py`. No game file is in this repository.

Limits: one game tested; a present waits for the GPU (`glFinish`); vsync is a software timer that is not locked to the panel.
