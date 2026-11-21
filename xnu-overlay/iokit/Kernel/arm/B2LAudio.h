/* SPDX-License-Identifier: BSD-2-Clause
 * B2L audio, called once from B2LCPU's coordinator thread right after b2l_gpu_probe().
 *
 * Stage A0 (B1L-0210): read-only audit of what S-Boot leaves for the speaker path.
 * Stage A1 (B1L-0211): ask both CS35L40 amplifiers who they are (DEVID/REVID, I2C
 * reads only), then put every pin touched back exactly as it was found.
 *
 * Speaker path on beyond1lte (live DTB): ABOX UAIF1 (format dsp_a, bit clock
 * inverted) feeds both Cirrus CS35L40 amplifiers on hsi2c@109A0000 (USI17_I2C,
 * PERIC1): 0x40 = cs35l40-bot (speaker), 0x41 = cs35l40-rcv (earpiece).  Their
 * resets are gph1-1 / gph1-2 (active low), the bus pins gph0-6 (SCL) / gph0-7
 * (SDA), I2C function 3, no internal pull (pull-ups on the board).  The headphone
 * codec CS47L93 sits on spi@10470000, reset gpg1-0 (PERIC0), IRQ on the ALIVE gpa2
 * bank.  ABOX itself is inside pd-aud (PMU 0x15861900).
 *
 * A0 read order, a clock-gated SFR may stall the bus: PMU and CMU_CMU (always on)
 * first; CMU_PERIC1 only if CMU_CMU's PERIC1 bus gate is open; the PERIC1 pins and
 * the I2C controller only if their own gate and Q-channel are open.  Nothing inside
 * pd-aud (ABOX, CMU_AUD, AUD pinctrl) is read.
 *
 * A1 (0210 found: gph0-6/7 and gph1-1/2 in function 0 = input, level 0; the USI
 * of USI17_I2C in reset, usi_con=1).  So the controller is left alone and I2C is
 * bit-banged on the two pins, as ios-pipa does for its CS35L41: a line is released
 * by making it an input (the board pulls it up) and pulled low by making it an
 * output whose data latch is 0.  If either line is not high once released, nothing
 * is driven.  Reset sequence of Samsung's cs35l41-core.c: reset low 1 ms, high,
 * 2 ms, then DEVID (0x0, expect 0x35a40) and REVID (0x4); registers and values are
 * 32 bits, big endian (cs35l41-i2c.c).  The amplifiers are never enabled (no write
 * over I2C at all).  At the end gph1 then gph0 get their start CON/DAT bits back,
 * so the reset lines float low again as S-Boot left them.
 *
 * 0211: both answered, DEVID 0x35a40 REVID 0xa0, pins restored.
 *
 * Stage A2 (B1L-0212): read-only audit of the ABOX side, now that 0211 showed
 * pd-aud on and CMU_CMU's AUD_CPU gate open: CMU_AUD (PLL_AUD0, the clocks of
 * ABOX, UAIF1 and GPIO_AUD), then -- each only if its own gate and Q-channel are
 * open -- the ABOX system/route/SPUS registers, UAIF0..3 and RDMA0
 * (abox_soc.h, ABOX v2), and the AUD pin banks gpb0..2 (UAIF1 = gpb1-0..3,
 * function 2, DTB aud-i2s1-bus).  No write anywhere.
 *
 * Addresses: cal-if/exynos9820/cmucal-sfr.c (CMU), pinctrl-exynos-arm64.c
 * (bank offsets; CON 0x00, DAT 0x04, PUD 0x08, DRV 0x0c), i2c-exynos5.c (HSI2C).
 * Gates: bit 20 manual, bit 21 value, bit 28 automatic.  QCH: bit 0 hardware
 * Q-channel, bit 1 software clock request (same reading as B2LWifi.cpp).
 *
 * A2 only reads the ABOX registers when PLL_AUD0 is enabled and locked (its
 * output feeds the ABOX bus divider); it now runs before A1 and hands A3/A4 the
 * PLL rate and the mapped CMU_AUD / ABOX / AUD pin windows.
 *
 * Grouped batch (owner's OK, B1L-0213), each stage only if the previous one worked:
 * Stage A3: with both amplifiers out of reset (A1), program them as Samsung's
 * cs35l41.c does for this board, every write as read-before / write / read-back,
 * logged: rev A0 errata, OTP boot done, boost peak 4100 mA (DTB), ASP dsp_a slave
 * with the bit clock inverted (IB_NF), 32-bit slots, RX1 slot 1 for the right
 * channel amplifier (bot) and 0 for rcv, 48 kHz, PLL from SCLK 6.144 MHz (DTB
 * cirrus,ampclk, table value 0x28), PCM volume muted, and PWR_CTRL2 amplifier
 * and boost off: nothing reaches the speaker before A5.
 * Stage A4: UAIF1 clock out of the ABOX (6.144 MHz = 48 kHz x 4 slots x 32 bits):
 * PMU ABOX pad retention released (0x1920 bit 11, abox.c for 9820), CMU_AUD
 * DIV_UAIF1 from DIV_AUDIF (only if 6.144 MHz divides it within 0.1 %), MUX_UAIF1
 * on the divider, UAIF1 CTRL1/CTRL0 as abox_if.c (dsp_a, IB_NF, master, 4 x 32),
 * gpb1-0..3 in function 2, the UAIF1 bit-clock Q-channel disabled (set_tristate
 * 0), SPK_ENABLE.  Then GLOBAL_EN on both amplifiers with their power-up patch:
 * PUP_DONE proves the amplifier found its clocks.  Then everything is undone in
 * reverse (power-down patch, PDN_DONE, ABOX/CMU/pins back to their start values)
 * and the amplifiers go back into reset with the pins.
 *
 * Batch 2 (first beep): A3 also copies the factory trims from the amplifier's OTP
 * into its registers (cs35l41_otp_unpack, right after OTP_BOOT_DONE as in probe).
 * Stage A5 plays a 1 kHz tone on the loudspeaker amplifier (bot@40) only.  Under
 * Android the Calliope firmware drives the RDMA; its accessors (vendor
 * calliope_dram.bin) give the fields, and here the AP writes them itself, the ABOX
 * SysMMU being left as reset left it (never enabled: bypass), so the RDMA reads a
 * physical address:
 *  - ABOX SYSPOWER_CTRL bit 0 (DRAM request, wait for STATUS), SBANK RDMA0 512 B
 *    at 0 and mixer 512 B at 0x3000 (firmware values), SPUS_CTRL0 channel 0 =
 *    RDMA -> SIFS0 without ASRC, MIXP format 32 bit x 4, SIFS0 counter 0 (paced by
 *    UAIF1), ROUTE_CTRL0 UAIF1 SPK = SIFS0 (abox_soc.h, ABOX 2.0.0 layout);
 *  - RDMA0: VOL_FACTOR 0x800000 (unity), BUF_STR/BUF_END/STR_POINT on a 65280 byte
 *    ring (85 ms, 4 slots x 32 bit, tone in slot 1 = right = bot, -20 dBFS),
 *    BUF_OFFSET = one 4080-byte period, CTRL0 FORMAT 0x1b, bit 5, BURST 8, ENABLE;
 *    the tone goes on only if the RDMA read pointer moves;
 *  - bot@40 before GLOBAL_EN, as DAPM would: DAC source ASPRX1, CLASS H on, output
 *    unmuted, AMP_EN; the boost stays off (VBST from the battery through the
 *    inductor, as the earpiece path runs).  After PUP_DONE: PCM volume -10 dB with
 *    the soft ramp for 700 ms, IRQ1 error bits polled every 100 ms (any error: mute
 *    at once), mute, power down.  rcv@41 never gets AMP_EN or a volume.
 * The RDMA is stopped before UAIF1, everything A5 writes in the ABOX goes back to
 * its start value (SYSPOWER last), and the ring is freed only once the RDMA is idle.
 *
 * Batch 3 (A6, iOS): after a clean silent check the path stays up -- UAIF1 clocking, RDMA0 looping over a silent
 * 85 ms ring, both amplifiers configured, powered down, out of reset -- and a kernel thread serves the frames
 * iOS writes to /dev/hakios-audio (HakiOS's AudioServerPlugIn, published by bsd/kern/b2l_audio_seed.h); see A6.
 *
 * Boot-arg b2l_audio: 0 off, 1 A0 only (read only), 2 A0 + A1, 3 A0 + A1 + A2,
 * 4 + A3, 5 + A4, 6 + A5 audible beep (all undone after it), 7 (default) + A6 iOS: the A5 path with a silent
 * ring and no volume (no boot beep, B1L-0245), kept up if the RDMA moves and bot@40 powers up cleanly.
 * Boot-arg b2l_audio_max: the speaker's ceiling, eighths of a dB under 0 dB (default 48, at least 24).
 * Log lines go through b2l_diag_record (one reserved range per line, never mixed
 * with another CPU's output, unlike b2l_early_trace's byte-by-byte writes).
 */
#ifndef B2L_AUDIO_H
#define B2L_AUDIO_H

#define B2L_AUD_PMU_PA          0x15860000ULL
#define B2L_AUD_PMU_PD_AUD      0x1900u    /* pd-aud: +0 configuration, +4 status */
#define B2L_AUD_PMU_CORE0       0x2d80u    /* abox-core@18c55000 samsung,pmu_power */
#define B2L_AUD_PMU_CORE1       0x2e00u    /* abox-core@18c55080 */
#define B2L_AUD_CMU_TOP_PA      0x1a240000ULL
#define B2L_AUD_TOP_AUD_CPU     0x201cu    /* GATE_CLKCMU_AUD_CPU */
#define B2L_AUD_TOP_PERIC1_BUS  0x20c4u    /* GATE_CLKCMU_PERIC1_BUS */
#define B2L_AUD_TOP_PERIC1_IP   0x20c8u    /* GATE_CLKCMU_PERIC1_IP */
#define B2L_AUD_CMU_PERIC1_PA   0x10800000ULL
#define B2L_AUD_P1_MUX_I2C      0x02e0u    /* PLL_CON0_MUX_CLKCMU_PERIC1_USI_I2C_USER */
#define B2L_AUD_P1_DIV_I2C      0x1838u    /* DIV_CLK_PERIC1_USI_I2C */
#define B2L_AUD_P1_GPIO_PCLK    0x2030u
#define B2L_AUD_P1_I2C_IPCLK    0x2110u    /* USI17_I2C */
#define B2L_AUD_P1_I2C_PCLK     0x2114u
#define B2L_AUD_P1_GPIO_QCH     0x3010u
#define B2L_AUD_P1_I2C_QCH      0x3074u
#define B2L_AUD_PERIC1_PIN_PA   0x10830000ULL
#define B2L_AUD_GPH0            0x0e0u
#define B2L_AUD_GPH1            0x100u
#define B2L_AUD_PERIC0_PIN_PA   0x10430000ULL
#define B2L_AUD_GPG1            0x0a0u
#define B2L_AUD_ALIVE_PIN_PA    0x15850000ULL
#define B2L_AUD_GPA2            0x040u
#define B2L_AUD_AMP_I2C_PA      0x109a0000ULL
#define B2L_AUD_SCL             6u         /* gph0 */
#define B2L_AUD_SDA             7u         /* gph0 */
#define B2L_AUD_RST_BOT         1u         /* gph1, amplifier 0x40 */
#define B2L_AUD_RST_RCV         2u         /* gph1, amplifier 0x41 */
#define B2L_AUD_CS35L40_ID      0x35a40u
#define B2L_AUD_CMU_AUD_PA      0x18c00000ULL  /* inside pd-aud */
#define B2L_AUD_A_MUX_CPU_USER  0x0100u
#define B2L_AUD_A_PLL_AUD0      0x0120u    /* CON0: ENABLE 31, STABLE 29, M 25:16, P 13:8, S 2:0 */
#define B2L_AUD_A_PLL_AUD0_K    0x012cu    /* CON3: K 15:0, signed */
#define B2L_AUD_A_MUX_UAIF1     0x1010u    /* select 0 = DIV_CLK_AUD_UAIF1, 1 = CLKIO */
#define B2L_AUD_A_DIV_AUDIF     0x1800u    /* ratio 8:0, parent PLL_AUD0 */
#define B2L_AUD_A_DIV_BUS       0x1804u
#define B2L_AUD_A_DIV_UAIF1     0x1830u
#define B2L_AUD_A_ABOX_ACLK     0x2010u
#define B2L_AUD_A_BCLK_UAIF1    0x2024u
#define B2L_AUD_A_GPIO_PCLK     0x205cu
#define B2L_AUD_A_SYSREG_PCLK   0x20b8u
#define B2L_AUD_A_QCH_ACLK      0x3020u
#define B2L_AUD_A_QCH_BCLK1     0x3028u
#define B2L_AUD_A_QCH_GPIO      0x304cu
#define B2L_AUD_ABOX_SFR_PA     0x18c50000ULL
#define B2L_AUD_AUD_PIN_PA      0x18c60000ULL  /* gpb0 0x00, gpb1 0x20, gpb2 0x40 */
#define B2L_AUD_PMU_ABOX_PAD    0x1920u    /* ABOX_PAD_NORMAL, mask 0x800 on 9820 */
#define B2L_AUD_X_QCH_DISABLE   0x0038u    /* bit 3 = BCLK_UAIF1 */
#define B2L_AUD_X_UAIF1_CTRL0   0x0510u    /* SPK_ENABLE 0, MODE 2 (1 = master) */
#define B2L_AUD_X_UAIF1_CTRL1   0x0514u
#define B2L_AUD_X_UAIF1_STATUS  0x051cu
/* abox_if.c for dsp_a IB_NF, 32 bits, 4 channels: FORMAT abox_get_format(32,4)=0x1b,
 * BCLK_POLARITY 0, WS_MODE 1, WS_POLAR 0, SLOT_MAX 3, SBIT_MAX 31, VALID_STR/END 31. */
