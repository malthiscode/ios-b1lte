# Display brightness: status

**Works (owner-checked on B1L-0373):** iOS's brightness slider (Control Center and Settings) changes the panel's brightness over its whole range, with a fade. The level is set again at boot.

## How it works
1. **iOS side.** `AppleARMBacklight`, attached under `AppleCLCD` (`B2LFramebuffer.cpp`), carries `backlight-control` and publishes three things CoreBrightness and BrightnessControl read:
   - `IODisplayParameters` (`brightness`, `BrightnessMilliNits` 1..1060 nits, `BrightnessMicroAmps`);
   - the d94ap `backlight-marketing-table`;
   - `backlight-calibration-parameters`.

   Without the table and the calibration, backboardd crashes in CoreBrightness (B1L-0361..0369). Putting `backlight-control` on `AppleCLCD` itself makes iOS crawl (B1L-0363..0364).
2. **Requests.** `setProperties` receives `brightness-nits` (16.16 nits) and only records it. It never touches the panel. `DisplayBrightnessNitsNVRAM`, sent at boot, is used until the first live request.
3. **Panel thread** (`B2LPanel.h`). It sends DCS commands through DSIM0 (0x19080000), left in command mode by S-Boot:
   - one path, under a lock, outside the swap path, every wait bounded (50 ms per packet, 100 ms per read);
   - closed list of commands: level-2 key (`F0`), AOR (`B1`, only within Samsung's normal table 0x0014..0x0BC9) and gamma update (`F7 03`);
   - each AOR write is read back, and the first anomaly turns the path off;
   - the bootloader's AOR is read first; if it cannot be read, nothing is written.
4. **Mapping.** 1..1060 nits are mapped on a log scale to emitted lines 47..3044 (vtotal 3064), that is AOR 0x0BC9 (floor, never black) to 0x0014 (top normal level). There is no HBM, no `0x51` and no MTP/flash write. Each step covers a quarter of the remaining distance, every 16 ms.

Panel facts are from LineageOS `android_kernel_samsung_exynos9820` (dpu20/cal_9820, panel/s6e3ha9): facts only, no code copied.

## Measured (B1L-0372)
- Panel ID `a1 00 13`, power mode `0x9F`.
- AOR left by S-Boot: `0x0440`.
- DSIM CONFIG `0x04887cff` (command mode, DSC on).
- On the 9820, `LINK_STATUS3` reads 0 even with the panel lit; the PLL lock is in the PHY. That false check stopped B1L-0371.

## Not done
- **Absolute luminance.** The bootloader's gamma is kept, so the nits iOS asks for are not calibrated.
- **Auto-brightness** (no ambient light sensor driver).
- **Screen off when locked.** This is a separate task; it must share `b2l_dsi_lock`.

Boot-args: `b2l_backlight=0` (no service), `b2l_panel=0` (no DCS), `b2l_panel_test=1` (B1L-0371's 3 s test).
Host test: `tests/test-panel-dcs.cpp`.
