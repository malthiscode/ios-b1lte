# Battery

**Status: works (B1L-0355).** The status bar and Settings → Battery show the real percentage, and the charging bolt follows the cable. Seen on the phone by the owner.

## How it works
- `xnu-overlay/iokit/Kernel/arm/B2LBattery.h`, started from `B2LCPU.cpp` 20 s after the CPUs, never on the boot path. Kill switch: `b2l_batt=0`.
- The MAX77705 is read over I2C by GPIO on its own bus pins (hsi2c@15D60000: SCL gpm12-0, SDA gpm13-0, pinctrl 0x15C30000). The USI/HSI2C controller, its clocks and its timings are not touched.
- **Read only.** The only transaction the code can send is START, address+W, a register from a fixed list, repeated START, address+R, read, STOP. No data byte is ever sent. Fuel gauge 0x36 (STATUS, RepCap, RepSOC, VCELL, Current, AvgCurrent, FullCapNom, FullCapRep), charger 0x69 (CHG_INT_OK, DETAILS_00/01), PMIC 0x66 (ID, REV). Clear-on-read interrupt registers are never read.
- Each released line must read high before use, and SCL is re-read right after the repeated START's edge. If a line is held or SCL is stuck for more than 1 ms, both lines are released without another clock and the driver stops until the next boot.
- Every 15 s the values go to iOS through `B2LBatterySource : IOPMPowerSource`, attached to the platform and registered for powerd (same approach as Jesvi-iOS `RedwoodBattery.cpp`, APSL 2.0). Only sane readings are published: a failed read never becomes 0 %.
- Published: whole percent (rounded down, as Android shows it), voltage, average current, cable (CHGIN_OK or WCIN_OK), charging (charger state 0–2), full (3–4), gauge mAh, design capacity 3400 mAh (phone's DTB). No temperature yet.

## Measured (B1L-0354 log against TWRP)
96.85 % from the gauge against 96 % under TWRP; cable plug/unplug seen; one read round takes about 14.7 ms.

## Known issues
- **No charging under iOS.** About 30 s after boot the charger reports state 0x0B: its watchdog expired (Samsung `max77705_chg_get_wdtmr_status`), because nothing kicks it under iOS. iOS correctly shows "not charging". Kicking it means writing to the charger, which is not done.
- Battery temperature is not published yet (next step, from the ADC).
- If the driver stops after a bus fault, iOS keeps the last value.

## Tests
`tests/test-battery-i2c.py` compiles the driver's own I2C core against a simulated MAX77705: all registers read correctly; NACK at every byte; a line held low at every moment (388 260 cases) never causes a register write when held 5 µs or more; the values given to iOS are checked against real readings from the log.

Facts (registers, pins, charger states) come from the phone's DTB and LineageOS's Exynos 9820 kernel `a412cfaf` (GPL: facts only, no code copied).