#define B2L_AUD_UAIF1_CTRL1     0x1b4df7dfu
#define B2L_AUD_BCLK_HZ         6144000u
/* CS35L41 registers (cs35l41_user.h) */
#define B2L_CS_TEST_KEY         0x00040u
#define B2L_CS_PWR_CTRL1        0x02014u   /* GLOBAL_EN bit 0 */
#define B2L_CS_PLL_CLK_CTRL     0x02c04u
#define B2L_CS_IRQ1_STATUS1     0x10010u   /* PUP_DONE bit 24, PDN_DONE bit 23 */
#define B2L_CS_IRQ1_STATUS4     0x1001cu   /* OTP_BOOT_DONE bit 1 */
#define B2L_CS_ERR_BITS         0x800281c0u /* AMP_SHORT, TEMP_ERR, BST_SHORT, BST_DCM_UVP, BST_OVP */
#define B2L_CS_VOL              0x06000u   /* AMP_DIG_VOL_CTRL: PCM volume 13:3 (1/8 dB, 0x4cf mute), ramp 2:0 */
#define B2L_CS_VOL_BEEP         0x7b0u     /* -10 dB */
#define B2L_AUD_X_RDMA0         0x1000u    /* CTRL0; STATUS at +0x30: PROGRESS 31, RBUF_OFFSET 27:16 */
/* 8160 frames x 4 slots x 32 bit (170 ms, 170 periods of the 1 kHz beep), in 32 RDMA periods of 4080 bytes:
 * RBUF_CNT (11:0) counts the bytes of a period, so a period must stay under 4096 bytes (abox_rdma_pointer).
 * B1L-0279: 4080 -> 8160 frames; with 4080 the HAL's late cycles and catch-up bursts at boot lost a third
 * of the first sound (0278: in=107520 lost=37220 resyncs=22, "saccadé au début"). */
#define B2L_AUD_RING_BYTES      130560u
#define B2L_AUD_RING_FRAMES     (B2L_AUD_RING_BYTES / 16)
#define B2L_AUD_PERIOD_BYTES    4080u

#ifndef B2L_AUDIO_HOST_TEST
#include <arm64/b2l_diag.h>
#include <IOKit/IOBufferMemoryDescriptor.h>
#include <IOKit/IOLocks.h>
#include <IOKit/IOLib.h>
extern "C" vm_offset_t ml_io_map(vm_offset_t, vm_size_t);
extern "C" void FlushPoC_DcacheRegion(vm_offset_t va, size_t length);
/* A5 ring: physically contiguous, page aligned, below 4 GB (RDMA addresses are 32 bit). */
static void *b2l_aud_dma_alloc(size_t len, int32_t **va, uint64_t *pa)
{
    IOBufferMemoryDescriptor *m = IOBufferMemoryDescriptor::inTaskWithPhysicalMask(kernel_task,
        kIODirectionInOut | kIOMemoryPhysicallyContiguous, len, 0x00000000fffff000ULL);
    if (!m) return nullptr;
    IOByteCount seg = 0;
    *pa = m->getPhysicalSegment(0, &seg, kIOMemoryMapperNone);
    *va = (int32_t *)m->getBytesNoCopy();
    if (!*va || !*pa || seg < len || (*pa & 0xfff) || *pa + len > 0x100000000ULL) { m->release(); return nullptr; }
    return m;
}
static void b2l_aud_dma_free(void *m) { ((IOBufferMemoryDescriptor *)m)->release(); }
static void b2l_aud_dma_clean(int32_t *va, size_t len) { FlushPoC_DcacheRegion((vm_offset_t)va, len); }
#endif

#ifndef B2L_AUDIO_HOST_TEST   /* the host test supplies both with a simulated I2C bus */
static inline uint32_t b2l_aud_rd(volatile uint8_t *b, uint32_t off) { return *(volatile uint32_t *)(b + off); }
static inline void b2l_aud_wr(volatile uint8_t *b, uint32_t off, uint32_t v)
{
    *(volatile uint32_t *)(b + off) = v;
    __asm__ volatile("dsb sy" ::: "memory");
}
#endif
/* Closed only when software forced the gate shut (manual, value 0). */
static inline bool b2l_aud_gate_open(uint32_t g) { return !((g & (1u << 20)) && !(g & (1u << 21))); }
static inline bool b2l_aud_qch_open(uint32_t q) { return (q & 3u) != 0; }
static inline unsigned b2l_aud_pin_fn(uint32_t con, unsigned pin) { return (con >> (pin * 4)) & 0xfu; }

/* Bank: CON, DAT, PUD, DRV.  Returns CON and DAT for the verdict. */
static void b2l_aud_bank(volatile uint8_t *pin, uint32_t off, const char *name, uint32_t *con, uint32_t *dat)
{
    *con = b2l_aud_rd(pin, off);
    *dat = b2l_aud_rd(pin, off + 0x4);
    b2l_diag_record("B2L AUDIO: %s con=%08x dat=%02x pud=%08x drv=%08x\n",
        name, *con, *dat, b2l_aud_rd(pin, off + 0x8), b2l_aud_rd(pin, off + 0xc));
}

/* ---- A1: bit-banged I2C on gph0 (open drain: input = released, output 0 = low) ---- */
struct b2l_aud_bus { volatile uint8_t *p1; bool stuck; };

static void b2l_aud_pin_mode(volatile uint8_t *p1, uint32_t bank, unsigned pin, unsigned fn)
{
    uint32_t con = b2l_aud_rd(p1, bank);
    b2l_aud_wr(p1, bank, (con & ~(0xfu << (pin * 4))) | (fn << (pin * 4)));
}
static void b2l_aud_pin_dat(volatile uint8_t *p1, uint32_t bank, unsigned pin, unsigned v)
{
    uint32_t dat = b2l_aud_rd(p1, bank + 4);
    b2l_aud_wr(p1, bank + 4, v ? (dat | (1u << pin)) : (dat & ~(1u << pin)));
}
static inline unsigned b2l_aud_pin_get(volatile uint8_t *p1, unsigned pin) { return (b2l_aud_rd(p1, B2L_AUD_GPH0 + 4) >> pin) & 1u; }
static inline void b2l_aud_low(struct b2l_aud_bus *b, unsigned pin) { b2l_aud_pin_mode(b->p1, B2L_AUD_GPH0, pin, 1); IODelay(5); }
static inline void b2l_aud_sda_rel(struct b2l_aud_bus *b) { b2l_aud_pin_mode(b->p1, B2L_AUD_GPH0, B2L_AUD_SDA, 0); IODelay(5); }
/* Release SCL and wait for it (a slave may stretch the clock), at most 1 ms. */
static void b2l_aud_scl_rel(struct b2l_aud_bus *b)
{
    b2l_aud_pin_mode(b->p1, B2L_AUD_GPH0, B2L_AUD_SCL, 0);
    for (unsigned i = 0; i < 1000 && !b2l_aud_pin_get(b->p1, B2L_AUD_SCL); ++i) IODelay(1);
    if (!b2l_aud_pin_get(b->p1, B2L_AUD_SCL)) b->stuck = true;
    IODelay(5);
}
static void b2l_aud_start(struct b2l_aud_bus *b)
{
    b2l_aud_sda_rel(b); b2l_aud_scl_rel(b);
    b2l_aud_low(b, B2L_AUD_SDA); b2l_aud_low(b, B2L_AUD_SCL);
}
static void b2l_aud_stop(struct b2l_aud_bus *b)
{
    b2l_aud_low(b, B2L_AUD_SDA); b2l_aud_scl_rel(b); b2l_aud_sda_rel(b);
}
/* Returns true on ACK. */
static bool b2l_aud_put(struct b2l_aud_bus *b, uint8_t v)
{
    for (int i = 7; i >= 0; --i) {
        if ((v >> i) & 1) b2l_aud_sda_rel(b); else b2l_aud_low(b, B2L_AUD_SDA);
        b2l_aud_scl_rel(b); b2l_aud_low(b, B2L_AUD_SCL);
    }
    b2l_aud_sda_rel(b); b2l_aud_scl_rel(b);
    bool ack = !b2l_aud_pin_get(b->p1, B2L_AUD_SDA);
    b2l_aud_low(b, B2L_AUD_SCL);
    return ack;
}
static uint8_t b2l_aud_get(struct b2l_aud_bus *b, bool ack)
{
    uint8_t v = 0;
    b2l_aud_sda_rel(b);
    for (int i = 0; i < 8; ++i) {
        b2l_aud_scl_rel(b);
        v = (uint8_t)((v << 1) | b2l_aud_pin_get(b->p1, B2L_AUD_SDA));
        b2l_aud_low(b, B2L_AUD_SCL);
    }
    if (ack) b2l_aud_low(b, B2L_AUD_SDA);
    b2l_aud_scl_rel(b); b2l_aud_low(b, B2L_AUD_SCL);
    b2l_aud_sda_rel(b);
    return v;
}
/* 32-bit register read.  0 = done, else the step that was not acknowledged:
 * 1 address+write, 2..5 register bytes, 6 address+read, 7 clock stuck. */
static unsigned b2l_aud_read32(struct b2l_aud_bus *b, uint8_t addr, uint32_t reg, uint32_t *out)
{
    unsigned step = 0;
    b2l_aud_start(b);
    if (!b2l_aud_put(b, (uint8_t)(addr << 1))) step = 1;
    for (unsigned i = 0; !step && i < 4; ++i)
        if (!b2l_aud_put(b, (uint8_t)(reg >> (24 - 8 * i)))) step = 2 + i;
    if (!step) {
        b2l_aud_start(b);    /* repeated start */
        if (!b2l_aud_put(b, (uint8_t)((addr << 1) | 1))) step = 6;
    }
    if (!step) {
        uint32_t v = 0;
        for (unsigned i = 0; i < 4; ++i) v = (v << 8) | b2l_aud_get(b, i < 3);
        *out = v;
    }
    b2l_aud_stop(b);
    return b->stuck ? 7 : step;
}

/* 32-bit register write.  0 = done, 1 address, 2..5 register bytes, 10..13 data bytes, 7 clock stuck. */
static unsigned b2l_aud_write32(struct b2l_aud_bus *b, uint8_t addr, uint32_t reg, uint32_t v)
{
    unsigned step = 0;
    b2l_aud_start(b);
    if (!b2l_aud_put(b, (uint8_t)(addr << 1))) step = 1;
    for (unsigned i = 0; !step && i < 8; ++i)
        if (!b2l_aud_put(b, (uint8_t)((i < 4 ? reg : v) >> (24 - 8 * (i & 3))))) step = i < 4 ? 2 + i : 6 + i;
    b2l_aud_stop(b);
    return b->stuck ? 7 : step;
}

