# Bluetooth: status as of 2026-11-21 (works)

## Current state (builds 0270-0284)
- Owner's test on build 0282: the toggle works, nearby devices are listed, headphones connect and play A2DP sound.
- Chain: kernel control `com.b2l.bluetooth` relays H4 to the UART; BlueTool is replaced by an injected dylib (v4); Broadcom vendor commands the BCM4375 refuses get local answers (0xfe3b: 255 bytes); AMFI interposing bit for BlueTool and bluetoothd; Skywalk user-pipe privilege (12000) granted to bluetoothd only (`b2l_sandbox.c`, 0282).
- bluetoothd is re-enabled in launchd's `disabled.plist` by `b2l_bt_seed.h` (boot-arg `b2l_bt_launchd`).
- Still to do: check that pairings survive a reboot, 115200 baud may limit A2DP quality, headset microphone, clean power-down.

---

## History

The owner asked to stop there for Bluetooth, without a build. We will come back to it later.

### What works (on the phone, build 0213)
- **Step 1 (0211)**: the Bluetooth part of the BCM4375 answers HCI on the UART_BT (uart@10840000), at 115200 bauds with RTS/CTS. HCI_Reset OK; ROM: hci=9, lmp=9, manufacturer=0xf (Broadcom), lmp_subver=0x1111, name "BCM4375B1".
- **Step 2 (0212)**: firmware `bt.hcd` (bcm4375B1_semco.hcd, 176 645 B, 1261 records) loaded in 17 s, everything accepted. After launch: HCI revision 0 → 0xad, name "BCM4375B1 Beyond E43 Ant0 [Baseline: 0173]". The test wrongly wrote "not confirmed" (lmp_subver does not change on this chip).
- At boot, the kernel loads the firmware then **powers the chip down again** (BT_EN=0); the UART_BT mux is set back to 0x10.
- Wi-Fi was never disturbed.

### What is installed (in 0213)
- `iokit/Kernel/arm/B2LBluetooth.cpp` (step 2), entries in `iokit/conf/files` and `iokit/KernelConfigTables.cpp`.
- `bsd/kern/b2l_userspace_seed.h`: `b2l_bt_fetch()` after `b2l_wifi_fetch()`.
- SYSTEM: `/b2l-wifi/bt.hcd` (copy of `~/Developer/s10-port/device-s10/wifi-bt-fw/system/bt.hcd`, outside git).
- Boot-args: `b2l_bt=0` (nothing), `1` (read-only), `2` (without firmware), default = step 2.

### Step 3 prepared, never flashed (hooking into iOS)
- Parked in `~/Developer/s10-port/bluetooth-step3-parked/`: `B2LBluetooth.cpp` (sha eab8b5a2…), `b2l_exec_bluetooth.h`, `b2l_bt_seed.h`, `b2l_bluetool_payload.h` and `bluetooth-step3-shared.patch` (for `b2l_userspace_seed.h`, `b2l_exec_graphics_diag.h` and `b2l_amfi_constraint.h`). To apply it: copy the files into `xnu-overlay/` at the same paths, then run `patch -p0` from `beyond2lte-ios/`.
- Principle: bluetoothd 23G90 opens the controller through `BT_HCI_TRANSPORT=H4:POSIX:com.apple.uart.sk.bluetooth,115200`, a kernel control (PF_SYSTEM). The kernel keeps the chip powered on and relays UART ↔ client. BlueTool is replaced by an injected dylib (`/usr/local/lib/b2l-bluetool-v1.dylib`, published by the seed, allowed by the AMFI dyld policy). Synthetic address written by Write_BD_ADDR.
- Host sources: `userspace/b2l-bluetool.c`, `scripts/build-bluetool-dylib.py`. Detailed plan: `docs/bluetooth/plan.md` §6.
- Noted risks: BlueTool's sandbox; bluetoothd crash at power-down (+0x60 hole, seen by ios-pipa); 115200 bauds too slow for A2DP; pairings not yet persistent.

### Build 0214 interrupted
- Build 0214 (step 3) was stopped at the owner's request, during the Mac-side hashing of the SYSTEM stage, before any write. Neither SYSTEM nor BOOT was written: the phone is still on 0213.
- `xnu/` and `xnu-overlay/` are reset to the 0213 state for Bluetooth (copies in `~/Developer/s10-port/bak-0214/`). The next build will reuse number 0214 or the following one.

### To resume
1. Put step 3 back in place, see above.
2. Build, then check in the log: "THE FIRMWARE RUNS", "THE RADIO IS UP for iOS", "H4: client connected", "TOOL: RunBuiltinScript boot -> 0", "iOS sends HCI …".
3. In Settings: the Bluetooth toggle, then a device search.

### Update 2026-11-17 (0270 flashed, booted into 0271)
The kernel does everything up to the H4 point. bluetoothd does not start, because it is disabled in the `disabled.plist` of
launchd on the clean USERDATA (0250). Fix ready in `xnu-overlay/bsd/kern/b2l_bt_seed.h` (rename
`com.apple.bluetoothd` → `com.apple.bluetoothX`), waiting for the USERDATA go-ahead. Details: `build-b1l-0270.md`.


### Build 0272 (2026-11-17): bluetoothd launched, but crashes at startup
- The seed did rename the key: bluetoothd is launched, with our variables (`Searching for transport: "H4"`, `driver: "POSIX"`).
- It crashes in a loop (17 times, "Too many corpses"): EXC_BAD_ACCESS at address 0x31c, at 0x1004040bc on the main thread (reports `bluetoothd-*.ips` in USERDATA, os_log: `Unknown platform 0xffffffff detected`).
- Cause: `MGGetProductType()` returns 0xffffffff, because MobileGestalt does not know this phone's model. bluetoothd's platform table (0x1004312a0, 88 calls to `MGIsDeviceOneOfType`) recognizes nothing, the platform object stays null, and bluetoothd reads its field +0x31c (the platform number, used in more than 400 places).
- Fix (next common build, `b2l-bluetool-v2.dylib`): the dylib is also inserted into bluetoothd. It answers "yes" to `MGIsDeviceOneOfType` at the only call site that creates the chosen platform, and lets all other calls through. Platform 0x16 by default (the most common Broadcom family); boot-arg `b2l_bt_platform=0x13|0x14|0x17|0x18|0x19`, `0` = nothing. Guard: UUID of bluetoothd 23G90 (DA7ADF00-…). AMFI also allows bluetoothd's pid (`b2l_btd_dyld_pid`).
- Not yet handled: possible crash when Bluetooth is turned off (+0x60 hole in the H4 table, seen by ios-pipa), Broadcom commands refused by the BCM4375 (the kernel logs them: `refused by the chip`), 115200 bauds for A2DP.
