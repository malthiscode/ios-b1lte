# GPU acceleration (Metal on the Mali-G76): where we are

Updated: 2026-11-22, B1L-0353: last build, GPU work paused (see the end of this file); earlier: 2026-11-14, B1L-0238: SpringBoard crash in the drawing return fixed; B1L-0237: images drawn by the GPU sent to the Mac (diagnostic); B1L-0236: the drawing returns into RenderBox's image memory (buffers without copy); B1L-0235: drawing into a texture on a buffer is no longer overwritten; B1L-0234: linked functions of a pipeline (`custom_fn` of RenderBox effects); B1L-0233: diagnostic of RenderBox custom effects; B1L-0232: RGB10A2Unorm format (SpringBoard's 122×122 icons); B1L-0231: texture set on a buffer (copy kept up to date); B1L-0230: shader sampler count fixed (`fa16dhmha4` refused by the GPU, 0x5A); B1L-0229: zero size for an unbound texture, no more freeze at boot but no icons; B1L-0228: unbound textures accepted, freeze on the lock screen; B1L-0227: no shader refused any more; B1L-0226: pointer phi (RenderBox traces, tested); B1L-0225: 3 refused pipelines found, cause of the 0188 crash proven on the Mac; B1L-0224: modules of RenderBox's refused shaders sent to the Mac; icon cache correct, defects in SpringBoard's live drawing; 2026-11-12, B1L-0191: return to the state of 0187 (last build with no reported problem), fixes 0188–0190 set aside; B1L-0190: `return` in the middle of a lowered shader, pointer `select` rendered to QuartzCore (flicker, under test); B1L-0189: a jump that Bifrost cannot emit is refused and named instead of crashing (under test); B1L-0188: the translator accepts structure and pointer phis in the same buffer (RenderBox shaders, under test); B1L-0187: driver log opened to SpringBoard, chronod and iconservicesagent (diagnostic); B1L-0186: `newBufferWithBytesNoCopy:` answers with a copy (RenderBox was crashing SpringBoard, under test); B1L-0185: big patch, recycled GPU memory zeroed and GPU opened to SpringBoard, chronod and iconservicesagent (under test); B1L-0136: copies of IOSurfaces with unaligned rows are re-read before each draw (live icon snapshots); 0135: texture inventory (uploads and formats read by each pass); 0134: chronod IOSurface calls; 0133: list of imported images; 0132: images with unaligned rows copied; 0131: reload of re-read targets; 0130: inventory; 0129: clear by attachment (vector traces); 0128: Liquid Glass and photo as wallpaper (re-flash: `b1l-tools/flash-saved.sh 0128`); 0123: partial views; 0121: glass textures allocated; 0120: return to 0115; 0115: very smooth; 0113: where the time goes; B1L-0112: crash on opening apps fixed, still slow (earlier: B1L-0111 crash unchanged; 0110 + command buffers kept 2 s + imagent/dasd blocked; earlier: diagnostic of command buffer lifetime; earlier: 30 s hold; earlier: asynchronous command buffers, crash fixes; earlier: large GPU buffers, texture views, switch without active loop; earlier: 64-byte lines for SpringBoard; contains 0102: Mesa LDAPR rewritten, kernel call timing; earlier: PAC interrupt profile; contains 0100 quick check and 0099 viewport, scissor, 64-byte lines; earlier: zero copy with backboardd images, RG8 texture for glass brightness; earlier: backboardd no longer draws into displayed memory; one-time cache cleanup; search for the display channel).

## Foundation (done before Metal)

- [x] GPU kernel driver (HakiosMali): the Mali powers on, runs jobs, has its own page table
- [x] GPU frequency set (about 676 MHz) and thermal protection
- [x] Mesa (panfrost, Bifrost compiler) built for iOS 26 and linked to the kernel driver
- [x] OpenGL ES test: iOS was displayed by the GPU (B1L-0041/42), but slower and more broken than the CPU, so abandoned in favour of Metal (kept as fallback, `b2l_gl=1`)
- [x] Memory: the low-bank RAM is treated as DRAM, the framebuffer is accessible to the GPU (B1L-0040/42)

## M1: iOS sees a Metal GPU

- [x] `IOAcceleratorES` kernel service announcing a Metal plug-in (B1L-0044)
- [x] Plug-in installed in `/System/Library/Extensions` and loaded by Metal
- [x] `MTLCreateSystemDefaultDevice()` returns our GPU (B1L-0045)
- [x] Metal crashes fixed (`targetDeviceArchitecture`, B1L-0047)
- [x] The test runs outside backboardd, so the interface is never touched (B1L-0049, then `weatherd`)

## M2: memory and copies done by the GPU

- [x] Buffers in GPU memory, readable and writable by the CPU
- [x] Command queue, command buffers, commit and wait
- [x] 256 KB copy and fill done by the Mali, exact to the byte (B1L-0051, 16 ms)

## M3: run Apple shaders (compute)

- [x] Scion's Mali Metal driver integrated (MIT licence): AIR → NIR translator → Bifrost compiler
- [x] Full Metal GPU: 0 missing methods out of 129 (MTLDevice) and out of 414 (MTLDeviceSPI)
- [x] iOS 16 addresses fixed for iOS 26 (`MTLAddDevice`, B1L-0055)
- [x] `image_calcminmax0` and `MetalFaceMask_InitMinMax` (iOS photo filters) compiled for the Mali and run with correct results (B1L-0055)

## M4: draw with Apple shaders