/* One log line built piece by piece (b2l_diag_record takes at most 640 bytes). */
struct b2l_aud_txt { char s[600]; unsigned n; };
static void b2l_aud_ts(struct b2l_aud_txt *t, const char *s)
{
    while (*s && t->n + 2 < sizeof(t->s)) t->s[t->n++] = *s++;
    t->s[t->n] = 0;
}
static void b2l_aud_tx(struct b2l_aud_txt *t, uint32_t v, unsigned digits)
{
    char h[9];
    for (unsigned i = 0; i < digits; ++i) h[i] = "0123456789abcdef"[(v >> (4 * (digits - 1 - i))) & 0xfu];
    h[digits] = 0;
    b2l_aud_ts(t, h);
}
/* Read before, write (before & ~mask) | val, read back; logs " reg:before>after".
 * 0 = done and the masked bits hold val, 1 = written but read back different, 2 = I2C error. */
static unsigned b2l_aud_upd(struct b2l_aud_bus *b, uint8_t addr, uint32_t reg, uint32_t mask, uint32_t val, struct b2l_aud_txt *t)
{
    uint32_t before = 0, after = 0;
    unsigned e = b2l_aud_read32(b, addr, reg, &before);
    if (!e) e = b2l_aud_write32(b, addr, reg, (before & ~mask) | val);
    if (!e) e = b2l_aud_read32(b, addr, reg, &after);
    if (t) {
        b2l_aud_ts(t, " "); b2l_aud_tx(t, reg, 5); b2l_aud_ts(t, ":");
        if (e) { b2l_aud_ts(t, "e="); b2l_aud_tx(t, e, 2); }
        else { b2l_aud_tx(t, before, 8); b2l_aud_ts(t, ">"); b2l_aud_tx(t, after, 8); }
    }
    return e ? 2 : (after & mask) != val;
}
static bool b2l_aud_seq(struct b2l_aud_bus *b, uint8_t addr, const uint32_t (*s)[2], unsigned n)
{
    for (unsigned i = 0; i < n; ++i) if (b2l_aud_write32(b, addr, s[i][0], s[i][1])) return false;
    return true;
}
/* Poll a status bit every 1 ms, at most ms_max; returns the milliseconds waited or ms_max + 1. */
static unsigned b2l_aud_poll(struct b2l_aud_bus *b, uint8_t addr, uint32_t reg, uint32_t bit, unsigned ms_max, uint32_t *v)
{
    for (unsigned ms = 0; ms <= ms_max; ++ms) {
        if (b2l_aud_read32(b, addr, reg, v)) return ms_max + 1;
        if (*v & bit) return ms;
        IODelay(1000);
    }
    return ms_max + 1;
}

static const char *const b2l_aud_name[2] = { "bot@40", "rcv@41" };

/* cs35l41.c, rev A0 errata (test registers between the 0x40 key writes). */
static const uint32_t b2l_aud_errata[7][2] = {
    { 0x03854, 0x05180240 }, { 0x07418, 0x9091a1c8 } /* OTP_TRIM_30 */, { 0x03014, 0x0200ee0e },
    { 0x0381c, 0x00000051 } /* BSTCVRT_DCM_CTRL */, { 0x00054, 0x00000004 },
    { 0x10318, 0x00000000 } /* IRQ1_DB3 */, { 0x10b18, 0x00000000 } /* IRQ2_DB3 */,
};
static const uint32_t b2l_aud_unlock[2][2] = { { B2L_CS_TEST_KEY, 0x5555 }, { B2L_CS_TEST_KEY, 0xaaaa } };
static const uint32_t b2l_aud_lock[2][2] = { { B2L_CS_TEST_KEY, 0xcccc }, { B2L_CS_TEST_KEY, 0x3333 } };
/* cs35l41_pup_patch / cs35l41_pdn_patch around GLOBAL_EN. */
static const uint32_t b2l_aud_pup[5][2] = { { 0x40, 0x55 }, { 0x40, 0xaa }, { 0x2084, 0x002f1aa0 }, { 0x40, 0xcc }, { 0x40, 0x33 } };
static const uint32_t b2l_aud_pdn[5][2] = { { 0x40, 0x55 }, { 0x40, 0xaa }, { 0x2084, 0x002f1aa3 }, { 0x40, 0xcc }, { 0x40, 0x33 } };

/* cs35l41-tables.c otp_map_2 (OTPID 2 and 3); otp_map_1 differs only by entry 93, which it skips.
 * {register, shift, size}; the packed fields start at OTP word 2, bit 16. */
struct b2l_aud_otp { uint32_t reg; uint8_t shift, size; };
static const struct b2l_aud_otp b2l_aud_otp_map[99] = {
    {0x02030,0,4}, {0x02030,7,1}, {0x0208c,24,6}, {0x02090,14,4}, {0x02090,10,4}, {0x0300c,11,4},
    {0x0394c,23,2}, {0x03950,0,7}, {0x03950,8,7}, {0x03950,16,8}, {0x03950,24,8}, {0x03954,0,7},
    {0x03954,8,7}, {0x03954,16,8}, {0x03954,24,8}, {0x03958,0,7}, {0x03958,8,7}, {0x03958,16,8},
    {0x03958,24,8}, {0x0395c,0,7}, {0x0395c,8,7}, {0x0395c,16,8}, {0x0395c,24,8}, {0x0416c,0,8},
    {0x04160,0,7}, {0x0416c,8,8}, {0x04160,16,10}, {0x0416c,16,12}, {0x0416c,28,1}, {0x04170,0,6},
    {0x04170,6,1}, {0x04170,8,6}, {0x04170,14,1}, {0x04170,16,9}, {0x04360,0,5}, {0x04360,6,9},
    {0x04448,0,8}, {0x04448,8,8}, {0x04448,16,8}, {0x04448,24,8}, {0x0444c,0,3}, {0x06e30,0,5},
    {0x06e30,8,5}, {0x06e30,16,5}, {0x06e30,24,5}, {0x06e34,0,5}, {0x06e34,8,5}, {0x06e34,16,5},
    {0x06e34,24,5}, {0x06e38,0,5}, {0x06e38,8,5}, {0x06e38,16,5}, {0x06e38,24,5}, {0x06e3c,0,5},
    {0x06e3c,8,5}, {0x06e3c,16,5}, {0x06e3c,24,5}, {0x06e40,0,5}, {0x06e40,8,5}, {0x06e40,16,5},
    {0x06e40,24,5}, {0x06e44,0,5}, {0x06e48,0,10}, {0x06e48,10,10}, {0x06e48,20,10}, {0x06e4c,0,10},
    {0x06e4c,10,10}, {0x06e4c,20,10}, {0x06e50,0,10}, {0x06e50,10,10}, {0x06e50,20,10}, {0x06e54,0,10},
    {0x06e54,10,10}, {0x06e54,20,10}, {0x06e58,0,10}, {0x06e58,10,10}, {0x06e58,20,10}, {0x06e5c,0,10},
    {0x06e5c,10,10}, {0x06e5c,20,10}, {0x06e60,0,10}, {0x06e60,10,10}, {0x06e60,20,10}, {0x06e64,0,10},
    {0x07418,7,5}, {0x0741c,0,5}, {0x0741c,11,4}, {0x0741c,19,4}, {0x07434,17,1}, {0x07434,18,7},
    {0x07068,0,9}, {0x0410c,7,1}, {0x0400c,0,7}, {0x04000,11,1}, {0x17040,0,8}, {0x17040,8,8},
    {0x17040,16,8}, {0x17040,24,8}, {0x17044,0,24},
};

/* cs35l41_otp_unpack: factory trims, read-modify-write of each field (values never logged:
 * 0x17040/0x17044 hold the die's lot and wafer data).  0 ok, 1 bad header, 2 unknown
 * OTPID, 3 I2C error. */
static unsigned b2l_audio_amp_otp(struct b2l_aud_bus *b, uint8_t addr, uint32_t *otpid, unsigned *writes)
{
    uint32_t mem[32];
    *writes = 0;
    if (!b2l_aud_seq(b, addr, b2l_aud_unlock, 2)) return 3;
    unsigned r = b2l_aud_read32(b, addr, 0x10, otpid) ? 3 : 0;
    for (unsigned i = 0; !r && i < 32; ++i) if (b2l_aud_read32(b, addr, 0x400 + 4 * i, &mem[i])) r = 3;
    if (!r && ((mem[1] & 0xfffffffeu) != 0x1500 || (mem[2] & 0xffffu) != 0x300)) r = 1;
    if (!r && (*otpid < 1 || *otpid > 3)) r = 2;
    for (unsigned i = 0, w = 2, bit = 16; !r && i < 99; ++i) {
        const struct b2l_aud_otp e = b2l_aud_otp_map[i];
        const uint64_t pair = mem[w] | (w < 31 ? (uint64_t)mem[w + 1] << 32 : 0);
        const uint32_t val = (uint32_t)(pair >> bit) & ((1u << e.size) - 1);
        bit += e.size; w += bit / 32; bit %= 32;
        if (i == 93 && *otpid == 1) continue;
        const uint32_t mask = ((1u << e.size) - 1) << e.shift;
        uint32_t old = 0;
        if (b2l_aud_read32(b, addr, e.reg, &old)) { r = 3; break; }
        const uint32_t nv = (old & ~mask) | (val << e.shift);
        if (nv == old) continue;
        if (b2l_aud_write32(b, addr, e.reg, nv)) { r = 3; break; }
        ++*writes;
    }
    if (!b2l_aud_seq(b, addr, b2l_aud_lock, 2)) r = 3;
    return r;
}

