/* SPDX-License-Identifier: BSD-2-Clause
 * B1L-0371, brightness step 3: DCS commands to the S6E3HA9 panel through DSIM0 (0x19080000), left running by S-Boot
 * in command mode. One path, under b2l_dsi_lock, on its own thread (never in the swap path), every wait bounded
 * (50 ms per packet, 100 ms per read) and slept, not spun.
 *
 * Closed list: the only packets this file can send are reads (max return size 0x37, DCS read 0x06) and three DCS long
 * writes: level-2 key {F0 5A 5A}/{F0 A5 A5}, AOR {B1 hi lo} with hi:lo inside Samsung's normal (non-HBM) AOR table
 * 0x0014..0x0BC9, and gamma update {F7 03}. No 0x51, no HBM, no MTP/flash register. Anything else is refused before
 * the FIFO is touched, and the first anomaly turns the file off for good (b2l_dsi_dead).
 *
 * Test (step 3): ~60 s after boot, read the AOR the bootloader left, write a medium one from the table, read it back,
 * wait 3 s, write the start value back. If the start value cannot be read, nothing is written.
 * Facts from LineageOS android_kernel_samsung_exynos9820 a412cfaf (GPL: facts only, no code copied): dpu20/cal_9820
 * regs-dsim.h and dsim_reg.c, dpu20/dsim_drv.c, panel/s6e3ha9 (beyond1 a3 s0 tables).
 * B1L-0372 (step 4): the slider is linked, see b2l_panel_link; the 3 s test runs only with b2l_panel_test=1.
 * Kill switch: b2l_panel=0.
 */
#ifndef B2L_PANEL_H
#define B2L_PANEL_H
#include <IOKit/IOLib.h>
#include <IOKit/IOLocks.h>
#include <IOKit/IOMemoryDescriptor.h>

#define B2L_DSIM_PA        0x19080000ULL
#define B2L_DSIM_LINK0     0x08
#define B2L_DSIM_LINK1     0x0c
#define B2L_DSIM_LINK3     0x14   /* bit0 PLL stable */
#define B2L_DSIM_CONFIG    0x4c   /* bit18 display mode: 1 video */
#define B2L_DSIM_INTSRC    0x50   /* write 1 to clear; bit18 RX data done */
#define B2L_DSIM_INTMSK    0x54
#define B2L_DSIM_PKTHDR    0x58
#define B2L_DSIM_PAYLOAD   0x5c
#define B2L_DSIM_RXFIFO    0x60
#define B2L_DSIM_FIFOCTRL  0x68   /* bit12 RX empty, bit11 PH full, bit10 PH empty, bit9 PL full, bit8 PL empty */
#define B2L_AOR_MIN        0x0014u
#define B2L_AOR_MAX        0x0BC9u   /* 2 nits: the darkest normal level, never 0 */
#define B2L_AOR_MEDIUM     0x0601u   /* beyond1 a3 s0 AOR table, index 31 */

static volatile uint8_t *b2l_dsi;
static IOMemoryMap *b2l_dsi_map;
static IOLock *b2l_dsi_lock;
static bool b2l_dsi_dead;

static uint32_t b2l_dsi_rd(uint32_t off) { return *(volatile uint32_t *)(b2l_dsi + off); }
static void b2l_dsi_wr(uint32_t off, uint32_t v)
{
    *(volatile uint32_t *)(b2l_dsi + off) = v;
#if defined(__arm64__)
    __asm__ volatile ("dsb sy" ::: "memory");   /* not on the Mac test (tests/test-panel-dcs.cpp) */
#endif
}

static void b2l_dsi_fail(const char *why)
{
    b2l_dsi_dead = true;
    b2l_diag_record("B2L PANEL: %s (link1 %08x fifo %08x intsrc %08x): DCS off\n", why,
        b2l_dsi_rd(B2L_DSIM_LINK1), b2l_dsi_rd(B2L_DSIM_FIFOCTRL), b2l_dsi_rd(B2L_DSIM_INTSRC));
}

/* bounded and slept: the thread can be preempted, interrupts stay on */
static bool b2l_dsi_wait(uint32_t off, uint32_t bit, unsigned ms)
{
    for (unsigned i = 0; i <= ms; i++) {
        if (b2l_dsi_rd(off) & bit) return true;
        IOSleep(1);
    }
    return false;
}

