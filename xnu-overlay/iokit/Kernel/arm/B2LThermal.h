/* SPDX-License-Identifier: BSD-2-Clause
 * B1L-0028 (T1): Exynos 9820 TMU readings and the CPU thermal throttle rule.
 * Plain C: compiled into the kernel (B2LCPU.cpp) and into tests/test_thermal.c.
 *
 * B1L-0030: the Exynos 9820's own map (Samsung exynos_tmu.c, CONFIG_SOC_EXYNOS9820, LineageOS
 * android_kernel_samsung_exynos9820 lineage-22.2): TRIMINFO(p) = p*4 for every sensor, CONTROL at 0x50,
 * current codes at 0x84 + (s/2)*4, shift (s%2)*16, 9 bits; calibration bits as below. The 9810 map that
 * B1L-0028/0029 used read the wrong registers (85 C constant). On the 9820 the TMU belongs to the ACPM
 * firmware (CONFIG_EXYNOS_ACPM_THERMAL): nothing is ever written to it here.
 * Facts (not code) from Samsung's Exynos TMU driver, as restated by HakiOS
 * (Scion-Project/hackios-xnu-kernel drivers/power/hakios.exynos_tmu.h, the
 * Exynos 9810 map; the 9820's TMU is the same family: the S10 DTB names G3D
 * "samsung,exynos9810-tmu", same sensor type 8 TEM1051X, efuse 55, trims 25/85):
 *   - TRIMINFO(s): s*4 for s <= 7, (s-8)*4 + 0x400 above; error1 [8:0] (code at
 *     25 C), error2 [17:9] (code at 85 C), two-point if TRIMINFO(0) bit 23;
 *     error1 = 55 when the fuse is empty.
 *   - current codes, 9 bits: sensors 0-1 at 0x40, 2-4 at 0x44, 5-7 at 0x48,
 *     8-9 at 0x440, 10-12 at 0x444.
 *   - CONTROL 0x20 bit0 CORE_EN, bit12 THERM_TRIP_EN (never set here).
 * S10 DTB (exynos9820-base.dts): TOP block 0x10090000 (0x800): BIG sensors
 * 0x7c0, MID 0x20, LITTLE 0x10; SUB block 0x100A0000: G3D 0x21e.
 */
#ifndef B2L_THERMAL_H
#define B2L_THERMAL_H
#include <stdint.h>
#include <stdbool.h>

#define B2L_TMU_TOP_PA   0x10090000ULL
#define B2L_TMU_SUB_PA   0x100A0000ULL
#define B2L_TMU_SIZE     0x800u
#define B2L_TMU_CONTROL  0x50u          /* 9820 */
#define B2L_TMU_CONTROL1 0x24u
#define B2L_TMU_AVG_CON  0x38u
#define B2L_TMU_COUNTER0 0x30u
#define B2L_TMU_COUNTER1 0x34u
#define B2L_TMU_TRIM0    0x3Cu
#define B2L_TMU_CORE_EN  (1u << 0)
#define B2L_TMU_TRIP_EN  (1u << 12)
#define B2L_TMU_SENSE    0x028Au

/* Exynos 9820 (exynos_tmu.c CONFIG_SOC_EXYNOS9820 and all_temp_show) */
static inline uint32_t b2l_tmu_triminfo(unsigned s) { return s * 4u; }
static inline uint32_t b2l_tmu_temp_reg(unsigned s) { return 0x84u + (s / 2) * 4u; }
static inline unsigned b2l_tmu_temp_shift(unsigned s) { return (s % 2) * 16u; }

/* Code to millidegrees with the sensor's own fuse (one- or two-point). */
static inline int32_t b2l_tmu_code_to_mc(uint32_t code, uint32_t triminfo, uint32_t triminfo0)
{
	int32_t e1 = (int32_t)(triminfo & 0x1ff), e2 = (int32_t)((triminfo >> 9) & 0x1ff);
	if (e1 == 0) e1 = 55;
	if (((triminfo0 >> 23) & 1) && e2 > e1)
		return (int32_t)(((int64_t)((int32_t)code - e1) * 60 * 1000) / (e2 - e1)) + 25000;
	return ((int32_t)code - e1 + 25) * 1000;
}
static inline bool b2l_tmu_reading_ok(uint32_t code, int32_t mc)
{
	return code != 0 && code != 0x1ff && mc >= 10000 && mc <= 120000;
}

enum { B2L_ZONE_BIG = 0, B2L_ZONE_MID = 1, B2L_ZONE_LITTLE = 2, B2L_ZONE_G3D = 3, B2L_NZONES = 4 };
static const struct { const char *name; uint8_t sub; uint16_t sensors; } b2l_zones[B2L_NZONES] = {
	{ "big", 0, 0x7c0 }, { "mid", 0, 0x020 }, { "little", 0, 0x010 }, { "g3d", 1, 0x21e },
};

/*
 * One step of the throttle for one cluster, once a second. Levels index the
 * firmware's fvmap table (0 = fastest); base = the boot's DVFS target level,
 * floor = slowest level allowed. At or above crit: floor at once. At or above
 * hi: one level slower, unless the last step already turned the heat (B1L-0319: mc
 * below prev_mc; the sensor lags the cut, and cutting every second while it fell took
 * the M4 from 2080 to 936 MHz at 83-84 C in 0316-0318). At or below lo: one level
 * faster, never past base.
 */
static inline uint32_t b2l_thr_step(uint32_t lv, uint32_t base, uint32_t floor,
    int32_t mc, int32_t prev_mc, int32_t hi_mc, int32_t lo_mc, int32_t crit_mc)
{
	if (mc >= crit_mc) return floor;
	if (mc >= hi_mc) return (lv < floor && mc >= prev_mc) ? lv + 1 : (lv < floor ? lv : floor);
	if (mc <= lo_mc && lv > base) return lv - 1;
	return lv;
}
#endif