/* ---- A3: amplifier set-up, output stage and boost kept off, volume muted ---- */
static bool b2l_audio_amp_prep(struct b2l_aud_bus *b, unsigned a, uint32_t rev, bool *otp_ok)
{
    const uint8_t addr = (uint8_t)(0x40 + a);
    struct b2l_aud_txt t = { {0}, 0 };
    bool io = true;
    unsigned off = 0;

    b2l_aud_ts(&t, "B2L AUDIO: amp-errata "); b2l_aud_ts(&t, b2l_aud_name[a]);
    if (rev == 0xa0) {
        io = b2l_aud_seq(b, addr, b2l_aud_unlock, 2);
        for (unsigned i = 0; io && i < 7; ++i) {
            unsigned r = b2l_aud_upd(b, addr, b2l_aud_errata[i][0], 0xffffffffu, b2l_aud_errata[i][1], &t);
            if (r == 2) io = false; else off += r;   /* a test register may read back differently */
        }
        io = b2l_aud_seq(b, addr, b2l_aud_lock, 2) && io;
        b2l_aud_ts(&t, off ? " readback-differs=" : " readback-same"); if (off) b2l_aud_tx(&t, off, 1);
    } else b2l_aud_ts(&t, " not-rev-a0 skipped");
    b2l_aud_ts(&t, io ? " i2c=ok\n" : " i2c=FAILED\n");
    b2l_diag_record("%s", t.s);
    if (!io) return false;

    uint32_t s4 = 0;
    const unsigned boot_ms = b2l_aud_poll(b, addr, B2L_CS_IRQ1_STATUS4, 0x2, 100, &s4);
    *otp_ok = false;
    if (boot_ms <= 100) {
        uint32_t otpid = 0;
        unsigned writes = 0;
        const unsigned r = b2l_audio_amp_otp(b, addr, &otpid, &writes);
        static const char *const res[4] = { "ok", "bad-header", "unknown-id", "i2c-error" };
        b2l_diag_record("B2L AUDIO: amp-otp %s otpid=%x result=%s writes=%u\n", b2l_aud_name[a], otpid, res[r], writes);
        if (r == 3) return false;
        *otp_ok = !r;
    }

    /* reg, mask, value: cs35l41.c probe/set_sysclk/set_dai_fmt/hw_params for this board */
    const uint32_t prep[9][3] = {
        { 0x03808, 0x7f, 0x42 },                  /* BSTCVRT_PEAK_CUR: (4100 - 1600) / 50 + 0x10 */
        { 0x0480c, 0x03, 0x03 },                  /* SP_HIZ_CTRL: cirrus,asp-sdout-hiz = 3 */
        { 0x04808, 0xffff0755u, 0x20200040u },    /* SP_FORMAT: slave, dsp_a, SCLK_INV, RX/TX width 32 */
        { 0x04820, 0x3f3f, a ? 0x0100u : 0x0001u },/* FRAME_RX_SLOT: right channel (bot) RX1 = slot 1 */
        { 0x02c0c, 0x1f, 0x03 },                  /* GLOBAL_CLK_CTRL: GLOBAL_FS 48 kHz */
        { 0x04804, 0x3f, 0x28 },                  /* SP_RATE_CTRL: SCLK 6.144 MHz */
        { 0x02018, 0x31, 0x00 },                  /* PWR_CTRL2: AMP_EN and BST_EN off */
        { 0x06000, 0x7ffu << 3, 0x4cfu << 3 },    /* AMP_DIG_VOL_CTRL: PCM volume = mute */
        { 0x04800, 1u << 16, 1u << 16 },          /* SP_ENABLES: ASPRX1 */
    };
    t.n = 0; t.s[0] = 0;
    b2l_aud_ts(&t, "B2L AUDIO: amp-prep "); b2l_aud_ts(&t, b2l_aud_name[a]);
    b2l_aud_ts(&t, boot_ms <= 100 ? " otp_boot=1" : " otp_boot=0");
    unsigned bad = boot_ms <= 100 ? 0 : 1;
    for (unsigned i = 0; !(bad & 0x100) && i < 9; ++i) {
        unsigned r = b2l_aud_upd(b, addr, prep[i][0], prep[i][1], prep[i][2], &t);
        bad |= r == 2 ? 0x100 : r;
    }
    /* set_sysclk order: open loop, REFCLK_FREQ, disable, source SCLK, closed loop, enable. */
    uint32_t pll0 = 0, pll1 = 0;
    if (!(bad & 0x100) && !b2l_aud_read32(b, addr, B2L_CS_PLL_CLK_CTRL, &pll0)) {
        const uint32_t steps[6][2] = { { 0x800, 0x800 }, { 0x7e0, 0x28u << 5 }, { 0x10, 0 }, { 0x07, 0 }, { 0x800, 0 }, { 0x10, 0x10 } };
        for (unsigned i = 0; !(bad & 0x100) && i < 6; ++i)
            if (b2l_aud_upd(b, addr, B2L_CS_PLL_CLK_CTRL, steps[i][0], steps[i][1], nullptr) == 2) bad |= 0x100;
        if (!(bad & 0x100) && b2l_aud_read32(b, addr, B2L_CS_PLL_CLK_CTRL, &pll1)) bad |= 0x100;
        if (!(bad & 0x100) && (pll1 & 0xfff) != ((0x28u << 5) | 0x10)) bad |= 1;
        b2l_aud_ts(&t, " 02c04:"); b2l_aud_tx(&t, pll0, 8); b2l_aud_ts(&t, ">"); b2l_aud_tx(&t, pll1, 8);
    } else bad |= 0x100;
    b2l_aud_ts(&t, bad ? ((bad & 0x100) ? " | i2c-error\n" : " | readback-mismatch\n") : " | ready\n");
    b2l_diag_record("%s", t.s);
    return !bad;
}

struct b2l_aud_abox { volatile uint8_t *cmu, *abox, *pins; uint64_t pll_hz; };

/* ---- A5: RDMA0 -> SPUS -> SIFS0 -> UAIF1, registers in the order the firmware writes them ---- */
static const uint32_t b2l_aud_path_reg[13] = {
    0x0010 /* SYSPOWER_CTRL */, 0x0220 /* SBANK_RDMA0 */, 0x0280 /* SBANK_MIXP */, 0x0200 /* SPUS_CTRL0 */,
    0x0204 /* SPUS_CTRL1 */, 0x0290 /* SIFS0_CNT */, 0x0040 /* ROUTE_CTRL0 */, 0x1018 /* RDMA0 VOL_FACTOR */,
    0x1008 /* BUF_STR */, 0x100c /* BUF_END */, 0x1014 /* STR_POINT */, 0x1010 /* BUF_OFFSET */, 0x1000 /* CTRL0 */,
};
/* 1 kHz at 48 kHz, -20 dBFS of a 32-bit sample */
static const int32_t b2l_aud_sine[48] = {
    0, 28030286, 55580967, 82180641, 107374182, 130730521, 151850025, 170371332, 185977539, 198401619,
    207430992, 212911163, 214748365, 212911163, 207430992, 198401619, 185977539, 170371332, 151850025,
    130730521, 107374182, 82180641, 55580967, 28030286, 0, -28030286, -55580967, -82180641, -107374182,
    -130730521, -151850025, -170371332, -185977539, -198401619, -207430992, -212911163, -214748365,
    -212911163, -207430992, -198401619, -185977539, -170371332, -151850025, -130730521, -107374182,
    -82180641, -55580967, -28030286,
};
struct b2l_aud_path { void *mem; int32_t *va; uint64_t pa; uint32_t save[13], st0, st1; bool on, moving; };

static uint32_t b2l_aud_path_val(unsigned i, uint32_t v, uint32_t pa)
{
    switch (i) {
    case 0: return v | 1u;                                          /* DRAM request */
    case 1: return (v & ~0x3ff03ff0u) | (0x20u << 20);              /* SBANK 512 B at 0 */
    case 2: return (v & ~0x3ff03ff0u) | (0x20u << 20) | 0x3000u;    /* SBANK 512 B at 0x3000 */
    case 3: return (v & ~0xfu) | 0x2u;                              /* ch 0: in RDMA, out SIFS0, no ASRC */
    case 4: return (v & ~0x1fu) | 0x1bu;                            /* MIXP 32 bit x 4 */
    case 5: return v & ~0xffffffu;                                  /* SIFS0 paced by UAIF1 */
    case 6: return (v & ~0xf0u) | 0x10u;                            /* UAIF1 SPK = SIFS0 */
    case 7: return (v & 0xff000000u) | 0x800000u;                   /* unity */
    case 8: case 10: return (v & 0xfu) | pa;
    case 9: return (v & 0xfu) | (pa + B2L_AUD_RING_BYTES);
    case 11: return (v & ~0xfff0u) | B2L_AUD_PERIOD_BYTES;
    default: return (v & ~0x007f3f21u) | (0x1bu << 8) | (1u << 5) | (8u << 19);  /* CTRL0: FORMAT, b5, BURST 8; b4 kept */
    }
}

/* Path up and RDMA0 started; true only if its read pointer moves.  On false the caller still
 * calls b2l_aud_path_off, which undoes whatever was written. */
static bool b2l_aud_path_on(volatile uint8_t *x, struct b2l_aud_path *p, bool tone)
{
    const uint32_t c0 = b2l_aud_rd(x, B2L_AUD_X_RDMA0), st = b2l_aud_rd(x, B2L_AUD_X_RDMA0 + 0x30);
    if ((c0 & 1u) || (st >> 31)) {
        b2l_diag_record("B2L AUDIO: beep skipped (rdma0 busy ctrl0=%08x status=%08x)\n", c0, st);
        return false;
    }
    int32_t *va = nullptr;
    p->mem = b2l_aud_dma_alloc(B2L_AUD_RING_BYTES, &va, &p->pa);
    if (!p->mem) { b2l_diag_record("B2L AUDIO: beep skipped (no contiguous ring below 4 GB)\n"); return false; }
    p->va = va;
    for (unsigned f = 0; f < B2L_AUD_RING_FRAMES; ++f) {
        va[4 * f] = 0; va[4 * f + 1] = tone ? b2l_aud_sine[f % 48] : 0; va[4 * f + 2] = 0; va[4 * f + 3] = 0;
    }
    b2l_aud_dma_clean(va, B2L_AUD_RING_BYTES);

    for (unsigned i = 0; i < 13; ++i) p->save[i] = b2l_aud_rd(x, b2l_aud_path_reg[i]);
    p->on = true;
    b2l_aud_wr(x, 0x10, b2l_aud_path_val(0, p->save[0], 0));
    unsigned us = 0;
    for (; us < 10000 && !(b2l_aud_rd(x, 0x14) & 1u); us += 10) IODelay(10);
    const bool dram = b2l_aud_rd(x, 0x14) & 1u;
    if (dram) {
        for (unsigned i = 1; i < 13; ++i) b2l_aud_wr(x, b2l_aud_path_reg[i], b2l_aud_path_val(i, p->save[i], (uint32_t)p->pa));
        b2l_aud_wr(x, B2L_AUD_X_RDMA0, b2l_aud_rd(x, B2L_AUD_X_RDMA0) | 1u);
        /* Sample the read pointer every 0.5 ms for 3 ms (0213: 80000000>80000058 after 0.5 ms). */
        p->st0 = p->st1 = b2l_aud_rd(x, B2L_AUD_X_RDMA0 + 0x30);
        for (unsigned i = 0; i < 6 && p->st1 == p->st0; ++i) { IODelay(500); p->st1 = b2l_aud_rd(x, B2L_AUD_X_RDMA0 + 0x30); }
        p->moving = p->st1 != p->st0;
    }
    struct b2l_aud_txt t = { {0}, 0 };
    b2l_aud_ts(&t, "B2L AUDIO: beep-path ring_pa="); b2l_aud_tx(&t, (uint32_t)p->pa, 8);
    b2l_aud_ts(&t, dram ? " dram=ok" : " dram=TIMEOUT");
    for (unsigned i = 0; i < 13; ++i) {
        b2l_aud_ts(&t, " "); b2l_aud_tx(&t, b2l_aud_path_reg[i], 4); b2l_aud_ts(&t, ":");
        b2l_aud_tx(&t, p->save[i], 8); b2l_aud_ts(&t, ">"); b2l_aud_tx(&t, b2l_aud_rd(x, b2l_aud_path_reg[i]), 8);
    }
    b2l_aud_ts(&t, " status="); b2l_aud_tx(&t, p->st0, 8); b2l_aud_ts(&t, ">"); b2l_aud_tx(&t, p->st1, 8);
    b2l_aud_ts(&t, p->moving ? " | rdma-moving\n" : " | rdma-NOT-moving\n");
    b2l_diag_record("%s", t.s);
    return p->moving;
}

/* RDMA0 stopped (UAIF1 still clocking it), then every path register back, SYSPOWER last.
 * Returns true if the ring could be freed. */
static bool b2l_aud_path_off(volatile uint8_t *x, struct b2l_aud_path *p, bool *restored)
{
    *restored = true;
    if (!p->mem) return true;
    bool idle = true;
    if (p->on) {
        b2l_aud_wr(x, B2L_AUD_X_RDMA0, b2l_aud_rd(x, B2L_AUD_X_RDMA0) & ~1u);
        for (unsigned i = 0; i < 100 && (b2l_aud_rd(x, B2L_AUD_X_RDMA0 + 0x30) >> 31); ++i) IODelay(100);
        idle = !(b2l_aud_rd(x, B2L_AUD_X_RDMA0 + 0x30) >> 31);
        for (unsigned i = 13; i-- > 0;) b2l_aud_wr(x, b2l_aud_path_reg[i], p->save[i]);
        for (unsigned i = 0; i < 13; ++i) *restored = *restored && b2l_aud_rd(x, b2l_aud_path_reg[i]) == p->save[i];
    }
    // shortcut: an RDMA that never goes idle keeps its 4 KB ring for good (it may still read it).
    if (idle) b2l_aud_dma_free(p->mem);
    return idle;
}

/* ---- A6: iOS's sound.  /dev/hakios-audio (bsd/kern/b2l_audio_seed.h) hands b2l_audio_sink() the frames HakiOS's
 * AudioServerPlugIn writes (s16 stereo 48 kHz); they go, mixed to mono, into slot 1 of the ring the RDMA keeps
 * reading.  A kernel thread, every 10 ms, zeroes what the RDMA has played (so nothing is ever played twice), powers
 * the speaker amplifier (bot@40) up when sound arrives and down after 3 s of silence, follows iOS's volume and
 * reads the amplifier's error bits.  Level guard: the amplifier never plays louder than b2l_audio_max eighths of a
 * dB under 0 dB (default 48 = -6 dB, at least 24), the boost stays off (battery voltage only), and after an error
 * it stays off 30 s; after 3 errors, until the next boot. ---- */
