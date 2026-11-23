// B1L-0371: host check of B2LPanel.h -- closed list, payload packing, read parsing. Fake DSIM registers.
#include <cstdint>
#include <cstdio>
#include <cstdarg>
#include <cassert>
#include <vector>
typedef int wait_result_t; typedef void *thread_t; typedef int kern_return_t;
struct IOLock {}; struct IOMemoryMap { uint64_t getVirtualAddress() { return 0; } };
static void IOSleep(unsigned) {}
static void b2l_diag_record(const char *f, ...) { va_list a; va_start(a, f); vprintf(f, a); va_end(a); }
static void IOLockLock(IOLock *) {} static void IOLockUnlock(IOLock *) {}
static bool PE_parse_boot_argn(const char *, void *, int) { return false; }
#define OSSafeReleaseNULL(x)
enum { kIODirectionInOut = 3, kIOMapInhibitCache = 0, KERN_SUCCESS = 0 };
struct IOMemoryDescriptor { static IOMemoryDescriptor *withPhysicalAddress(uint64_t, unsigned, int) { return nullptr; } IOMemoryMap *map(int) { return nullptr; } };
static IOLock the_lock;
static IOLock *IOLockAlloc() { return &the_lock; }
static void IOLockSleep(IOLock *, void *, int) {}
static void IOLockWakeup(IOLock *, void *, bool) {}
enum { THREAD_UNINT = 0 };
static kern_return_t kernel_thread_start(void (*)(void *, wait_result_t), void *, thread_t *) { return 1; }
static void thread_deallocate(thread_t) {}
static uint32_t regs[0x400];
#include "../xnu-overlay/iokit/Kernel/arm/B2LPanel.h"
int main() {
    b2l_dsi = (volatile uint8_t *)regs;
    regs[B2L_DSIM_FIFOCTRL / 4] = (1u << 12) | (1u << 10) | (1u << 8);
    uint8_t ok1[3] = { 0xF0, 0x5A, 0x5A }, ok2[3] = { 0xB1, 0x06, 0x01 }, ok3[2] = { 0xF7, 0x03 };
    uint8_t bad[][3] = { { 0x51, 0x01, 0x00 }, { 0xB1, 0x00, 0x00 }, { 0xB1, 0x0B, 0xCA }, { 0x53, 0x20, 0 }, { 0xF0, 0x5A, 0xA5 } };
    assert(b2l_dsi_allowed(ok1, 3) && b2l_dsi_allowed(ok2, 3) && b2l_dsi_allowed(ok3, 2));
    for (auto &b : bad) assert(!b2l_dsi_allowed(b, 3));
    assert(!b2l_dsi_allowed(ok3, 3) && !b2l_dsi_allowed(ok2, 4));
    for (unsigned v = 0; v < 0x10000; v++) { uint8_t c[3] = { 0xB1, (uint8_t)(v >> 8), (uint8_t)v }; assert(b2l_dsi_allowed(c, 3) == (v >= 0x14 && v <= 0xBC9)); }
    assert(b2l_dsi_cmd(ok2, 3));
    assert(regs[B2L_DSIM_PAYLOAD / 4] == 0x0106B1 && regs[B2L_DSIM_PKTHDR / 4] == 0x0339);
    regs[B2L_DSIM_PAYLOAD / 4] = 0xdead;
    assert(!b2l_dsi_cmd(bad[0], 3) && b2l_dsi_dead && regs[B2L_DSIM_PAYLOAD / 4] == 0xdead);   // refused before the FIFO
    b2l_dsi_dead = false;
    regs[B2L_DSIM_FIFOCTRL / 4] = (1u << 12);   // header FIFO never empties
    assert(!b2l_dsi_cmd(ok1, 3) && b2l_dsi_dead);
    // B1L-0373: nits -> AOR over the whole slider, log scale
    uint32_t prev = 0xffff;
    for (uint64_t n = 0; n <= (2200ull << 16); n += 331) {
        uint32_t a = b2l_aor_for((uint32_t)n);
        assert(a >= 0x14 && a <= 0xBC9 && a <= prev);   // inside the normal table, floor kept, monotonic
        prev = a;
    }
    assert(b2l_aor_for(0) == 0xBC9 && b2l_aor_for(1u << 16) == 0xBC9 && b2l_aor_for(1060u << 16) == 0x14 && b2l_aor_for(2175u << 16) == 0x14);
    { uint32_t m = 3064 - b2l_aor_for(33u << 16); assert(m > 360 && m < 400); }   // ~sqrt(1060) nits -> ~sqrt(47*3044) = 378 lines
    // the NVRAM value counts only until a live brightness-nits comes
    b2l_bl_req_lock = IOLockAlloc();
    b2l_panel_request(155u << 16, true); assert(b2l_bl_req == (155u << 16) && b2l_bl_req_seq == 1);
    b2l_panel_request(80u << 16, false); assert(b2l_bl_req == (80u << 16) && b2l_bl_req_seq == 2);
    b2l_panel_request(155u << 16, true); assert(b2l_bl_req == (80u << 16) && b2l_bl_req_seq == 2);
    puts("test-panel-dcs: ok");
}