static bool b2l_dsi_header(uint32_t id, uint32_t d0, uint32_t d1, bool bta)
{
    if (b2l_dsi_dead) return false;
    if (!b2l_dsi_wait(B2L_DSIM_FIFOCTRL, 1u << 10, 50)) { b2l_dsi_fail("header FIFO not empty"); return false; }
    b2l_dsi_wr(B2L_DSIM_INTSRC, 1u << 28);
    b2l_dsi_wr(B2L_DSIM_PKTHDR, (bta ? 1u << 24 : 0) | (d1 & 0xff) << 16 | (d0 & 0xff) << 8 | (id & 0xff));
    if (!b2l_dsi_wait(B2L_DSIM_FIFOCTRL, 1u << 10, 50)) { b2l_dsi_fail("packet not sent in 50 ms"); return false; }
    return true;
}

static bool b2l_dsi_allowed(const uint8_t *b, unsigned n)
{
    if (n == 3 && b[0] == 0xF0) return (b[1] == 0x5A && b[2] == 0x5A) || (b[1] == 0xA5 && b[2] == 0xA5);
    if (n == 3 && b[0] == 0xB1) { uint32_t v = (uint32_t)b[1] << 8 | b[2]; return v >= B2L_AOR_MIN && v <= B2L_AOR_MAX; }
    return n == 2 && b[0] == 0xF7 && b[1] == 0x03;
}

/* DCS long write 0x39: payload 4 bytes per word, low byte first, then the header with the length */
static bool b2l_dsi_cmd(const uint8_t *b, unsigned n)
{
    if (!b2l_dsi_allowed(b, n)) { b2l_dsi_fail("command outside the closed list refused"); return false; }
    if (b2l_dsi_dead || !b2l_dsi_wait(B2L_DSIM_FIFOCTRL, 1u << 8, 50)) { if (!b2l_dsi_dead) b2l_dsi_fail("payload FIFO not empty"); return false; }
    for (unsigned i = 0; i < n; i += 4) {
        uint32_t w = 0;
        for (unsigned j = 0; j < 4 && i + j < n; j++) w |= (uint32_t)b[i + j] << (8 * j);
        b2l_dsi_wr(B2L_DSIM_PAYLOAD, w);
    }
    return b2l_dsi_header(0x39, n, 0, false);
}

/* returns the number of bytes read, or -1 */
static int b2l_dsi_read(uint8_t reg, unsigned n, uint8_t *out)
{
    if (b2l_dsi_dead) return -1;
    for (int i = 0; !(b2l_dsi_rd(B2L_DSIM_FIFOCTRL) & (1u << 12)) && i < 64; i++) (void)b2l_dsi_rd(B2L_DSIM_RXFIFO);
    b2l_dsi_wr(B2L_DSIM_INTSRC, 1u << 18);
    if (!b2l_dsi_header(0x37, n, 0, false) || !b2l_dsi_header(0x06, reg, 0, true)) return -1;
    if (!b2l_dsi_wait(B2L_DSIM_INTSRC, 1u << 18, 100)) { b2l_dsi_fail("no read answer in 100 ms"); return -1; }
    int got = -1;
    for (int k = 0; !(b2l_dsi_rd(B2L_DSIM_FIFOCTRL) & (1u << 12)) && k < 16; k++) {
        uint32_t w = b2l_dsi_rd(B2L_DSIM_RXFIFO);
        switch (w & 0xff) {
        case 0x02: b2l_diag_record("B2L PANEL: read %02x: error report %04x\n", reg, (w >> 8) & 0xffff); return -1;
        case 0x08: break;   /* end of transmission */
        case 0x21: case 0x11: if (n >= 1) { out[0] = (uint8_t)(w >> 8); got = 1; } break;
        case 0x22: case 0x12: if (n >= 2) { out[0] = (uint8_t)(w >> 8); out[1] = (uint8_t)(w >> 16); got = 2; } break;
        case 0x1c: case 0x1a: {
            unsigned size = (w >> 8) & 0xffff;
            if (size > n) { b2l_dsi_fail("read longer than asked"); return -1; }
            for (unsigned i = 0; i < size; i += 4) {
                uint32_t p = b2l_dsi_rd(B2L_DSIM_RXFIFO);
                for (unsigned j = 0; j < 4 && i + j < size; j++) out[i + j] = (uint8_t)(p >> (8 * j));
            }
            got = (int)size;
            break;
        }
        default: b2l_dsi_fail("unknown answer type"); return -1;
        }
    }
    return got;
}