#define B2L_AUD_LEAD            2048u   /* frames written ahead of the RDMA after a (re)start: 43 ms (B1L-0279: was 21) */
#define B2L_AUD_GUARD           512u    /* frames the writer never fills just behind the RDMA */
#define B2L_AUD_LAG             256u    /* the zeroing stays this far behind the read pointer */
struct b2l_aud_svc {
    IOSimpleLock *lock;
    struct b2l_aud_bus bus;
    volatile uint8_t *x;
    int32_t *ring;
    uint32_t wpos, zpos, below, muted, max_below, faults, cool, vol_seen;
    volatile uint32_t tick, sound_tick, vol_gen;
    uint64_t in, lost, resyncs, played;
    volatile bool running;
    uint32_t wahead;   /* frames the writer was ahead of the RDMA when last seen; only shrinks until a write */
    bool wvalid, amp_on, dead;
};
static struct b2l_aud_svc b2l_aud_svc;

/* RDMA read pointer in frames, as abox_rdma_pointer() reads it on ABOX 2.0.1 (version 02000100 in 0212):
 * RBUF_OFFSET 27:12 in 16-byte units, rounded down to the period, plus RBUF_CNT 11:0 bytes.
 * B2L_AUD_RING_FRAMES when the RDMA is not in progress or the value is out of the ring. */
static uint32_t b2l_aud_rpos(volatile uint8_t *x)
{
    const uint32_t st = b2l_aud_rd(x, B2L_AUD_X_RDMA0 + 0x30), off = ((st >> 12) & 0xffffu) << 4;
    const uint32_t p = off - off % B2L_AUD_PERIOD_BYTES + (st & 0xfffu);
    return (st >> 31) && p < B2L_AUD_RING_BYTES ? p / 16 : B2L_AUD_RING_FRAMES;
}
static void b2l_aud_ring_clean(int32_t *ring, uint32_t from, uint32_t n)
{
    const uint32_t first = n < B2L_AUD_RING_FRAMES - from ? n : B2L_AUD_RING_FRAMES - from;
    if (first) b2l_aud_dma_clean(ring + 4 * from, first * 16);
    if (n > first) b2l_aud_dma_clean(ring, (n - first) * 16);
}
/* AMP_DIG_VOL_CTRL PCM volume (1/8 dB, two's complement) for iOS's setting under the guard's ceiling. */
static uint32_t b2l_aud_vol_code(const struct b2l_aud_svc *s)
{
    const uint32_t total = s->max_below + s->below;
    return s->muted || total >= 816 ? 0x4cfu : (0x800u - total) & 0x7ffu;
}

/* Called from write() on any thread; never waits.  Returns the frames taken (the rest are lost). */
extern "C" uint32_t b2l_audio_sink(const int16_t *lr, uint32_t n)
{
    struct b2l_aud_svc *s = &b2l_aud_svc;
    if (!s->running) return 0;
    IOSimpleLockLock(s->lock);
    const uint32_t N = B2L_AUD_RING_FRAMES, r = b2l_aud_rpos(s->x);
    uint32_t taken = 0;
    if (r < N) {
        uint32_t ahead = (s->wpos + N - r) % N;
        if (!s->wvalid || ahead > s->wahead || ahead < 64 || ahead > N - B2L_AUD_GUARD) {   /* first write, or the RDMA caught up: restart ahead of it */
            s->wpos = (r + B2L_AUD_LEAD) % N; ahead = B2L_AUD_LEAD; s->wvalid = true; s->resyncs++;
        }
        const uint32_t room = N - B2L_AUD_GUARD - ahead;
        taken = n < room ? n : room;
        bool loud = false;
        for (uint32_t i = 0; i < taken; ++i) {
            int32_t *f = s->ring + 4 * ((s->wpos + i) % N);
            const int32_t m = ((int32_t)lr[2 * i] + (int32_t)lr[2 * i + 1]) * 32768;   /* (L + R) / 2, 32 bit */
            f[0] = 0; f[1] = m; f[2] = 0; f[3] = 0;
            loud = loud || m;
        }
        b2l_aud_ring_clean(s->ring, s->wpos, taken);
        s->wpos = (s->wpos + taken) % N; s->wahead = ahead + taken;
        if (loud) s->sound_tick = s->tick;
    }
    s->in += n; s->lost += n - taken;
    IOSimpleLockUnlock(s->lock);
    return taken;
}
/* HAKIOS_AUDIO_SET_VOLUME: eighths of a dB under the loudest, and muted. */
extern "C" void b2l_audio_volume_set(uint32_t below, uint32_t muted)
{
    struct b2l_aud_svc *s = &b2l_aud_svc;
    s->below = below > 816 ? 816 : below; s->muted = muted != 0; s->vol_gen++;
}

/* bot@40: DAPM order (PCM source ASP, CLASS H, output unmuted, AMP_EN, boost off), power-up patch, GLOBAL_EN,
 * PUP_DONE, then the volume with the soft ramp.  0 = playing, else the step that failed. */
static unsigned b2l_aud_amp_up(struct b2l_aud_svc *s)
{
    struct b2l_aud_bus *b = &s->bus;
    uint32_t v = 0;
    const uint32_t dapm[4][3] = { { 0x04c00, 0x7f, 0x08 }, { 0x0201c, 0x10, 0x10 }, { 0x02024, 0x10, 0 }, { 0x02018, 0x31, 0x01 } };
    for (unsigned i = 0; i < 4; ++i) if (b2l_aud_upd(b, 0x40, dapm[i][0], dapm[i][1], dapm[i][2], nullptr)) return 1;
    if (b2l_aud_write32(b, 0x40, B2L_CS_IRQ1_STATUS1, (3u << 23) | B2L_CS_ERR_BITS)) return 2;   /* sticky bits cleared (W1C) */
    if (!b2l_aud_seq(b, 0x40, b2l_aud_pup, 5) || b2l_aud_upd(b, 0x40, B2L_CS_PWR_CTRL1, 1, 1, nullptr)) return 3;
    if (b2l_aud_poll(b, 0x40, B2L_CS_IRQ1_STATUS1, 1u << 24, 20, &v) > 20) return 4;
    if (v & B2L_CS_ERR_BITS) return 5;
    if (b2l_aud_upd(b, 0x40, B2L_CS_VOL, (0x7ffu << 3) | 7u, (b2l_aud_vol_code(s) << 3) | 3u, nullptr)) return 6;
    return 0;
}
static void b2l_aud_amp_down(struct b2l_aud_svc *s)
{
    struct b2l_aud_bus *b = &s->bus;
    uint32_t v = 0;
    b2l_aud_upd(b, 0x40, B2L_CS_VOL, 0x7ffu << 3, 0x4cfu << 3, nullptr);
    IODelay(20000);   /* the ramp down */
    b2l_aud_write32(b, 0x40, B2L_CS_IRQ1_STATUS1, 1u << 23);
    b2l_aud_upd(b, 0x40, B2L_CS_PWR_CTRL1, 1, 0, nullptr);
    IODelay(1000);
    b2l_aud_seq(b, 0x40, b2l_aud_pdn, 5);
    b2l_aud_poll(b, 0x40, B2L_CS_IRQ1_STATUS1, 1u << 23, 20, &v);
    b2l_aud_upd(b, 0x40, 0x02018, 0x01, 0, nullptr);
}
static void b2l_aud_fault(struct b2l_aud_svc *s, const char *what, uint32_t v)
{
    b2l_aud_amp_down(s);
    s->amp_on = false;
    s->cool = s->tick + 3000;
    s->dead = ++s->faults >= 3;
    b2l_diag_record("B2L AUDIO: ios-fault %s value=%08x faults=%u | %s\n", what, v, s->faults,
        s->dead ? "speaker-off-until-reboot" : "speaker-off-30s");
}
/* One 10 ms step of the service thread (the host test calls it directly). */
static void b2l_aud_svc_step(struct b2l_aud_svc *s)
{
    IOSimpleLockLock(s->lock);
    const uint32_t t = ++s->tick, N = B2L_AUD_RING_FRAMES, r = b2l_aud_rpos(s->x);
    if (r < N) {   /* zero what has been played */
        const uint32_t z = (r + N - B2L_AUD_LAG) % N;
        uint32_t d = (z + N - s->zpos) % N;
        if (s->wvalid && (s->wpos + N - r) % N <= s->wahead) {
            /* B1L-0279: the writer is ahead of the RDMA: a late step never zeroes its frames (r..wpos), which lie
             * just behind z once the writer is more than N - LAG - d ahead */
            const uint32_t free_ = (z + N - s->wpos) % N;
            if (d > free_) { s->zpos = s->wpos; d = free_; }
        }
        for (uint32_t i = 0; i < d; ++i) {
            int32_t *f = s->ring + 4 * ((s->zpos + i) % N);
            f[0] = f[1] = f[2] = f[3] = 0;
        }
        b2l_aud_ring_clean(s->ring, s->zpos, d);
        s->zpos = z; s->played += d;
        const uint32_t ahead = (s->wpos + N - r) % N;   /* grew without a write: the RDMA passed the writer (a gap) */
        if (ahead > s->wahead) s->wvalid = false; else s->wahead = ahead;
    }
    const bool sound = s->sound_tick && t - s->sound_tick < 300;
    const uint64_t in = s->in, lost = s->lost, resyncs = s->resyncs;
    IOSimpleLockUnlock(s->lock);

    if (s->dead) return;
    if (sound && !s->amp_on && t >= s->cool) {
        s->vol_seen = s->vol_gen;
        const unsigned e = b2l_aud_amp_up(s);
        if (e) { b2l_aud_fault(s, "amp-up", e); return; }
        s->amp_on = true;
        b2l_diag_record("B2L AUDIO: ios-play amp=on vol=%03x below=%u muted=%u in=%llu lost=%llu resyncs=%llu rpos=%u\n",
            b2l_aud_vol_code(s), s->below, s->muted, (unsigned long long)in, (unsigned long long)lost, (unsigned long long)resyncs, r);
    } else if (!sound && s->amp_on) {
        b2l_aud_amp_down(s);
        s->amp_on = false;
        b2l_diag_record("B2L AUDIO: ios-stop amp=off in=%llu lost=%llu resyncs=%llu played=%llu\n",
            (unsigned long long)in, (unsigned long long)lost, (unsigned long long)resyncs, (unsigned long long)s->played);
    } else if (s->amp_on) {
        uint32_t v = 0;
        if (s->vol_seen != s->vol_gen) {
            s->vol_seen = s->vol_gen;
            b2l_aud_upd(&s->bus, 0x40, B2L_CS_VOL, 0x7ffu << 3, b2l_aud_vol_code(s) << 3, nullptr);
            b2l_diag_record("B2L AUDIO: ios-volume below=%u muted=%u vol=%03x\n", s->below, s->muted, b2l_aud_vol_code(s));
        }
        if (t % 10 == 0) {   /* every 100 ms */
            if (b2l_aud_read32(&s->bus, 0x40, B2L_CS_IRQ1_STATUS1, &v)) b2l_aud_fault(s, "i2c", 0);
            else if (v & B2L_CS_ERR_BITS) b2l_aud_fault(s, "amp-error", v & B2L_CS_ERR_BITS);
        }
    }
}
#ifndef B2L_AUDIO_HOST_TEST
static void b2l_aud_svc_thread(void *, wait_result_t)
{
    for (;;) { IOSleep(10); b2l_aud_svc_step(&b2l_aud_svc); }
}
#endif
/* Hand the running path (ring silent, bot@40 in standby out of reset) to the service; false if it cannot start. */
static bool b2l_aud_svc_start(const struct b2l_aud_bus *b, volatile uint8_t *x, int32_t *ring)
{
    struct b2l_aud_svc *s = &b2l_aud_svc;
    s->lock = IOSimpleLockAlloc();
    if (!s->lock) return false;
    uint32_t max = 48;
    PE_parse_boot_argn("b2l_audio_max", &max, sizeof(max));
    s->max_below = max < 24 ? 24 : max;
    s->bus = *b; s->x = x; s->ring = ring; s->tick = 1;
    const uint32_t r = b2l_aud_rpos(x);
    s->zpos = r < B2L_AUD_RING_FRAMES ? r : 0;
#ifndef B2L_AUDIO_HOST_TEST
    thread_t th;
    if (kernel_thread_start(b2l_aud_svc_thread, nullptr, &th) != KERN_SUCCESS) return false;
    thread_deallocate(th);
#endif
    __atomic_store_n(&s->running, true, __ATOMIC_RELEASE);
    return true;
}