- [x] Apple graphics engine shader library loaded (274 functions), render pipeline built, draw completed (`status 4`)
- [x] Fix B1L-0058 (list of shader inputs and outputs given to panfrost): useful but **not enough**, images still empty
- [x] Comparison with the software engine (B1L-0059): **all drawings are correct in software**, so the Metal translation is sound and the bug is only in the Mali driver path (panfrost)
- [x] Trace of a draw on the Mali (B1L-0060): geometry, viewport and destination images correct; lead: the colour may be written to the wrong image (target 0 instead of 2)
- [x] Trace of the draw with QuartzCore's vertex shader (B1L-0061): the position matrix and the colours both come from a constant buffer, which reads as zeros on the Mali; what does not depend on it (the texture coordinate) arrives correctly
- [x] Constants copied by the driver (B1L-0062): the right values do reach the GPU, but the shader still reads zero → reading a constant buffer from the shader (`LOAD.ubo`) does not work on this Mali; it had never been used in OpenGL ES
- [x] Constants pushed directly into the shader registers (B1L-0063): no change, the cause is elsewhere or incomplete
- [x] Trace with pushed constants (B1L-0064): the shader now receives its constants, but image no. 2 stays empty → the problem is in the colour write (which image is targeted, and with which format)
- [x] Reading of the three images (B1L-0065): nothing is written anywhere → it is not the write, it is the **coverage** (the triangles cover almost no pixels)
- [x] **Cause found:** on the Mali, the viewport transform is added by the compiler in the vertex shader, but the pass that does it did not recognise the shape of the shaders prepared by Scion's library → positions never transformed, everything fell on pixel (0,0)
- [x] **Fix B1L-0066 validated: the draw with QuartzCore's vertex shader is correct on the Mali** (full grid, 16 pixels out of 16, identical to the software engine)
- [x] Trace of the 3-image draw (B1L-0067): write settings valid; the case "only the 3rd image is written" stays empty, set aside (single-image rendering, the one QuartzCore uses, works)
- [ ] Index buffer offset and very thin triangles: small differences from the software engine
- [ ] Apple shader pixel identical to the software reference (`2458616957 …`)
- [ ] Indexed draws correct (right half of the image)
- [x] Draw with QuartzCore's vertex shader: full 16-pixel grid (B1L-0066)
- [ ] Textures, blending and depth verified

## M5: draw into screen surfaces (IOSurface): done