static const uint8_t b2l_key2_on[3] = { 0xF0, 0x5A, 0x5A }, b2l_key2_off[3] = { 0xF0, 0xA5, 0xA5 }, b2l_gamma_upd[2] = { 0xF7, 0x03 };

/* writes the AOR, latches it, reads it back; level-2 key closed again in every case */
static bool b2l_panel_set_aor(uint32_t v, bool say = true)
{
    uint8_t aor[3] = { 0xB1, (uint8_t)(v >> 8), (uint8_t)v }, rb[2] = { 0, 0 };
    bool ok = b2l_dsi_cmd(b2l_key2_on, 3) && b2l_dsi_cmd(aor, 3) && b2l_dsi_cmd(b2l_gamma_upd, 2) &&
        b2l_dsi_read(0xB1, 2, rb) == 2 && ((uint32_t)rb[0] << 8 | rb[1]) == v;
    bool was_dead = b2l_dsi_dead;
    b2l_dsi_dead = false;   /* the key is closed even after a failure */
    b2l_dsi_cmd(b2l_key2_off, 3);
    b2l_dsi_dead = b2l_dsi_dead || was_dead || !ok;
    if (say || !ok) b2l_diag_record("B2L PANEL: AOR %04x -> read back %02x%02x %s\n", v, rb[0], rb[1], ok ? "ok" : "FAILED");
    return ok;
}

/* B1L-0372, step 4: iOS's brightness (nits, 16.16) -> AOR. Emission time = vtotal 3064 - AOR lines (Samsung
 * beyond1 a3 s0 dimming: vtotal 3064).
 * B1L-0373: 0372 mapped 420 nits to the bootloader's AOR and never went brighter, so part of the slider did nothing.
 * Now the whole range iOS uses (BrightnessMilliNits 1..1060 nits) spans the whole normal AOR table, on a log scale:
 * 1 nit -> 0x0BC9 (darkest table level, the floor, never 0), 1060 nits -> 0x0014 (Samsung's top normal level, no
 * HBM). 64 points, nits_i = 1060^(i/63), lines_i = 47 * (3044/47)^(i/63), linear in between. Gamma untouched.
 * shortcut: the bootloader's gamma sets the top luminance; drive the gamma too if the top is too dim. */
#define B2L_AOR_VTOTAL 3064u
static const uint32_t b2l_bl_nits[64] = {
    65536u, 73198u, 81756u, 91315u, 101991u, 113916u, 127234u, 142110u,
    158725u, 177283u, 198010u, 221161u, 247018u, 275899u, 308156u, 344184u,
    384425u, 429371u, 479571u, 535641u, 598266u, 668213u, 746339u, 833598u,
    931059u, 1039916u, 1161499u, 1297298u, 1448973u, 1618382u, 1807598u, 2018936u,
    2254983u, 2518628u, 2813097u, 3141994u, 3509346u, 3919646u, 4377918u, 4889769u,
    5461464u, 6099999u, 6813190u, 7609765u, 8499472u, 9493201u, 10603114u, 11842793u,
    13227412u, 14773915u, 16501231u, 18430498u, 20585328u, 22992093u, 25680250u, 28682696u,
    32036178u, 35781738u, 39965215u, 44637810u, 49856709u, 55685784u, 62196374u, 69468160u
};
static const uint16_t b2l_bl_aor[64] = {
    3017, 3014, 3010, 3007, 3003, 2999, 2994, 2989, 2984, 2979, 2973, 2967, 2960, 2953, 2945, 2937,
    2928, 2919, 2909, 2899, 2887, 2875, 2862, 2849, 2834, 2818, 2801, 2783, 2764, 2743, 2722, 2698,
    2673, 2646, 2618, 2587, 2554, 2520, 2482, 2443, 2400, 2355, 2306, 2254, 2199, 2139, 2076, 2009,
    1936, 1859, 1777, 1689, 1594, 1494, 1386, 1272, 1149, 1018, 878, 728, 568, 398, 215, 20
};
static IOLock *b2l_bl_req_lock;
static uint32_t b2l_bl_req, b2l_bl_req_seq;   /* nits 16.16, bumped on every request */
static bool b2l_bl_req_live;                    /* a brightness-nits came: the NVRAM value no longer counts */