/* ---- A4: UAIF1 bit clock, amplifiers' GLOBAL_EN as a clock check, then all undone; A5 inside;
 * with keep (A6), a working path stays up for iOS instead of being undone ---- */
static bool b2l_audio_uaif1(struct b2l_aud_bus *b, const struct b2l_aud_abox *ab, volatile uint8_t *pmu, bool beep, bool keep)
{
    if (!ab->cmu || !ab->abox || !ab->pins || !ab->pll_hz) {
        b2l_diag_record("B2L AUDIO: uaif1 skipped (A2 not ready: cmu=%d abox=%d pins=%d pll_hz=%llu)\n",
            ab->cmu != nullptr, ab->abox != nullptr, ab->pins != nullptr, (unsigned long long)ab->pll_hz);
        return false;
    }
    volatile uint8_t *cmu = ab->cmu, *x = ab->abox, *pin = ab->pins;
    const uint32_t audif = b2l_aud_rd(cmu, B2L_AUD_A_DIV_AUDIF);
    const uint64_t f = ab->pll_hz / ((audif & 0x1ffu) + 1), n = (f + B2L_AUD_BCLK_HZ / 2) / B2L_AUD_BCLK_HZ;
    const uint64_t miss = f > n * B2L_AUD_BCLK_HZ ? f - n * B2L_AUD_BCLK_HZ : n * B2L_AUD_BCLK_HZ - f;
    if (n < 1 || n > 512 || miss * 1000 > f) {
        b2l_diag_record("B2L AUDIO: uaif1 skipped (div_audif=%08x audif=%llu Hz is not a multiple of 6.144 MHz)\n",
            audif, (unsigned long long)f);
        return false;
    }

    /* Start values of everything A4 writes; each is put back at the end. */
    const uint32_t pad = b2l_aud_rd(pmu, B2L_AUD_PMU_ABOX_PAD), div = b2l_aud_rd(cmu, B2L_AUD_A_DIV_UAIF1),
                   mux = b2l_aud_rd(cmu, B2L_AUD_A_MUX_UAIF1), gate = b2l_aud_rd(cmu, B2L_AUD_A_BCLK_UAIF1),
                   pcon = b2l_aud_rd(pin, 0x20), c0 = b2l_aud_rd(x, B2L_AUD_X_UAIF1_CTRL0),
                   c1 = b2l_aud_rd(x, B2L_AUD_X_UAIF1_CTRL1), qd = b2l_aud_rd(x, B2L_AUD_X_QCH_DISABLE);

    if (!(pad & 0x800u)) b2l_aud_wr(pmu, B2L_AUD_PMU_ABOX_PAD, pad | 0x800u);
    b2l_aud_wr(cmu, B2L_AUD_A_DIV_UAIF1, (div & ~0x1ffu) | (uint32_t)(n - 1));
    for (unsigned i = 0; i < 100 && (b2l_aud_rd(cmu, B2L_AUD_A_DIV_UAIF1) & (1u << 16)); ++i) IODelay(1);  /* DIV busy */
    b2l_aud_wr(cmu, B2L_AUD_A_MUX_UAIF1, mux & ~1u);
    if (!b2l_aud_gate_open(gate)) b2l_aud_wr(cmu, B2L_AUD_A_BCLK_UAIF1, gate | (1u << 21));
    b2l_aud_wr(pin, 0x20, (pcon & ~0xffffu) | 0x2222u);
    b2l_aud_wr(x, B2L_AUD_X_UAIF1_CTRL1, (c1 & 0xe0000000u) | B2L_AUD_UAIF1_CTRL1);
    b2l_aud_wr(x, B2L_AUD_X_UAIF1_CTRL0, c0 | 0x4u);              /* master */
    b2l_aud_wr(x, B2L_AUD_X_QCH_DISABLE, qd | (1u << 3));         /* BCLK_UAIF1 always on */
    b2l_aud_wr(x, B2L_AUD_X_UAIF1_CTRL0, c0 | 0x5u);              /* + SPK_ENABLE (no data source: zeros) */
    IODelay(2000);
    b2l_diag_record("B2L AUDIO: uaif1-on pad=%08x>%08x div_audif=%08x div_uaif1=%08x>%08x mux=%08x>%08x gate=%08x>%08x gpb1_con=%08x>%08x ctrl1=%08x>%08x ctrl0=%08x>%08x qch_disable=%08x>%08x status=%08x bclk=%llu\n",
        pad, b2l_aud_rd(pmu, B2L_AUD_PMU_ABOX_PAD), audif, div, b2l_aud_rd(cmu, B2L_AUD_A_DIV_UAIF1),
        mux, b2l_aud_rd(cmu, B2L_AUD_A_MUX_UAIF1), gate, b2l_aud_rd(cmu, B2L_AUD_A_BCLK_UAIF1),
        pcon, b2l_aud_rd(pin, 0x20), c1, b2l_aud_rd(x, B2L_AUD_X_UAIF1_CTRL1), c0, b2l_aud_rd(x, B2L_AUD_X_UAIF1_CTRL0),
        qd, b2l_aud_rd(x, B2L_AUD_X_QCH_DISABLE), b2l_aud_rd(x, B2L_AUD_X_UAIF1_STATUS), (unsigned long long)(f / n));

    /* A5: the RDMA feeds UAIF1 before any amplifier output stage exists. */
    struct b2l_aud_path path = {};
    bool play = beep && b2l_aud_path_on(x, &path, !keep);   /* level 7: a silent ring, no boot beep (B1L-0245) */

    /* cs35l41_main_amp_event POST_PMU / POST_PMD; the output stage (PWR_CTRL2 AMP_EN) only on bot for A5. */
    uint32_t s0[2][4] = { { 0 } }, s1[2][4] = { { 0 } }, v = 0;
    unsigned pup[2] = { 999, 999 }, pdn[2] = { 999, 999 }, io[2] = { 0, 0 };
    if (play) {
        struct b2l_aud_txt t = { {0}, 0 };
        b2l_aud_ts(&t, "B2L AUDIO: beep-amp bot@40");
        /* DAPM order for "AMP SPK": PCM source ASP, CLASS H, unmuted output, Main AMP (AMP_EN, boost off). */
        const uint32_t dapm[4][3] = { { 0x04c00, 0x7f, 0x08 }, { 0x0201c, 0x10, 0x10 }, { 0x02024, 0x10, 0 }, { 0x02018, 0x31, 0x01 } };
        unsigned r = 0;
        for (unsigned i = 0; !r && i < 4; ++i) r =b2l_aud_upd(b, 0x40, dapm[i][0], dapm[i][1], dapm[i][2], &t);
        b2l_aud_ts(&t, r ? " | FAILED\n" : " | ready\n");
        b2l_diag_record("%s", t.s);
        if (r & 2) io[0] = 2;
        play = !r;
    }
    for (unsigned a = 0; a < 2; ++a) {
        const uint8_t addr = (uint8_t)(0x40 + a);
        for (unsigned i = 0; !io[a] && i < 4; ++i) if (b2l_aud_read32(b, addr, B2L_CS_IRQ1_STATUS1 + 4 * i, &s0[a][i])) io[a] = 1;
        if (!io[a] && (!b2l_aud_seq(b, addr, b2l_aud_pup, 5) || b2l_aud_upd(b, addr, B2L_CS_PWR_CTRL1, 1, 1, nullptr))) io[a] = 2;
    }
    for (unsigned a = 0; a < 2; ++a)
        if (!io[a]) pup[a] = b2l_aud_poll(b, (uint8_t)(0x40 + a), B2L_CS_IRQ1_STATUS1, 1u << 24, 100, &v);
    for (unsigned a = 0; a < 2; ++a)
        for (unsigned i = 0; !io[a] && i < 4; ++i)
            if (b2l_aud_read32(b, (uint8_t)(0x40 + a), B2L_CS_IRQ1_STATUS1 + 4 * i, &s1[a][i])) io[a] = 3;

    /* A5 tone: -10 dB with the soft ramp, error bits every 100 ms, then mute and wait for the ramp. */
    unsigned played_ms = 0, vol_up = 0, vol_down = 9;
    uint32_t perr = 0, st_end = 0;
    const bool bot_up = play && !io[0] && pup[0] <= 100 && !(s1[0][0] & B2L_CS_ERR_BITS);
    if (bot_up && !keep) {
        vol_up = b2l_aud_upd(b, 0x40, B2L_CS_VOL, (0x7ffu << 3) | 7u, (B2L_CS_VOL_BEEP << 3) | 3u, nullptr) == 0;
        while (vol_up && played_ms < 700) {
            IODelay(100000);
            if (b2l_aud_read32(b, 0x40, B2L_CS_IRQ1_STATUS1, &v)) { perr = 0xffffffffu; break; }
            if ((perr = v & B2L_CS_ERR_BITS)) break;
            played_ms += 100;
        }
        st_end = b2l_aud_rd(x, B2L_AUD_X_RDMA0 + 0x30);
        vol_down = b2l_aud_upd(b, 0x40, B2L_CS_VOL, 0x7ffu << 3, 0x4cfu << 3, nullptr);
        IODelay(60000);
    }
    for (unsigned a = 0; a < 2; ++a) {   /* power down whatever was attempted */
        const uint8_t addr = (uint8_t)(0x40 + a);
        if (io[a] == 1) continue;
        if (b2l_aud_upd(b, addr, B2L_CS_PWR_CTRL1, 1, 0, nullptr) == 2) { io[a] = 4; continue; }
        IODelay(1000);
        if (!b2l_aud_seq(b, addr, b2l_aud_pdn, 5)) { io[a] = 5; continue; }
        pdn[a] = b2l_aud_poll(b, addr, B2L_CS_IRQ1_STATUS1, 1u << 23, 100, &v);
    }
    /* A6: keep everything up for iOS only if the RDMA moved, bot@40 powered up with its output stage and without
     * error, and both amplifiers powered down again. */
    bool kept = false;
    if (path.mem) {
        /* Output stage off (the amplifiers go back into reset with the pins, or stay in standby for iOS). */
        const unsigned amp_off = beep ? b2l_aud_upd(b, 0x40, 0x02018, 0x01, 0, nullptr) : 0;
        if (keep && bot_up && !amp_off && !io[0] && !io[1] && pdn[0] <= 100 && pdn[1] <= 100) {
            for (unsigned i = 0; i < 4 * B2L_AUD_RING_FRAMES; ++i) path.va[i] = 0;   /* the ring falls silent */
            b2l_aud_dma_clean(path.va, B2L_AUD_RING_BYTES);
            kept = b2l_aud_svc_start(b, x, path.va);
        }
        bool abox_back = true, freed = false;
        if (!kept) freed = b2l_aud_path_off(x, &path, &abox_back);
        b2l_diag_record("B2L AUDIO: beep tone=%d played=%d ms=%u vol_up=%u vol_down=%u err=%08x rdma_status_end=%08x amp_en_off=%d | %s rdma-idle-ring-freed=%d abox-path-restored=%d\n",
            !keep, played_ms >= 700, played_ms, vol_up, vol_down, perr, st_end, amp_off == 0,
            kept ? "kept-for-ios" : "undone", freed, kept ? 0 : abox_back);
    }
    if (kept) {
        b2l_diag_record("B2L AUDIO: ios-ready /dev/hakios-audio -> rdma0 ring_pa=%08x (%u frames) uaif1 6.144 MHz, bot@40 standby, max=-%u/8 dB, rpos=%u\n",
            (uint32_t)path.pa, B2L_AUD_RING_FRAMES, b2l_aud_svc.max_below, b2l_aud_rpos(x));
    }

    /* Undo A4 in reverse (not when kept for iOS); the PMU pad stays released (abox.c never puts it back either). */
    if (!kept) {
        b2l_aud_wr(x, B2L_AUD_X_UAIF1_CTRL0, c0);
        b2l_aud_wr(x, B2L_AUD_X_QCH_DISABLE, qd);
        b2l_aud_wr(x, B2L_AUD_X_UAIF1_CTRL1, c1);
        b2l_aud_wr(pin, 0x20, pcon);
        b2l_aud_wr(cmu, B2L_AUD_A_BCLK_UAIF1, gate);
        b2l_aud_wr(cmu, B2L_AUD_A_MUX_UAIF1, mux);
        b2l_aud_wr(cmu, B2L_AUD_A_DIV_UAIF1, div);
    }
    const bool undone = !kept && b2l_aud_rd(x, B2L_AUD_X_UAIF1_CTRL0) == c0 && b2l_aud_rd(x, B2L_AUD_X_QCH_DISABLE) == qd &&
        b2l_aud_rd(x, B2L_AUD_X_UAIF1_CTRL1) == c1 && b2l_aud_rd(pin, 0x20) == pcon &&
        ((b2l_aud_rd(cmu, B2L_AUD_A_DIV_UAIF1) ^ div) & 0x1ffu) == 0 && ((b2l_aud_rd(cmu, B2L_AUD_A_MUX_UAIF1) ^ mux) & 1u) == 0;

    bool ok = true;
    for (unsigned a = 0; a < 2; ++a) {
        const bool up = !io[a] && pup[a] <= 100, err = (s1[a][0] & B2L_CS_ERR_BITS) != 0;
        ok = ok && up && !err && pdn[a] <= 100;
        b2l_diag_record("B2L AUDIO: amp-power %s io=%u pup_done=%d(%ums) pdn_done=%d(%ums) irq1 before=%08x/%08x/%08x/%08x after=%08x/%08x/%08x/%08x err=%08x\n",
            b2l_aud_name[a], io[a], up, pup[a], pdn[a] <= 100, pdn[a], s0[a][0], s0[a][1], s0[a][2], s0[a][3],
            s1[a][0], s1[a][1], s1[a][2], s1[a][3], s1[a][0] & B2L_CS_ERR_BITS);
    }
    b2l_diag_record("B2L AUDIO: uaif1-verdict %s | abox-cmu-pins-undone=%d kept-for-ios=%d\n",
        ok ? "amps-clocked-by-uaif1" : "amps-not-clocked", undone, kept);
    return kept;
}

