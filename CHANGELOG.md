# Changelog

Format: [Keep a Changelog](https://keepachangelog.com/). Build-by-build history: [docs/journal](docs/journal).

## [Unreleased]

### Added
- Battery in iOS (B1L-0354..0355, `B2LBattery.h`): read-only MAX77705 fuel gauge and charger over GPIO I2C, published as an `IOPMPowerSource`; status in `docs/battery/status.md`.
- OpenGL ES for apps (B1L-0342..0352, `userspace/b2l_eagl_app.m`): Geometry Dash playable on the Mali, up to 60 fps made and 58 shown, about 30 fps in busy scenes.
- Kernel installer for one staged app (B1L-0339..0340, `b2l_app_install.h`, `scripts/stage-app.py`) and PAC emulation for arm64 apps (B1L-0341).
- Metal (B1L-0337..0352): wallpaper processes on the Mali, sampling without a usage flag, R8Unorm on 'L008', `air.is_uniform`, `llvm.assume`, general bitcasts, ASTC LDR sampling, zeros for unbound constant buffers.
- Patches 0110..0112 (HFS purgeable fsctl from B1L-0254/0255, zone map 24 → 16 GB of address space). Like 0100..0109, `scripts/prepare-xnu.sh` does not apply them.
- GPU (B1L-0285..0335): Mali for every process except the home-screen renderers, single-file shader cache, kernel buffer mapping/purge/address-range work, Mesa buffer cache tuning, Metal texture recycling, Liquid Glass diagnostics, QuartzCore GPU-family analysis; status in `docs/gpu/metal-progress.md`.
- Boot menu icons in the XnuLoader (B1L-0324, `scripts/make-menu-icons.py`, `uefi/XnuLoader/MenuIcons.h`).
- Audio through iOS (B1L-0274..0279): `b2l_audio_seed.h`, a product-id shown to audiomxd only, D94AP root model, real-time floor lowered to 40 us (`osfmk/kern/sched_rt.c`), larger audio ring.
- Bluetooth through iOS (B1L-0270..0282): `b2l_bt_seed.h`, `b2l_exec_bluetooth.h`, BlueTool stand-in payload, `com.b2l.bluetooth` control, vendor HCI replies, Skywalk user pipe for A2DP headphones.
- Networking (B1L-0283..0284): Safari without JIT (`patches/0109-b2l-webkit-no-jit.patch`), synthetic ChipID/ECID for the Apple identity daemons only, fairplayd re-enabled for Weather; status in `docs/wifi/status.md`.
- First public layout of the Galaxy S10 port, imported from `beyond2lte-ios` (branch `b1l`, build B1L-0273).
- `tools/b1l` (build/flash loop), `tools/flash-kit`, `userspace/audio-plugin`.
- `docs/architecture.md`, `docs/installation.md`, `docs/troubleshooting.md`, `THIRD_PARTY_NOTICES.md`, `.env.example`.

### Fixed
- Patch 0108 put `noreturn` on the EL0 cache maintenance function and panicked the kernel (B1L-0346).
- Alternating stale frames for landscape apps (B1L-0345), exhausted shared kernel range and the backboardd crashes that followed (B1L-0347..0349).
- Frame cost: GPU-copy present (B1L-0350), IOSurface caches cleaned only after CPU use (B1L-0351).
- Flat 16-bit varyings read wrongly on Bifrost before v9 (B1L-0353, flashed, no test log read).

### Changed
- The published SpringBoard payload no longer embeds a wallpaper picture.
- Build reports moved to `docs/journal/` (kept in French); status and plan documents translated to English under `docs/gpu`, `docs/audio`, `docs/bluetooth`, `docs/plans` and `docs/project`.
- Scripts read `S10_PORT`, `B2L_ROOT`, `ADB`, `B2L_SERIAL` and `PY_ENV` instead of fixed paths.

### Removed
- The phone's serial number, the home network name and personal paths from scripts and documents.