static uint32_t b2l_aor_for(uint32_t nits16)
{
    if (nits16 <= b2l_bl_nits[0]) return b2l_bl_aor[0];
    for (unsigned i = 1; i < 64; i++) {
        if (nits16 <= b2l_bl_nits[i]) {
            uint32_t lo = b2l_bl_nits[i - 1], hi = b2l_bl_nits[i], a = b2l_bl_aor[i - 1], b = b2l_bl_aor[i];
            uint32_t aor = a - (uint32_t)((uint64_t)(a - b) * (nits16 - lo) / (hi - lo));
            return aor < B2L_AOR_MIN ? B2L_AOR_MIN : aor > B2L_AOR_MAX ? B2L_AOR_MAX : aor;
        }
    }
    return b2l_bl_aor[63];
}

/* called by AppleARMBacklight::setProperties (backboardd): stores the wish, never touches the panel */
extern "C" void b2l_panel_request(uint32_t nits16, bool from_nvram)
{
    if (b2l_bl_req_lock == NULL) return;
    IOLockLock(b2l_bl_req_lock);
    if (!from_nvram || !b2l_bl_req_live) {
        b2l_bl_req = nits16;
        b2l_bl_req_seq++;
        b2l_bl_req_live = b2l_bl_req_live || !from_nvram;
        IOLockWakeup(b2l_bl_req_lock, &b2l_bl_req_seq, false);
    }
    IOLockUnlock(b2l_bl_req_lock);
}

/* fade: each step goes a quarter of the way (at least one line), ~16 ms apart, toward the newest wish */
static void b2l_panel_link(uint32_t start)
{
    uint32_t cur = start, seen = 0, logged = 0;
    for (;;) {
        IOLockLock(b2l_bl_req_lock);
        while (b2l_bl_req_seq == seen) IOLockSleep(b2l_bl_req_lock, &b2l_bl_req_seq, THREAD_UNINT);
        seen = b2l_bl_req_seq;
        uint32_t nits16 = b2l_bl_req;
        IOLockUnlock(b2l_bl_req_lock);
        uint32_t tgt = b2l_aor_for(nits16), steps = 0;
        IOLockLock(b2l_dsi_lock);
        while (cur != tgt && !b2l_dsi_dead) {
            uint32_t d = (cur > tgt ? cur - tgt : tgt - cur) / 4;
            if (d == 0) d = 1;
            cur = cur > tgt ? cur - d : cur + d;
            if (!b2l_panel_set_aor(cur, false)) break;
            steps++;
            IOLockUnlock(b2l_dsi_lock);
            IOSleep(16);
            IOLockLock(b2l_dsi_lock);
            if (__atomic_load_n(&b2l_bl_req_seq, __ATOMIC_RELAXED) != seen) break;   /* newer wish: re-aim */
        }
        bool dead = b2l_dsi_dead;
        IOLockUnlock(b2l_dsi_lock);
        if ((cur == tgt && logged < 200) || dead) {   /* B1L-0373: only where a fade ends */
            logged++;
            b2l_diag_record("B2L PANEL: %u.%02u nits -> AOR %04x (now %04x, %u steps)%s\n", nits16 >> 16,
                (nits16 & 0xffff) * 100 >> 16, tgt, cur, steps, dead ? ": DCS off, brightness left as is" : "");
        }
        if (dead) return;
    }
}