/* ---- A1 (+ A3, A4 while the amplifiers are out of reset) ---- */
#define B2L_AUD_M0 ((0xfu << (B2L_AUD_SCL * 4)) | (0xfu << (B2L_AUD_SDA * 4)))
#define B2L_AUD_D0 ((1u << B2L_AUD_SCL) | (1u << B2L_AUD_SDA))
#define B2L_AUD_M1 ((0xfu << (B2L_AUD_RST_BOT * 4)) | (0xfu << (B2L_AUD_RST_RCV * 4)))
#define B2L_AUD_D1 ((1u << B2L_AUD_RST_BOT) | (1u << B2L_AUD_RST_RCV))
static void b2l_audio_amps(volatile uint8_t *p1, uint32_t level, const struct b2l_aud_abox *ab, volatile uint8_t *pmu)
{
    const uint32_t con0 = b2l_aud_rd(p1, B2L_AUD_GPH0), dat0 = b2l_aud_rd(p1, B2L_AUD_GPH0 + 4);
    const uint32_t con1 = b2l_aud_rd(p1, B2L_AUD_GPH1), dat1 = b2l_aud_rd(p1, B2L_AUD_GPH1 + 4);
    struct b2l_aud_bus bus = { p1, false };
    uint32_t id[2] = { 0, 0 }, rev[2] = { 0, 0 };
    unsigned eid[2] = { 9, 9 }, erev[2] = { 9, 9 };   /* 9 = not tried */

    /* Both lines released, data latches 0: from here a line is only ever input or output-low. */
    b2l_aud_pin_mode(p1, B2L_AUD_GPH0, B2L_AUD_SCL, 0);
    b2l_aud_pin_mode(p1, B2L_AUD_GPH0, B2L_AUD_SDA, 0);
    /* One write for both latches: DAT reads back the level of input pins, so a second
     * read-modify-write would put the first line's pulled-up 1 back into its latch.
     * gph0's DAT is not written again until the restore below. */
    b2l_aud_wr(p1, B2L_AUD_GPH0 + 4, b2l_aud_rd(p1, B2L_AUD_GPH0 + 4) & ~B2L_AUD_D0);
    IODelay(20);
    const unsigned idle_scl = b2l_aud_pin_get(p1, B2L_AUD_SCL), idle_sda = b2l_aud_pin_get(p1, B2L_AUD_SDA);

    if (idle_scl && idle_sda) {
        /* cs35l41-core.c: reset low 1 ms, high, 2 ms. */
        b2l_aud_pin_dat(p1, B2L_AUD_GPH1, B2L_AUD_RST_BOT, 0);
        b2l_aud_pin_dat(p1, B2L_AUD_GPH1, B2L_AUD_RST_RCV, 0);
        b2l_aud_pin_mode(p1, B2L_AUD_GPH1, B2L_AUD_RST_BOT, 1);
        b2l_aud_pin_mode(p1, B2L_AUD_GPH1, B2L_AUD_RST_RCV, 1);
        IODelay(1100);
        b2l_aud_pin_dat(p1, B2L_AUD_GPH1, B2L_AUD_RST_BOT, 1);
        b2l_aud_pin_dat(p1, B2L_AUD_GPH1, B2L_AUD_RST_RCV, 1);
        IODelay(2100);
        for (unsigned a = 0; a < 2 && !bus.stuck; ++a) {
            eid[a] = b2l_aud_read32(&bus, (uint8_t)(0x40 + a), 0x0, &id[a]);
            if (!eid[a]) erev[a] = b2l_aud_read32(&bus, (uint8_t)(0x40 + a), 0x4, &rev[a]);
        }
    }
    const bool both = !eid[0] && !eid[1] && !erev[0] && !erev[1] && id[0] == B2L_AUD_CS35L40_ID && id[1] == B2L_AUD_CS35L40_ID;
    b2l_diag_record("B2L AUDIO: amp-id idle scl=%u sda=%u | bot@40 err=%u id=%08x rev=%08x err=%u | rcv@41 err=%u id=%08x rev=%08x err=%u | %s\n",
        idle_scl, idle_sda, eid[0], id[0], rev[0], erev[0], eid[1], id[1], rev[1], erev[1],
        both ? "both-cs35l40" : !(idle_scl && idle_sda) ? "bus-not-idle" : bus.stuck ? "scl-stuck" : "no-answer");

    bool kept = false;
    if (level >= 4 && both) {
        bool otp[2] = { false, false };
        const bool r0 = b2l_audio_amp_prep(&bus, 0, rev[0], &otp[0]);
        const bool r1 = b2l_audio_amp_prep(&bus, 1, rev[1], &otp[1]);
        if (level >= 5) {
            if (level >= 6 && !otp[0]) b2l_diag_record("B2L AUDIO: beep skipped (amp-otp bot@40 not ok)\n");
            if (r0 && r1) kept = b2l_audio_uaif1(&bus, ab, pmu, level >= 6 && otp[0], level >= 7);
            else b2l_diag_record("B2L AUDIO: uaif1 skipped (amp-prep not ready: bot=%d rcv=%d)\n", r0, r1);
        }
    } else if (level >= 4) b2l_diag_record("B2L AUDIO: amp-prep skipped (amp-id did not find both CS35L40)\n");

    if (kept) {   /* A6: the amplifiers stay out of reset and the bus pins stay with b2l_aud_svc */
        b2l_diag_record("B2L AUDIO: amps-kept bot@40 standby, rcv@41 standby, bus pins with the iOS service\n");
        return;
    }
    /* Back to the start state: resets first (the amplifiers return to reset), then the bus pins. */
    b2l_aud_wr(p1, B2L_AUD_GPH1 + 4, (b2l_aud_rd(p1, B2L_AUD_GPH1 + 4) & ~B2L_AUD_D1) | (dat1 & B2L_AUD_D1));
    b2l_aud_wr(p1, B2L_AUD_GPH1, (b2l_aud_rd(p1, B2L_AUD_GPH1) & ~B2L_AUD_M1) | (con1 & B2L_AUD_M1));
    b2l_aud_wr(p1, B2L_AUD_GPH0, (b2l_aud_rd(p1, B2L_AUD_GPH0) & ~B2L_AUD_M0) | (con0 & B2L_AUD_M0));
    b2l_aud_wr(p1, B2L_AUD_GPH0 + 4, (b2l_aud_rd(p1, B2L_AUD_GPH0 + 4) & ~B2L_AUD_D0) | (dat0 & B2L_AUD_D0));
    const bool restored = ((b2l_aud_rd(p1, B2L_AUD_GPH0) ^ con0) & B2L_AUD_M0) == 0 && ((b2l_aud_rd(p1, B2L_AUD_GPH1) ^ con1) & B2L_AUD_M1) == 0;
    b2l_diag_record("B2L AUDIO: amps-off pins-restored=%d (resets and bus pins back to the start state)\n", restored);
}

