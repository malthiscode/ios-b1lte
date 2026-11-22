# Third-party notices

This repository contains or derives from the third-party works below. Each file keeps the notice it was received with; this list tells where they are and under which terms.

## Apple Public Source License 2.0 (APSL 2.0)

- **Apple XNU** (xnu-12377), https://github.com/apple-oss-distributions/xnu — `patches/` and the Apple-copyrighted files in `xnu-overlay/`. Some XNU files also carry Carnegie Mellon University (Mach) and University of California (BSD) notices, kept as found.
- **Apple hfs** (hfs-715.120.4) and **libpthread** (539.100.4) — staged by `scripts/stage-hfs.py` and `scripts/stage-libpthread.py`.

APSL 2.0 terms that apply here: Apple's notices stay in each file; modified files are published as source; the license text is at https://opensource.apple.com/apsl/. TODO: add the full text as `LICENSE.APSL` and a modification notice ("modified by …, date") to each changed Apple file, as section 2.2(c) requires.

## MIT License

- **HakiOS** — Copyright (c) 2025-2026 the HakiOS authors, https://github.com/oosexgang/hakios-xnu-kernel. Ported kernel drivers, `userspace/metal/LICENSE.hakios-metal`.
- **Scion / HakiOS Metal driver** — `userspace/metal/scion/` (commit `a6f4bc38`), license in `userspace/metal/scion/LICENSE.scion-hakios`. TODO: upstream link.
- **fiio-jm21-xnu AudioServerPlugIn** — https://github.com/soumarcelino/fiio-jm21-xnu, `userspace/audio-plugin/` (license in `userspace/audio-plugin/LICENSE`).
- **ios-pipa** — https://github.com/nishant713/ios-pipa (commit `830ec1c`), settings reproduced in `scripts/prepare-ios-root.py`; WPA code from `lx_wpa.c`/`lx_wpa.h` copied into `xnu-overlay/bsd/dev/b2l_wpa.c` and `bsd/sys/b2l_wpa.h`.
- **hakios-metal AIR translator** — Copyright the HakiOS authors; the B1L-0352 blocks of `userspace/mesa/air-b2l/apply.py` (`air.is_uniform`, `llvm.assume`, bitcast) come from it, license in `userspace/metal/LICENSE.hakios-metal`.
- **Jesvi-iOS** — https://github.com/jesvi-eulah/Jesvi-iOS (commit `1b41d5b`, `userland/src`); the IOSurface presentation ring of `userspace/b2l_eagl_app.m` follows its idea, written anew, no code copied.
- **Mesa 3D** 25.0.7 and **libdrm** — interfaces reproduced in `userspace/mesa/`.
- **BearSSL** — https://www.bearssl.org, staged by `scripts/stage-bearssl.py`.

## BSD-2-Clause

- **Spleen** font — Copyright (c) 2018-2026 Frederic Cambus, https://github.com/fcambus/spleen, in `uefi/XnuLoader/FontSpleen16x32.h` and `FontSpleen32x64.h`.

## BSD-2-Clause-Patent

- **Project Mu / Mu-Silicium** — https://github.com/Project-Silicium/Mu-Silicium; two files in `uefi/` carry this identifier.

## Used as a reference only (no code copied, to be confirmed)

- **LineageOS android_kernel_samsung_exynos9820** (GPL-2.0) — register layouts, PSCI, RTC, DPU sequences were read there and re-implemented. GPL code must not be copied into APSL/BSD/MIT files; TODO: confirm for `xnu-overlay/bsd/dev/b2l_mali_core.c`, `B2LFramebuffer.cpp` and `B2LThermal.h`.
- **ios-a5xelte** (https://github.com/wardevsa/ios-a5xelte) — SpringBoard injection idea. TODO: license.

## Not redistributed

iOS IPSW and dyld cache, Xcode, the iOS SDK and the Kernel Debug Kit (Apple proprietary), Samsung and Broadcom firmware, NVRAM and calibration data.