static void b2l_panel_thread(void *, wait_result_t)
{
    uint32_t delay = 2, test = 0;   /* B1L-0372: the 0371 test is off by default */
    PE_parse_boot_argn("b2l_panel_delay", &delay, sizeof(delay));
    PE_parse_boot_argn("b2l_panel_test", &test, sizeof(test));
    IOSleep(delay * 1000);
    IOLockLock(b2l_dsi_lock);
    uint32_t link3 = b2l_dsi_rd(B2L_DSIM_LINK3), cfg = b2l_dsi_rd(B2L_DSIM_CONFIG);
    b2l_diag_record("B2L PANEL: DSIM link0 %08x link1 %08x link3 %08x config %08x intsrc %08x intmsk %08x fifo %08x\n",
        b2l_dsi_rd(B2L_DSIM_LINK0), b2l_dsi_rd(B2L_DSIM_LINK1), link3, cfg, b2l_dsi_rd(B2L_DSIM_INTSRC),
        b2l_dsi_rd(B2L_DSIM_INTMSK), b2l_dsi_rd(B2L_DSIM_FIFOCTRL));
    /* B1L-0372: LINK_STATUS3 bit0 read 0 at 0371 with the panel lit: on the 9820 the PLL lock is a PHY register
     * (dsim_reg_is_pll_stable reads DSIM_PHY_PLL_STAT0), not mapped here. Checked instead: command mode, both
     * transmit FIFOs empty, and the clock lane not in ULPS (DPHY_STATUS bit9). */
    uint32_t fifo = b2l_dsi_rd(B2L_DSIM_FIFOCTRL), dphy = b2l_dsi_rd(0x1c);
    b2l_diag_record("B2L PANEL: DSIM mipi status %08x dphy status %08x\n", b2l_dsi_rd(0x18), dphy);
    (void)link3;
    if (cfg & (1u << 18)) b2l_dsi_fail("video mode, not the command mode expected");
    else if ((fifo & 0x500) != 0x500) b2l_dsi_fail("transmit FIFOs not empty");
    else if (dphy & (1u << 9)) b2l_dsi_fail("clock lane in ULPS");
    uint8_t pm = 0, id[3] = { 0, 0, 0 }, aor[2] = { 0, 0 };
    int npm = b2l_dsi_read(0x0A, 1, &pm), nid = b2l_dsi_read(0x04, 3, id);
    b2l_diag_record("B2L PANEL: power mode (%d) %02x, id (%d) %02x %02x %02x\n", npm, pm, nid, id[0], id[1], id[2]);
    int naor = -1;
    if (b2l_dsi_cmd(b2l_key2_on, 3)) { naor = b2l_dsi_read(0xB1, 2, aor); b2l_dsi_cmd(b2l_key2_off, 3); }
    uint32_t start = (uint32_t)aor[0] << 8 | aor[1];
    b2l_diag_record("B2L PANEL: start AOR (%d) %04x\n", naor, start);
    if (!test || b2l_dsi_dead || naor != 2 || start < B2L_AOR_MIN || start > B2L_AOR_MAX) {
        IOLockUnlock(b2l_dsi_lock);
        if (!b2l_dsi_dead && naor == 2 && start >= B2L_AOR_MIN && start <= B2L_AOR_MAX) {
            b2l_diag_record("B2L PANEL: slider linked, start AOR %04x, range %04x..%04x\n", start, B2L_AOR_MIN, B2L_AOR_MAX);
            b2l_panel_link(start);
            return;
        }
        b2l_diag_record("B2L PANEL: slider not linked (start value unknown): the panel keeps the bootloader's brightness\n");
        return;
    }
    uint32_t medium = start == B2L_AOR_MEDIUM ? 0x0476u : B2L_AOR_MEDIUM;
    bool ok = b2l_panel_set_aor(medium);
    IOLockUnlock(b2l_dsi_lock);
    if (ok) IOSleep(3000);
    IOLockLock(b2l_dsi_lock);
    bool back = false;
    for (int t = 0; t < 3 && !back; t++) { b2l_dsi_dead = false; back = b2l_panel_set_aor(start); }   /* the start value is always put back */
    b2l_dsi_dead = b2l_dsi_dead || !ok || !back;
    b2l_diag_record("B2L PANEL: test %s, start AOR %s\n", ok ? "done" : "FAILED", back ? "restored" : "NOT restored");
    IOLockUnlock(b2l_dsi_lock);
}

static void b2l_panel_start(void)
{
    uint32_t on = 1;
    PE_parse_boot_argn("b2l_panel", &on, sizeof(on));
    if (!on) { b2l_diag_record("B2L PANEL: off (b2l_panel=0)\n"); return; }
    IOMemoryDescriptor *md = IOMemoryDescriptor::withPhysicalAddress(B2L_DSIM_PA, 0x1000, kIODirectionInOut);
    b2l_dsi_map = md ? md->map(kIOMapInhibitCache) : NULL;
    OSSafeReleaseNULL(md);
    b2l_dsi = b2l_dsi_map ? (volatile uint8_t *)b2l_dsi_map->getVirtualAddress() : NULL;
    b2l_dsi_lock = IOLockAlloc();
    b2l_bl_req_lock = IOLockAlloc();
    thread_t th;
    if (b2l_dsi == NULL || b2l_dsi_lock == NULL || b2l_bl_req_lock == NULL || kernel_thread_start(b2l_panel_thread, nullptr, &th) != KERN_SUCCESS) {
        b2l_diag_record("B2L PANEL: DSIM not mapped or no thread: off\n");
        return;
    }
    thread_deallocate(th);
}
#endif