/* ---- A2: ABOX side, read only ---- */
static void b2l_audio_abox(struct b2l_aud_abox *ab)
{
    volatile uint8_t *cmu = (volatile uint8_t *)ml_io_map(B2L_AUD_CMU_AUD_PA, 0x4000);
    if (!cmu) { b2l_diag_record("B2L AUDIO: CMU_AUD map failed\n"); return; }
    const uint32_t aclk = b2l_aud_rd(cmu, B2L_AUD_A_ABOX_ACLK), q_aclk = b2l_aud_rd(cmu, B2L_AUD_A_QCH_ACLK);
    const uint32_t gpio = b2l_aud_rd(cmu, B2L_AUD_A_GPIO_PCLK), q_gpio = b2l_aud_rd(cmu, B2L_AUD_A_QCH_GPIO);
    const uint32_t bclk1 = b2l_aud_rd(cmu, B2L_AUD_A_BCLK_UAIF1), q_bclk1 = b2l_aud_rd(cmu, B2L_AUD_A_QCH_BCLK1);
    const uint32_t pll0 = b2l_aud_rd(cmu, B2L_AUD_A_PLL_AUD0), pll3 = b2l_aud_rd(cmu, B2L_AUD_A_PLL_AUD0_K);
    /* 0831x: Fout = 26 MHz * (M + K / 65536) / (P << S) */
    const uint64_t m = (pll0 >> 16) & 0x3ffu, p = (pll0 >> 8) & 0x3fu, s = pll0 & 7u;
    const bool pll_ok = (pll0 >> 31) & (pll0 >> 29) & 1u;
    const uint64_t hz = (pll_ok && p) ? ((26000000ull * (uint64_t)((int64_t)(m << 16) + (int16_t)(pll3 & 0xffffu))) / (p << s)) >> 16 : 0;
    b2l_diag_record("B2L AUDIO: cmu_aud pll_aud0=%08x k=%08x (%llu Hz) mux_cpu_user=%08x div_audif=%08x div_bus=%08x div_uaif1=%08x mux_uaif1=%08x aclk=%08x qch_aclk=%08x bclk_uaif1=%08x qch_bclk1=%08x gpio_pclk=%08x qch_gpio=%08x sysreg_pclk=%08x\n",
        pll0, pll3, (unsigned long long)hz, b2l_aud_rd(cmu, B2L_AUD_A_MUX_CPU_USER), b2l_aud_rd(cmu, B2L_AUD_A_DIV_AUDIF),
        b2l_aud_rd(cmu, B2L_AUD_A_DIV_BUS), b2l_aud_rd(cmu, B2L_AUD_A_DIV_UAIF1), b2l_aud_rd(cmu, B2L_AUD_A_MUX_UAIF1),
        aclk, q_aclk, bclk1, q_bclk1, gpio, q_gpio, b2l_aud_rd(cmu, B2L_AUD_A_SYSREG_PCLK));
    ab->cmu = cmu; ab->pll_hz = hz;

    /* The ABOX bus clock divides PLL_AUD0: no SFR read unless the PLL is on and locked. */
    const bool abox_ok = pll_ok && b2l_aud_gate_open(aclk) && b2l_aud_qch_open(q_aclk);
    if (abox_ok) {
        volatile uint8_t *a = (volatile uint8_t *)ml_io_map(B2L_AUD_ABOX_SFR_PA, 0x2000);
        if (!a) { b2l_diag_record("B2L AUDIO: ABOX map failed\n"); return; }
        ab->abox = a;
        b2l_diag_record("B2L AUDIO: abox ip=%08x version=%08x syspower=%08x/%08x config0=%08x dyn_clk_off=%08x/%08x qch_disable=%08x route=%08x/%08x/%08x spus=%08x/%08x\n",
            b2l_aud_rd(a, 0x00), b2l_aud_rd(a, 0x04), b2l_aud_rd(a, 0x10), b2l_aud_rd(a, 0x14), b2l_aud_rd(a, 0x20),
            b2l_aud_rd(a, 0x30), b2l_aud_rd(a, 0x34), b2l_aud_rd(a, 0x38),
            b2l_aud_rd(a, 0x40), b2l_aud_rd(a, 0x44), b2l_aud_rd(a, 0x48), b2l_aud_rd(a, 0x200), b2l_aud_rd(a, 0x204));
        b2l_diag_record("B2L AUDIO: abox uaif0=%08x/%08x/%08x uaif1=%08x/%08x/%08x uaif2=%08x/%08x/%08x uaif3=%08x/%08x/%08x rdma0=%08x/%08x/%08x\n",
            b2l_aud_rd(a, 0x500), b2l_aud_rd(a, 0x504), b2l_aud_rd(a, 0x50c),
            b2l_aud_rd(a, 0x510), b2l_aud_rd(a, 0x514), b2l_aud_rd(a, 0x51c),
            b2l_aud_rd(a, 0x520), b2l_aud_rd(a, 0x524), b2l_aud_rd(a, 0x52c),
            b2l_aud_rd(a, 0x530), b2l_aud_rd(a, 0x534), b2l_aud_rd(a, 0x53c),
            b2l_aud_rd(a, 0x1000), b2l_aud_rd(a, 0x1004), b2l_aud_rd(a, 0x1030));
    }
    const bool pins_ok = b2l_aud_gate_open(gpio) && b2l_aud_qch_open(q_gpio);
    uint32_t con = 0, dat = 0;
    if (pins_ok) {
        volatile uint8_t *p = (volatile uint8_t *)ml_io_map(B2L_AUD_AUD_PIN_PA, 0x1000);
        if (!p) { b2l_diag_record("B2L AUDIO: AUD pinctrl map failed\n"); return; }
        ab->pins = p;
        uint32_t c, d;
        b2l_aud_bank(p, 0x00, "gpb0(mclk=0)", &c, &d);
        b2l_aud_bank(p, 0x20, "gpb1(uaif1 0..3)", &con, &dat);
        b2l_aud_bank(p, 0x40, "gpb2", &c, &d);
    }
    b2l_diag_record("B2L AUDIO: abox-verdict pll_aud0=%s abox_regs=%d aud_pins=%d uaif1_pins_fn=%u%u%u%u bclk_uaif1=%s\n",
        pll_ok ? "locked" : "off-or-unlocked", abox_ok, pins_ok,
        b2l_aud_pin_fn(con, 0), b2l_aud_pin_fn(con, 1), b2l_aud_pin_fn(con, 2), b2l_aud_pin_fn(con, 3),
        (b2l_aud_gate_open(bclk1) && b2l_aud_qch_open(q_bclk1)) ? "open" : "gated");
}

// shortcut: ml_io_map pages are never unmapped (a few pages of VA, as b2l_gpu_probe); free them if this ever runs more than once.
static void b2l_audio_probe(void)
{
    uint32_t level = 7;   /* A6 for iOS, silent; 6 = the audible boot beep (off since B1L-0245), 5 = no path */
    PE_parse_boot_argn("b2l_audio", &level, sizeof(level));
    if (!level) { b2l_diag_record("B2L AUDIO: off (b2l_audio=0)\n"); return; }

    volatile uint8_t *pmu = (volatile uint8_t *)ml_io_map(B2L_AUD_PMU_PA, 0x3000);
    volatile uint8_t *top = (volatile uint8_t *)ml_io_map(B2L_AUD_CMU_TOP_PA, 0x3000);
    volatile uint8_t *p0 = (volatile uint8_t *)ml_io_map(B2L_AUD_PERIC0_PIN_PA, 0x1000);
    volatile uint8_t *alive = (volatile uint8_t *)ml_io_map(B2L_AUD_ALIVE_PIN_PA, 0x1000);
    if (!pmu || !top || !p0 || !alive) { b2l_diag_record("B2L AUDIO: map failed\n"); return; }

    uint32_t pd_cfg = b2l_aud_rd(pmu, B2L_AUD_PMU_PD_AUD), pd_st = b2l_aud_rd(pmu, B2L_AUD_PMU_PD_AUD + 4);
    b2l_diag_record("B2L AUDIO: pd-aud conf=%08x status=%08x (%s) core0=%08x/%08x core1=%08x/%08x\n",
        pd_cfg, pd_st, (pd_st & 1) ? "on" : "off",
        b2l_aud_rd(pmu, B2L_AUD_PMU_CORE0), b2l_aud_rd(pmu, B2L_AUD_PMU_CORE0 + 4),
        b2l_aud_rd(pmu, B2L_AUD_PMU_CORE1), b2l_aud_rd(pmu, B2L_AUD_PMU_CORE1 + 4));

    uint32_t t_aud = b2l_aud_rd(top, B2L_AUD_TOP_AUD_CPU), t_bus = b2l_aud_rd(top, B2L_AUD_TOP_PERIC1_BUS),
             t_ip = b2l_aud_rd(top, B2L_AUD_TOP_PERIC1_IP);
    b2l_diag_record("B2L AUDIO: cmu_top aud_cpu=%08x peric1_bus=%08x peric1_ip=%08x\n", t_aud, t_bus, t_ip);

    uint32_t con, dat, codec_con, codec_dat;
    b2l_aud_bank(p0, B2L_AUD_GPG1, "gpg1(codec reset=0)", &codec_con, &codec_dat);
    b2l_aud_bank(alive, B2L_AUD_GPA2, "gpa2(codec irq)", &con, &dat);

    const char *amp = "peric1-bus-gated";
    uint32_t rst_con = 0, rst_dat = 0, scl_fn = 0, sda_fn = 0;
    bool i2c_read = false;
    volatile uint8_t *pins = nullptr;
    if (b2l_aud_gate_open(t_bus)) {
        volatile uint8_t *cmu = (volatile uint8_t *)ml_io_map(B2L_AUD_CMU_PERIC1_PA, 0x4000);
        if (!cmu) { b2l_diag_record("B2L AUDIO: CMU_PERIC1 map failed\n"); return; }
        uint32_t g_gpio = b2l_aud_rd(cmu, B2L_AUD_P1_GPIO_PCLK), q_gpio = b2l_aud_rd(cmu, B2L_AUD_P1_GPIO_QCH);
        uint32_t g_ip = b2l_aud_rd(cmu, B2L_AUD_P1_I2C_IPCLK), g_pc = b2l_aud_rd(cmu, B2L_AUD_P1_I2C_PCLK),
                 q_i2c = b2l_aud_rd(cmu, B2L_AUD_P1_I2C_QCH);
        b2l_diag_record("B2L AUDIO: cmu_peric1 gpio_pclk=%08x gpio_qch=%08x usi17_i2c ipclk=%08x pclk=%08x qch=%08x mux=%08x div=%08x\n",
            g_gpio, q_gpio, g_ip, g_pc, q_i2c, b2l_aud_rd(cmu, B2L_AUD_P1_MUX_I2C), b2l_aud_rd(cmu, B2L_AUD_P1_DIV_I2C));

        amp = "peric1-gpio-gated";
        if (b2l_aud_gate_open(g_gpio) && b2l_aud_qch_open(q_gpio)) {
            volatile uint8_t *p1 = (volatile uint8_t *)ml_io_map(B2L_AUD_PERIC1_PIN_PA, 0x1000);
            if (!p1) { b2l_diag_record("B2L AUDIO: PERIC1 pinctrl map failed\n"); return; }
            pins = p1;
            b2l_aud_bank(p1, B2L_AUD_GPH0, "gph0(amp scl=6 sda=7)", &con, &dat);
            scl_fn = b2l_aud_pin_fn(con, B2L_AUD_SCL); sda_fn = b2l_aud_pin_fn(con, B2L_AUD_SDA);
            b2l_aud_bank(p1, B2L_AUD_GPH1, "gph1(amp reset bot=1 rcv=2)", &rst_con, &rst_dat);
            amp = "i2c-gated";
            if (b2l_aud_gate_open(g_ip) && b2l_aud_gate_open(g_pc) && b2l_aud_qch_open(q_i2c)) {
                volatile uint8_t *i2c = (volatile uint8_t *)ml_io_map(B2L_AUD_AMP_I2C_PA, 0x1000);
                if (!i2c) { b2l_diag_record("B2L AUDIO: hsi2c map failed\n"); return; }
                uint32_t fs1 = b2l_aud_rd(i2c, 0x60), fs2 = b2l_aud_rd(i2c, 0x64);
                b2l_diag_record("B2L AUDIO: hsi2c@109a0000 ctl=%08x conf=%08x trans=%08x fs1=%08x fs2=%08x fs3=%08x usi_con=%08x usi_opt=%08x\n",
                    b2l_aud_rd(i2c, 0x00), b2l_aud_rd(i2c, 0x40), b2l_aud_rd(i2c, 0x50), fs1, fs2,
                    b2l_aud_rd(i2c, 0x68), b2l_aud_rd(i2c, 0xc4), b2l_aud_rd(i2c, 0xc8));
                i2c_read = true;
                amp = (fs1 && fs2) ? "i2c-timed" : "i2c-untimed";
            }
        }
    }
    /* Function 0 = input, 1 = output, 3 = I2C on gph0-6/7. */
    b2l_diag_record("B2L AUDIO: verdict pd-aud=%s amp-bus=%s scl_fn=%u sda_fn=%u amp_reset bot=fn%u/%u rcv=fn%u/%u codec_reset=fn%u/%u i2c_regs=%d\n",
        (pd_st & 1) ? "on" : "off", amp, scl_fn, sda_fn,
        b2l_aud_pin_fn(rst_con, 1), (rst_dat >> 1) & 1, b2l_aud_pin_fn(rst_con, 2), (rst_dat >> 2) & 1,
        b2l_aud_pin_fn(codec_con, 0), codec_dat & 1, i2c_read);

    /* A2 (read only) first, so A4 knows the ABOX state while the amplifiers are out of reset. */
    struct b2l_aud_abox ab = { nullptr, nullptr, nullptr, 0 };
    if (level >= 3) {
        /* CMU_AUD and everything behind it only with pd-aud on and the AUD clock from CMU_CMU not forced off. */
        if ((pd_st & 1) && b2l_aud_gate_open(t_aud)) b2l_audio_abox(&ab);
        else b2l_diag_record("B2L AUDIO: abox skipped (pd-aud off or aud_cpu gated)\n");
    }

    if (level < 2) return;
    /* A1 only from the state 0210 measured: both bus pins and both resets plain inputs. */
    if (!pins || scl_fn || sda_fn || b2l_aud_pin_fn(rst_con, B2L_AUD_RST_BOT) || b2l_aud_pin_fn(rst_con, B2L_AUD_RST_RCV))
        b2l_diag_record("B2L AUDIO: amp-id skipped (pins not in the 0210 start state)\n");
    else
        b2l_audio_amps(pins, level, &ab, pmu);
}

#endif /* B2L_AUDIO_H */
