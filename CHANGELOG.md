# Changelog

Format: [Keep a Changelog](https://keepachangelog.com/). Build-by-build history: [docs/journal](docs/journal).

## [Unreleased]

### Added
- Audio through iOS (B1L-0274..0279): `b2l_audio_seed.h`, a product-id shown to audiomxd only, D94AP root model, real-time floor lowered to 40 us (`osfmk/kern/sched_rt.c`), larger audio ring.
- Bluetooth through iOS (B1L-0270..0282): `b2l_bt_seed.h`, `b2l_exec_bluetooth.h`, BlueTool stand-in payload, `com.b2l.bluetooth` control, vendor HCI replies, Skywalk user pipe for A2DP headphones.
- Networking (B1L-0283..0284): Safari without JIT (`patches/0109-b2l-webkit-no-jit.patch`), synthetic ChipID/ECID for the Apple identity daemons only, fairplayd re-enabled for Weather; status in `docs/wifi/status.md`.
- First public layout of the Galaxy S10 port, imported from `beyond2lte-ios` (branch `b1l`, build B1L-0273).
- `tools/b1l` (build/flash loop), `tools/flash-kit`, `userspace/audio-plugin`.
- `docs/architecture.md`, `docs/installation.md`, `docs/troubleshooting.md`, `THIRD_PARTY_NOTICES.md`, `.env.example`.

### Changed
- Build reports moved to `docs/journal/` (kept in French); status and plan documents translated to English under `docs/gpu`, `docs/audio`, `docs/bluetooth`, `docs/plans` and `docs/project`.
- Scripts read `S10_PORT`, `B2L_ROOT`, `ADB`, `B2L_SERIAL` and `PY_ENV` instead of fixed paths.

### Removed
- The phone's serial number, the home network name and personal paths from scripts and documents.