- [x] Create a Metal texture on an IOSurface (a window's memory): **the Mali clears it red then green, 4096 pixels out of 4096, read back correctly by the CPU (B1L-0070)**
  - [x] B1L-0068: device and IOSurface OK, but texture refused (Scion's driver looked for the Adreno kernel service, `HakiosGPU`)
  - [x] B1L-0069: Mali service (`HakiosMali`) used: **the IOSurface texture is created**; but the clear writes nothing
  - [x] B1L-0070: Scion's driver only cleared during a draw; fixed, **successful**
- [x] Bug found along the way: each draw in a pass re-cleared the previous draws (fixed in B1L-0070, essential for QuartzCore)
- [x] CPU/GPU cache on these surfaces: both directions correct in the test (CPU pattern overwritten by the GPU, GPU colour seen by the CPU)
- [ ] `newTextureWithDescriptor:iosurface:plane:slice:` still an Apple stub (aborts if called)
- [x] A triangle drawn by Metal into an IOSurface, read back correctly by the CPU
  - [x] B1L-0071: **identical to the Mac, 0 lines out of 64 differ**: two triangles (`std_vert0_lpf` + `attachment_clear_frag_lpf` from QuartzCore) in one pass, compared line by line with the image from the Mac's GPU (2005 pixels)
- [x] Driver speed: a single GPU wait per command buffer, one batch per pass, no more PAC trap per draw (from 8–15 ms to ~0.03–0.05 ms per draw)
  - [x] B1L-0074: no more wait per draw, a single GPU batch per pass: 2 triangles in 7.8 ms (instead of 16 to 30), 200 correct draws; the GPU now waits only 2 to 11 ms, but the CPU costs 1.2 to 3.4 ms per draw
  - [x] B1L-0075: Mesa and the driver without signed returns: **0.65 to 0.91 ms per draw** (about 3 times better), images still correct
  - [x] B1L-0076: without signed calls: **0.55 to 0.58 ms per draw**, images correct
  - [x] B1L-0077: profile: **48 traps per draw, all in the linker's trampolines** (`__auth_stubs`, `__objc_stubs`), none in Apple's code; the large core does not help
  - [x] B1L-0078/0079: trampolines rewritten without signed branch: **0 traps per draw, 26 to 56 µs per draw** (0.2 ms target exceeded)
- [x] `CAMetalLayer` and its drawables (what QuartzCore uses to present an image)
  - [x] B1L-0072: **`nextDrawable` works** (CoreAnimation creates the surface, our driver supplies the texture), `presentDrawable` at status 4; but the draw is refused (the texture was not marked as a render target)
  - [x] B1L-0073: **QuartzCore drawing into the drawable identical to the Mac (0 lines out of 64 differ), `presentDrawable` at status 4**

## M6: iOS's interface rendered by Metal

Detailed plan and decisions: [docs/plans/speed-and-m6.md](../plans/speed-and-m6.md). Success criteria: 40 frames/s while scrolling on the home screen, correct Liquid Glass, faster than CPU rendering.

- [x] M6a: test app with a full-screen `CAMetalLayer`, shown by the current server (CPU): real presentation, frames/s
  - [x] B1L-0079: **shown on screen** (animated background, rotating triangles); 20 to 23 frames/s; per frame: encoding 0.17 ms, GPU ~6 ms, drawable wait ~32 ms (backboardd's CPU compositor is the bottleneck)
- [ ] M6b: backboardd on the Mali, behind the kernel switch `b2l_metal_ui` (enabled in the test BOOT, automatic return to CPU if backboardd crashes twice)
  - [x] B1L-0080: backboardd does take the Mali, but crashes (`targetDeviceArchitecture` missing); the automatic return to CPU worked
  - [x] B1L-0081: backboardd compiles QuartzCore's pipelines, but 4 are refused (inline samplers in a two-word shape); CoreAnimation stops, return to CPU
  - [x] B1L-0082: inline samplers fixed; two new refusals (`air.get_read_sampler`, a pointer with a computed offset passed to a function)
  - [x] B1L-0083: these two cases pass; new refusals (insertelement at a computed position, pointer in place of a number)
  - [x] `air2nir` tool on the Mac: the 189 QuartzCore shaders checked in a few seconds, without boot
  - [x] B1L-0084: **backboardd draws on the Mali for the first time** (a 1440×3040 image sent to the screen); but the Mali stops the job on an invalid instruction (0x51, staircase image), then a pipeline is refused (depth)
  - [x] B1L-0085: the *function constants* do arrive (modules already specialised by QuartzCore, `A2Xghfc` = 75,592 instructions); the depth refusal disappeared; the invalid instruction 0x51 remains
  - [x] B1L-0086: `/b2l-dump` lands on USERDATA (unreadable from the Mac); the fault each time follows `A2Xhf` (7 instructions) then `TimgA2Xhf_Isrc`
  - [x] B1L-0087: backboardd no longer crashes (4 images); SpringBoard was crashing because of the plug-in (`[self release]`); 19 backboardd modules recovered
  - [x] **Cause of the invalid instruction found on the Mac** (`b2l-bicomp`, Mesa with assertions): `pan_lower_noperspective` multiplies a 16-bit varying by a 32-bit `w`
  - [x] B1L-0088: **iOS's interface is displayed rendered by the GPU** (home screen, Settings, Control Centre, Contacts, keyboard); defects: cyan tint, Liquid Glass without blur, left edge cut off, slowness
  - [x] B1L-0089: the assertions name the causes: 16-bit varyings forbidden for writing on v7, intrinsics left by system values (including `LEA_BUFFER`, a Valhall instruction)
  - [x] B1L-0090: varyings widened to 32 bits, replaced instructions removed; no more 0x51 "did not finish" fault, but an assertion on `demote` (main shader `A2Xghfc`) stops backboardd twice, hence the fallback to CPU rendering
  - [x] B1L-0091: `demote`/`demote_if` emitted as `discard` by Bifrost; **interface on the GPU with the blur** (Control Centre); defects: blue background, Liquid Glass opaque grey, Clock/Calendar icons transparent, slow Control Centre; log: 112 invalid instructions (0x51), 30 MMU faults, 14 of them on an already freed buffer
  - [x] B1L-0092: `compute_average_luma` (brightness for the Liquid Glass tint) no longer refused: shared memory size given at dispatch; tested: the Liquid Glass bubble of Files' tabs works, Control Centre tiles still grey; the dispatch still fails (texture 0 without a texture from the library beneath)
  - [x] B1L-0093: diagnostic; **every failed backboardd batch contains `A2Xghfc`**; on the Mac, the invalid instruction comes from `bi_lower_swizzle` (Mesa)
  - [ ] B1L-0094: `bi_lower_swizzle` fixed (3 cases); the 41 modules compile on the Mac with assertions; flashed, tested with 0095
  - [x] B1L-0095: shader cache + measurements; **0 faults 0x51**; cache cleanup up to 2.9 s / 10 s, screen copy 16 ms per frame; progressive display (the screen was not waiting for the GPU)
  - [x] B1L-0096: zero copy never enabled (IDMA blocks assumed at 0); cause of the progressive drawing found (surface 1 = displayed memory) (DECON/IDMA registers of the Exynos 9820), wait for the GPU before displaying, contiguous screen surfaces without cache cleanup
  - [x] B1L-0097: no more progressive drawing; GPU submission 2–22 ms / 10 s; panel address found at `0x19071040`; default surface in ordinary memory, one-time cache cleanup, search for the panel address in the display registers
  - [x] B1L-0098: **zero copy active (0 CPU copies)**; screen switch with line width (`IDMA_SRC_SIZE`), backboardd surfaces contiguous and write-combined, `RG8Unorm` import; slideshow due to PAC emulation (20,000–80,000 interrupts/s, 11 µs each); viewport ignored 1126 times
  - [ ] B1L-0099: viewport (rectangle) and scissor served; backboardd lines rounded to 64 bytes
  - [x] B1L-0101: PAC profile; emulation 1.9 µs per interrupt; dasd and other daemons in corecrypto; backboardd: Mesa's LDAPR; but backboardd stays at 82 s kernel / 90 for another reason
  - [x] B1L-0100: quick check (11 µs → 1.9 µs per PAC interrupt)
  - [x] B1L-0099: viewport and scissor (visually better)
  - [x] B1L-0102: LDAPR → LDAR in our libraries; timing of HakiosMali/IOSurface/IOMFB calls
  - [x] B1L-0103: backboardd no longer burns the CPU (LDAPR); 2–7 frames/s, GPU 2–7%: waiting bottleneck; large GPU buffers refused by the kernel; texture views `nil`; lines rounded to 64 bytes also for SpringBoard (`LA08` source of the brightness calculation)
  - [x] B1L-0104: no more refused buffer, no brightness failure, no GPU fault; backboardd crashes (Calculator); GPU buffers ≥ 1 MB allocated by pages, texture views, screen switch when asleep
  - [x] B1L-0105: asynchronous wait OK; 10 frames/s steady; crash on opening an app (freed command buffer); one barrier per command buffer, "completed" callbacks off the render thread; `commitAndWaitUntilSubmitted`; parent of texture views
  - [x] B1L-0106: crash still there (command buffer without callback freed); up to 24 switches/s at times; command buffer held until its callbacks are delivered
  - [x] B1L-0107: remaining crash (hold released after 1 s, too early); command buffer held as soon as it is created (released after its callbacks, or 1 s without submission)
  - [x] B1L-0108: crash unchanged; 30 s hold for an unsubmitted buffer, queue slot returned
  - [x] B1L-0109: buffers alive at submission; crash in objc_msgSend on a random isa (destroyed object); command buffer lifetime log (commitAndWait, dealloc)
  - [ ] B1L-0110: GPU fixed at 650 MHz, diagnostics cut (b2l_diag=2, b2l_perf, b2l_pacprof), non-blocking screen switch (built, not flashed alone)
  - [x] B1L-0111: GPU 650 MHz and blocked daemons confirmed; crash unchanged; 0110 + command buffers kept 2 s after their callbacks + imagent and dasd blocked
  - [x] B1L-0112: **crash on opening apps fixed** (freed resources sent by `-commit`: the recorded work now keeps them); a bit faster, still very slow; GPU busy ≈ 8%
  - [x] B1L-0113: sampler: CoreAnimation's display thread does all the work (73% in use, 35% at rest); 3–8 frames/s in use, 10.0 fixed at rest; two hot CPU copies: `-replaceRegion:` (`util_copy_box`, page faults) and the constants copied in full at every draw (`panfrost_emit_const_buf`)
  - [ ] B1L-0114: extended sampler (kernel pc, fault type, page faults, stacks of active threads); flashed, tested with 0115
  - [x] B1L-0115: **"extremely smoother"**: constants copied only up to where the shader reads (2 to 3.6 GB avoided / 5 s); 10–14 frames/s
  - [x] B1L-0116: wallpaper drawn (gradient, waves, dots) instead of the flat blue (displayed correctly)
  - [x] B1L-0117: boot-arg `b2l_diag` read once (it was re-read at every system call of backboardd/SpringBoard)
  - [x] B1L-0118: shader states cached by content → **regression** (with 0119: 544 GPU faults at address 0, slow, broken image); removed in 0120
  - [x] B1L-0119: memory state when a GPU buffer is refused (no refusal during this test)
  - [x] B1L-0120: 0118 removed: **return to the state of 0115 + wallpaper**; 0 GPU faults; glass grey because its textures (24–36 MB) are refused with 1.2 GB of free RAM (`kmem_data_shared` range of 1.93 GB full)
  - [x] B1L-0121: `kmem_data_shared` range at 2,999 MB: 0 refusals, 0 GPU faults; glass still grey/black (no copy encoder)
  - [x] B1L-0122: copy encoder (blit); QuartzCore only uses it for small pieces: glass unchanged
  - [x] B1L-0123: level 1–3 views of a 768×1600 pyramid correctly requested; but 11 GPU faults (reads after a 64/16 KB pool), flickering, Control Centre more broken
  - [x] B1L-0124: worse: 46 GPU faults (including freed buffers), flickering of the whole interface; the constants bound was not the cause
  - [x] B1L-0125: no more flickering, owner's photo as wallpaper; glass still grey/black; 6 GPU faults in the glass passes
  - [x] B1L-0126: GPU fault diagnostic; no fault during the test
  - [x] B1L-0127: **real Liquid Glass on the Focus button of Control Centre** with the mipmap views; 14 GPU faults at addresses never given; 170 draws refused (attachments of different sizes); black background
  - [x] B1L-0128: **Liquid Glass rendered, photo as wallpaper, "works very well"**; re-flashable: `b1l-tools/flash-saved.sh 0128`
  - [x] B1L-0129: each attachment has its own load action: correct, but **no change on screen** (glyphs, widgets still missing). The bright top of the wallpaper is the photo itself
  - [x] B1L-0130: inventory: no `path_*`; vibrant glyphs = `fixed_frag`, which re-reads attachments 0 and 1 (BGRA8 + RGBA16Float)
  - [x] B1L-0131: panfrost reloads the targets re-read by the shader: Wi-Fi, AirDrop, Bluetooth and AirPlay glyphs of Control Centre rendered
  - [x] B1L-0132: images with unaligned rows copied instead of refused: works, but no visible change
  - [x] B1L-0133: backboardd imports only 3 IOSurfaces; power-off menu icons repaired (thanks to 0132); chronod receives "unsupported" for 4 IOSurface selectors
  - [x] B1L-0134: the kernel accepts these 4 chronod calls: no visible change. Re-read of log 0134: 7 IOSurface imports, all large (background, dock, brightness, InCallService, Preferences), no glyph or widget surface
  - [x] B1L-0135: glyph and icon textures do reach the shaders (23 RGBA8 shapes all sampled); only 3 imports; the `240x240 LA08` icon snapshots are imported only once, **before** the application draws them
  - [x] B1L-0136: the IOSurface copy with unaligned rows is re-read before each draw (live icon snapshots); diagnostic `B2L LIVE`. **Finding: the "stale copy" lead is not the cause — the path is never executed.** No `240x240` surface is imported, no `B2L LIVE: surface`, no `copied into a texture`. `MobileTimer id 16`, `MobileCal id 26/30`, `Spotlight id 14` do create `240x240 LA08` surfaces, but they are created then abandoned (never looked up, never imported). `docs/journal/build-b1l-0136.md` completed.
  - [ ] B1L-0185: big patch (2 changes grouped at the owner's request): resource buffers reused from the panfrost cache zeroed (blue corners, tint on first display); Metal GPU given to SpringBoard, chronod and iconservicesagent in addition to backboardd (glyphs, Clock, Calendar, widgets). Installed (BOOT `06ffd047…`), under test. Note: kernels 0137–0184 all reported driver v85. **Result: SpringBoard crashes in a loop in RenderBox (`tables_buffer`), the lead of the GPU outside backboardd is confirmed.**
  - [x] B1L-0186: `newBufferWithBytesNoCopy:` answers with a copy instead of `nil`. **iOS starts with 4 processes on the GPU; purple tint gone (0185).** Clock/Calendar icons, glyphs and widgets still missing.
  - [x] B1L-0187: diagnostic, the driver also logs SpringBoard, chronod and iconservicesagent. **Cause found: the AIR → NIR translator refuses RenderBox (SwiftUI) shaders: structure phi, pointer phi on a loop edge (`uber_vertex`).** Reproducible on the Mac: `air2nir --report out/rb-check/default.metallib`.
  - [x] B1L-0188: structure phis, pointer phis in the same buffer and pointer `select` translated (`apply.py`, `air_to_nir.c`). `uber_vertex` translates on the Mac. **Result: the shaders pass the translator, but SpringBoard crashes in a loop in Bifrost (`bi_emit_jump`, null jump target).**
  - [x] B1L-0189: guard, a jump that Bifrost cannot emit is refused with the shader's name; null target removed in `bi_emit_jump`. **Result: starts; only `fa16dhmha4` remains refused (`return` in the middle); but the interface flickers while moving.**
  - [x] B1L-0190: `nir_lower_returns`; pointer `select` as before 0188 for QuartzCore. **Result: no improvement, apps slow to open.**
  - [x] B1L-0191: return to the state of 0187 at the owner's request (BOOT `dbb192a5…`). Fixes 0188–0190 kept in `userspace/mesa/air-b2l/renderbox-0188-0190.py.txt`. **Learned: the RenderBox shaders all compile with these fixes, but nothing more appears; the blocking is after the drawing.**
  - [x] 2026-11-14 (Mac, no flash): iOS icon cache read from USERDATA: **correct icons** (Clock with its ticks) → the defects come from RenderBox's live drawing in SpringBoard; `b2l-bicomp` restored: with 0188, `uber_vertex` passes Bifrost but produces **75 KB of code** (suspect for the slowness of 0190)
  - [x] B1L-0224: diagnostic (RenderBox refused pipelines named, modules sent to the Mac); **did not run**: the kernel never replaces the libraries of an already published set (`v95` kept by mistake)
  - [x] B1L-0225: diagnostic: **only 3 refused pipelines** (`fa16dhmha4`+`uber_vertex` structure phi; two trace pipelines pointer phi); modules in `out/b2lmod-0225`. On the Mac: **cause of the 0188 crash = `return` embedded without being lowered** (and 0190 lowered it too late); with the right order, the 6 modules compile; `fa16dhmha4` = 93 KB of code
  - [x] B1L-0226: pointer phi (trace pipelines): **RenderBox's traces pass and draw**, no more refusals for them; screen unchanged (Clock without ticks, Calendar empty, Control Centre glyphs missing)
  - [x] B1L-0227: structure phi + `return` lowered before embedding: **no shader refused any more**; screen unchanged; but 34 SpringBoard draws refused (`fa16dhmha4` reads an unbound texture)
  - [ ] B1L-0228: declared but unbound textures/samplers accepted (null texture, default sampler); flashed (BOOT `85a6ddb5…`): freeze on the lock screen, SpringBoard crashes on the size of an unbound texture
  - [ ] B1L-0229: size 0 for an unbound texture (`panfrost_upload_txs_sysval`); flashed (BOOT `7b8e7cbe…`): starts, but no icons, freezes; 70 GPU jobs in 0x5A on `fa16dhmha4`
  - [ ] B1L-0230: `sampler_count` = samplers used (up to 14), not the number of textures; flashed (BOOT `3c308e95…`): Clock and Calendar drawn (ribbed dial), disappear on page change; Control Centre without icons; clean log (no refused job)
  - [ ] B1L-0231: `newTextureWithDescriptor:offset:bytesPerRow:` from a buffer = copy kept up to date; flashed (BOOT `5587ec82…`): no visible change; format 90 (RGB10A2) refused
  - [ ] B1L-0232: `MTLPixelFormatRGB10A2Unorm` → `R10G10B10A2_UNORM` (bit order taken from MoltenVK); flashed (BOOT `3d6ee122…`): no visible difference; 38 RenderBox "custom effect" pipelines refused (`custom_fn` function assembled separately, not handled)
  - [ ] B1L-0233 (diagnostic): linked functions, assemblies and compiled source noted in the log; flashed (BOOT `94d0677b…`): `custom_fn` arrives as an AIR module in `fragmentLinkedFunctions.privateFunctions` (no assembly)
  - [ ] B1L-0234: linked modules read with the shader, `<f>.MTL_VISIBLE_FN_REF` → `<f>`; passes on the Mac; flashed (BOOT `6f52d428…`): clean log (no refusal), no better on screen
  - [ ] B1L-0235: texture on a buffer where the GPU writes = filled only once; flashed (BOOT `f85666a4…`): no better
  - [ ] B1L-0236: drawing return (texture on buffer → buffer → caller's bytes) at the end of the command buffer; flashed (BOOT `c5dae724…`): the return runs, no better on screen
  - [ ] B1L-0237 (diagnostic): images of the buffer-backed targets sent to the Mac; flashed (BOOT `e967e7d7…`): shapes correct but riddled with noise, 204×204 icon almost empty; SpringBoard crashes in `hakiosSyncOut`
  - [ ] B1L-0238: `hakiosSyncOut` protected (caller present, projected zone); flashed (BOOT `512f97a0…`), under test; next lead: chronod's two-plane YCbCr surfaces
  - [ ] To do next: texture set on a buffer, chronod.
- [ ] B1L-0137: instrumentation of IOSurface accesses (lookup, create port, metadata set/copy) with the surface ID and the requester's PID, to identify who consumes the `240x240 LA08` surface and why backboardd never imports it. Set `b2l-metal-v86`, kernel only. Finding: no `240x240` surface is ever looked up; the path does not exist.
- [ ] B1L-0138: accepts IOSurface selector 17 for `iconservicesagent` (like chronod in 0134). Finding: `accepted 17`, but still no lookup on `240x240` surfaces; selector 17 was not the blocker.
- [ ] B1L-0139: userspace instrumentation (`hakiosmetal_air.m`, set `b2l-metal-v88`) to trace: (1) creation of a `240x240` texture via `newTextureWithDescriptor:`, (2) binding a `240x240` texture to a fragment sampler (`setFragmentTexture:atIndex:`), (3) upload to a `240x240` texture (`replaceRegion:`). Goal: confirm whether the Metal path consumes the `240x240 LA08` icon snapshots or not.
  - [ ] The 16 kernels with imageblocks: reserved for Apple 4+ GPUs, presumably not requested from our GPU declared as Apple 1 (to be confirmed in the log)

- [x] QuartzCore (in backboardd) accepts our GPU instead of CPU rendering (B1L-0088)
- [ ] Its 189 shaders (`QuartzCore.framework/default.metallib`) compiled for the Mali: 173 translated by the AIR translator (B1L-0084), the 16 imageblock kernels remain
- [ ] Apple's precompiled pipeline archive refused cleanly (without crash)
- [x] Lock screen displayed by the GPU (B1L-0088, wrong colours)
- [ ] Liquid Glass (blur, glass) rendered
- [ ] On-disk shader cache, to avoid compilation stutters
- [ ] Smoothness measured: 40 frames/s while scrolling on the home screen, and faster than CPU rendering

## On the side: the CPU

- [x] PAC traps measured by type: 70% signed returns, 25% signed calls, 5% signed reads, almost all in the shared cache
- [x] Voice synthesisers that crashed in a loop blocked (B1L-0052)
- [x] Main lead found: corecrypto, excluded from PAC translation
- [ ] Reduce the PAC traps in Apple's code: patch at load time by the kernel (`retab`→`ret`, `blraaz`→`blr`, `aut*`→NOP), after M6, corecrypto excluded
- [ ] "About" app (Informations) that does not open
- [x] B1L-0164: keyboard frozen → dictation cut in UIKit (keyboard dock drowned by the dictation availability changes); tested: keyboard smooth
- [x] B1L-0165: bottom gestures (home, multitasking) → edge markers (FromEdgeTip + direction) in touch events; tested: gestures OK
- [ ] B1L-0166: start-up ~3 min → purge of the 63,523 stale records of the native cache (60 s per start); flashed, to test
- **0239** (diagnostic): surface receptions per port noted, images per process; tested: no chronod surface received, icons copied again only if someone waits → 0240.
- **0240**: RenderBox drawings copied again even without the caller waiting; tested: no effect (the case never happens).
- **0241**: "Metal Test" app in Clock (on-screen tests); not tested: Clock takes ~30 s to open.
- **0242**: "Metal Test" app moved to Voice Memos (opens in 2 s); tested: 24 OK, 9 FAIL (system device nil, families, MSAA, NoCopy+blit, blit→texture, compute→texture, MTLSharedEvent), CAMetalLayer 11–18 i/s; build from source OK. Lead: slow Clock/Calendar = waiting for their live icon at iconservicesagent.
- **0243**: buffer ↔ texture copies encoded, buffer without copy handed back to the app after copy; flashed (BOOT `b86e33bc…`), **tested OK** (failures 4 and 5 fixed).
- **0244**: `write_texture` (compute) stores with the type of the value; flashed (BOOT `791ecd3f…`), **tested OK** (failure 6 fixed).
- **0245**: MSAA resolve (Gallium blit); start-up beep cut (audio level 5); flashed (BOOT `f062d363…`), **tested OK** (red resolve; beep not played).
- **0246**: MTLEvent / MTLSharedEvent (signal at the end of the command buffer, listeners); flashed (BOOT `18fa0c62…`), **tested OK**; SpringBoard requests no event.
- **0247** (diagnostic): GPU memory per process every 60 s, portion marked discardable by Mesa; flashed (BOOT `b5ee8db6…`), tested: ~290 MB real at 3 min (backboardd ~230 MB), of which 27 MB discardable. Metal Test: **29 OK, 4 FAIL** (system device ×2, families, MSAA announced), CAMetalLayer 10.9 i/s. Goal: the whole system on the GPU without too much memory.
- **0248**: GPU opened to all processes (B1L-0185 list lifted, boot-arg `b2l_gpu_all=0` to restore it); set v118; first flash without libraries (set misnamed `v118`) → no GPU; re-flashed (BOOT `4921dded…`), to test.
- **0249**: app extensions (none registered): ExtensionKit setting, LaunchServices base rebuilt, fairplayd.H2 cut (writes USERDATA, owner's agreement); flashed (BOOT `ea11bae0…`): **worse**, all icons transparent and apps freeze; ExtensionKit setting not written (EPERM).
- **0250**: 0249 code cut by default (boot-arg `b2l_ext_reset=1` to restore it), before the clean USERDATA; flashed (BOOT `39be1437…`), phone left in TWRP.
- **0251** (tested: crash still there, grey icons): iconservicesagent crash localised (IconRendering+0x55208, nil weak reference); `bufferBytesPerRow`/`bufferOffset` of buffer-backed textures fixed (they answered 0); set v119; flashed (BOOT `f0efd1da…`), to test.
- **0252**: Metal Test app removed from Voice Memos by default (boot-arg `b2l_metal_app=1` to restore it); flashed (BOOT `46587916…`), to test.
- **0253**: cause of the iconservicesagent crash found — the `deallocator` block of the no-copy buffers was dropped, IconRendering's original buffer freed too early; block kept and called on release; set v120; flashed (BOOT `f5035bc6…`): **crash gone**, but icons still empty.
- **0254** (kernel): HFS+ accepts PosterBoard's APFS "purgeable" request 0xC0084A44 (without a poster, SpringBoard applies no icon style); flashed (BOOT `cc011e66…`): **PosterBoard repaired** (iOS logs: no more purgeable error, icon style received), icons still empty.
- **0255** (kernel): the 4 other PosterFoundation "purgeable" requests also accepted on HFS+ (safety net); flashed (BOOT `93138266…`).
- **0256**: Metal heap (`newHeapWithDescriptor:`): RenderBox, which draws the icons, was logging `unable to create heap`; set v121; flashed (BOOT `233447fc…`): heap accepted, icons still empty.
- **0257** (Mesa): the translator keeps a pointer stored in a local variable (SpringBoard's RenderBox "custom effect" pipeline, which places the pictogram in the icon, refused 19 times); icon cache from 0248 lost in the clean USERDATA (370 → 3); set v122; flashed (BOOT `65ec61fa…`): **icons partly back**; then memory exhausted → SpringBoard crashes → panic.
- **0258** (kernel): the GPU memory that Mesa marks as discardable (~600 MB) is really reclaimed when memory runs short (like panfrost's shrinker in Linux); flashed (BOOT `9ba922e4…`). Tested: stuck on the Apple logo, SpringBoard crashes in a loop (ctx->prog[VS] NULL on secondary threads); purge not triggered.
- **0259** (set v123): a global lock around all calls to the Gallium context in hakiosmetal_air.m (SpringBoard was drawing from several threads without a lock); flashed (BOOT `04b02d7e…`): **boots, apps openable**, icons mostly present but pictograms distorted or empty; Calendar/Clock unchanged; top of the wallpaper beige; widgets "Waiting…".
- **0260** (diagnostic, set v124): 0259 icon cache re-read → pictograms cut diagonally (one triangle in two wrong, custom effect pipelines); `B2L DRAW` line per draw shape; flashed (BOOT `892145ce…`): RenderBox draws in batches `1 0 2 3 65535 5 4 6…`, and primitive restart was disabled.
- **0261** (Mesa, set v125): primitive restart at 0xFFFF/0xFFFFFFFF for indexed draws, as Metal does; flashed (BOOT `829d9e95…`): still broken on screen (0259's icon cache reused?), very slow; icon cache erased on USERDATA (agreed, backup kept) → **new cache correct (140 icons, Clock and Calendar included)**, but SpringBoard breaks again later; slow.
- **0262** (diagnostic, set v126): images of SpringBoard's late draws and wait on the lock; tested: icons broken again after a while; lock contended only at start-up; 0262 icon cache entirely correct (breaks on live drawing).
- **0263** (Mesa, set v127): Metal buffers created with persistent mapping; panfrost was replacing the BO under the `contents` pointer during a copy/fill (stale data → strokes/triangles); tested: no visible improvement (horizontal streaks, slowness); R8 masks wrong every other line per 2×2 block → derivatives lead.
- **0264** (test, set v128): Metal Test app put back in Voice Memos + tests of the derivatives and of the R8/RG16F/BGRA8 pass (2×2 parity); tested: 35 OK, 3 FAIL; derivatives correct; **R8+RG16F+BGRA8 pass: only BGRA8 written** (RenderBox accumulates its coverage in RG16F).
- **0265** (test, set v129): separate targets (R8, RG16F, float/float2/float4), 3 BGRA8 targets, target read-back; tested: targets written but **position shifted by half a pixel** (0264 had been wrong); read-back OK.
- **0266** (Mesa, set v130): fragment position +0.5 (panfrost gives the pixel corner on Bifrost); tested: same problem (icons broken as soon as they move), very slow; lock waited 4.1 s out of 5 s in SpringBoard.
- **0267** (diagnostic, set v131): lock hold time in places (`B2L HOLD`). Tested: 43 OK, 1 FAIL (MSAA not announced), `CAMetalLayer`
  9.5 i/s. Pipeline creation holds the lock for up to 18.3 s (iconservicesagent), 0.8 s (backboardd), 0.6 s (SpringBoard).
- **0268** (optimisation, set v132, Mesa): the AIR → NIR translation of a pipeline is done without the lock; only panfrost compilation holds it. Flashed (BOOT `a9bff31f…`), but **never loaded** (plug-in `B2LMetalv131` remained, see build-b1l-0268.md).
- **0269** (set v133): GPU reserved for backboardd as before 0185 (boot-args `b2l_gpu_icons=1` / `b2l_gpu_all=1` to reopen); icons go back through drawing without Apple's GPU. First flash (BOOT `74a1cf8e…`) had no effect: the v131 plug-in from 0267 remained loaded (name `B2LMetalvNNN` not changed, nor 0268); name fixed, SYSTEM image decluttered (old sets removed), re-flashed (BOOT `276d422e…`): only backboardd has the GPU (verified in the log); panfrost compilation = 59.5 s, including one of 49.2 s in backboardd; visual result pending.

## B1L-0285 – B1L-0335 (2026-11-18 – 2026-11-22): GPU for the whole system, memory and Liquid Glass

Build-by-build reports (French): `docs/journal/build-b1l-0285.md` … `build-b1l-0335.md`; plan tracking: `docs/journal/suivi-plan-gpu.md`.

**Where it stands (driver set v162, build 0335):** backboardd renders the UI on the Mali at about 16–37 fps (peak 59.8 fps in 0291). Liquid Glass still renders too light (white) on light backgrounds. Some flicker remains, most likely from buffer allocation stalls. Safari renders without the GPU.

- **Shader cache (0288–0290):** panfrost's disk cache in a single file (`MESA_DISK_CACHE_SINGLE_FILE=1`): lookups 6.7 s → 98 ms; shader start-up 6.7 → 3.5 s.
- **GPU for every process (0291–0294):** `WAIT_BO` on a reclaimed buffer answers "done" as on Linux. All processes but the home-screen renderers get the Mali (`b2l_gpu_icons=1`, `b2l_gpu_all=1`). `com.apple.WebKit.GPU` was taken off the GPU again in 0294: Safari pages were white.
- **Kernel paths (0293–0323):**
  - GPU buffers are mapped with `kIOMapPrefault` (page faults ÷ 300).
  - Two contiguous display surfaces are reserved at boot (no CPU blit).
  - Purge follows memorystatus, and skips buffers that are still mapped.
  - `powergate_latency` fixed (UFS waits ÷ 45).
  - Samsung's thermal thresholds; the A55 cores are fixed at 1456 MHz.
  - The shared GPU address range is split by size (small below 1 GB, large above). It fragments less: "no memory" 127 → 13.
- **Mesa buffer cache (0299, 0305, 0306, 0335):**
  - Buffers under 256 KB skip `madvise`.
  - The cache is capped at 160 MB.
  - Cached buffers keep their CPU mapping (fps ~12 → ~19.5).
  - 0335: in the ≥4 MB bucket a busy buffer is skipped instead of ending the search, and nothing over twice the asked size is taken. In 0333, backboardd allocated 114 large buffers (1.9 GB) every 10 s.
- **Metal driver (0308–0334):**
  - 16-byte-aligned constant buffers are bound by GPU address instead of copied.
  - Memoryless textures and render targets are recycled (pool of 128 / 320 MB, oldest out first). On an allocation refusal, the pool is handed back and the creation retried.
  - Tile memory (0329: drop memoryless/DontCare attachments after their pass) caused yellow corners. Panfrost keeps passes on one framebuffer in a single batch, so the discard also erased earlier stored content. It was narrowed in 0333, then switched off in 0334.
- **Liquid Glass (0325–0332):**
  - The glass shaders' blend state, uniforms (`GlassBackgroundUniforms`) and the luma kernels (`compute_average_luma`, `compute_sum_luma`) were checked on the GPU and look correct. CoreAnimation's own colour matrix lightens light backdrops.
  - GPU family: profile 1 (Apple1+Apple2) is the default. Profile 4 (Apple3) makes CoreAnimation ask for the private XR pixel format 550, which the driver does not have, and backboardd aborts.
  - QuartzCore 23G90 (`CA::OGL::new_metal_context`) keeps `supportsFamily:` answers in a limits word. The Apple3 bit is read only by `four_cc_to_mtl_format` ('b3a8' → 550), so for CoreAnimation Apple3 means nothing but XR.

## B1L-0336 to 0353

- **White glass (0336–0339):** with a dark wallpaper Liquid Glass is right everywhere, so the light wallpaper caused it, not the driver. Apple's wallpapers stay black: 0338's remaining refusal is CollectionsPoster's `VTMTSComputeFunction1x1` (threadgroup memory).
- **Geometry Dash (0339–0346):** installed by the kernel, PAC emulation extended to arm64 apps, EAGL bridge injected. In 0345, backboardd keeps portrait targets for landscape apps. 0346 fixes a kernel panic caused by a misplaced `noreturn` in patch 0108.
- **Stability (0347–0349):** shared kernel range about 2.6 → 6 GB (patch 0112), no copy for a last layer that covers the screen, a refused transfer buffer no longer kills the process. 0349: no refused buffer (1229 in 0346), no backboardd crash.
- **Speed (0350–0352):** GPU-copy present, and IOSurface caches cleaned only after CPU use (median cleaning in the interface 153 → 5 ms per 10 s). The game went from 30 to 60 fps made and 58 shown in simple scenes, 29–32 fps in busy ones: see `overview.md`, section 9.
- **Photos (0352–0353):**
  - 0352 adds `air.is_uniform`, `llvm.assume`, a general bitcast, ASTC LDR sampling and zeros for an unbound constant buffer. Its log has no abort and no ASTC refusal, but the thumbnails stayed invisible.
  - 0353 loads flat 16-bit varyings in 32 bits, as Bifrost before v9 requires (thumbnail opacity read as 0, deduced from the compiler; its assertion reproduced on the Mac). Flashed; no test log read.
- **Paused after 0353. Remaining:** CPU/GPU split of busy game frames, Liquid Glass flicker, Apple's wallpapers, Photos editing to confirm, ASTC rendering never seen, WebKit's GPU process on the Mali, hardware video decoding (Exynos MFC, not started).
