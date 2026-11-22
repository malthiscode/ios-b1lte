/* SPDX-License-Identifier: BSD-2-Clause
 * B1L-0354, battery step 1: the MAX77705's fuel gauge and charger are read and logged.
 * B1L-0355, step 2: given to iOS as an IOPMPowerSource (powerd), after Jesvi-iOS RedwoodBattery.cpp (APSL 2.0).
 * No temperature is published until it is measured (step 3).
 *
 * I2C by GPIO on hsi2c_45's own pins, pinctrl@15C30000: SCL gpm12-0 (+0x180), SDA gpm13-0 (+0x1A0), one register
 * bank each. This needs none of USI_CMGP3's clocks, mode or timings, whose state after S-Boot is unknown (same choice
 * as B2LAudio.h for the amplifiers, and as Jesvi-iOS for this chip on the S20).
 *
 * The only message this file can send is: START, address+W, one register from b2l_batt_regs, repeated START,
 * address+R, read, STOP. No data byte is ever sent. A released line is checked high before it is used, so a held
 * SDA cannot turn the repeated START into a register write. The interrupt registers (clear on read) are not listed.
 *
 * Facts from the phone's DTB and from LineageOS's Exynos 9820 kernel a412cfaf (GPL: facts only, no code copied):
 * max77705.c, max77705-private.h, max77705_fuelgauge.c, pinctrl-exynos-arm64.c.
 * Kill switch: b2l_batt=0.
 */
#ifndef B2L_BATTERY_H
#define B2L_BATTERY_H
#include <IOKit/IOPlatformExpert.h>
#include <IOKit/pwr_mgt/IOPM.h>
#include <IOKit/pwr_mgt/IOPMPowerSource.h>

static uint32_t b2l_batt_rd(volatile uint8_t *p, uint32_t off) { return *(volatile uint32_t *)(p + off); }
static void b2l_batt_wr(volatile uint8_t *p, uint32_t off, uint32_t v)
{
    *(volatile uint32_t *)(p + off) = v;
    __asm__ volatile ("dsb sy" ::: "memory");
}
static void b2l_batt_delay(unsigned us) { IODelay(us); }

/* B2L BATT CORE BEGIN: compiled as is by tests/test-battery-i2c.py */
#define B2L_BATT_SCL 0x180u
#define B2L_BATT_SDA 0x1A0u
#define B2L_BATT_FG 0x36u
#define B2L_BATT_CHG 0x69u
#define B2L_BATT_TOP 0x66u

enum { BATT_ID, BATT_REV, BATT_STATUS, BATT_REPCAP, BATT_SOC, BATT_FULLREP, BATT_FULLNOM, BATT_VCELL, BATT_CURRENT,
       BATT_AVG, BATT_INT_OK, BATT_DTLS0, BATT_DTLS1, BATT_COUNT };
static const struct { uint8_t addr, reg, len; } b2l_batt_regs[BATT_COUNT] = {
    { B2L_BATT_TOP, 0x00, 1 }, { B2L_BATT_TOP, 0x01, 1 },
    { B2L_BATT_FG, 0x00, 2 }, { B2L_BATT_FG, 0x05, 2 }, { B2L_BATT_FG, 0x06, 2 }, { B2L_BATT_FG, 0x35, 2 },
    { B2L_BATT_FG, 0x23, 2 }, { B2L_BATT_FG, 0x09, 2 }, { B2L_BATT_FG, 0x0a, 2 }, { B2L_BATT_FG, 0x0b, 2 },
    { B2L_BATT_CHG, 0xb2, 1 }, { B2L_BATT_CHG, 0xb3, 1 }, { B2L_BATT_CHG, 0xb4, 1 },
};
enum { BATT_OK, BATT_NACK, BATT_STUCK };
struct b2l_batt_bus { volatile uint8_t *pins; uint32_t scl_con, scl_dat, sda_con, sda_dat; bool stuck; };

static void b2l_batt_mode(struct b2l_batt_bus *b, uint32_t pin, unsigned low)
{
    uint32_t con = b2l_batt_rd(b->pins, pin);
    b2l_batt_wr(b->pins, pin, (con & ~0xfu) | low);      /* 1: output, whose data is 0; 0: released */
    b2l_batt_delay(5);
}
static bool b2l_batt_high(struct b2l_batt_bus *b, uint32_t pin) { return (b2l_batt_rd(b->pins, pin + 4) & 1u) != 0; }
static void b2l_batt_scl_rel(struct b2l_batt_bus *b)
{
    b2l_batt_mode(b, B2L_BATT_SCL, 0);
    for (unsigned i = 0; i < 1000 && !b2l_batt_high(b, B2L_BATT_SCL); ++i) b2l_batt_delay(1);
    if (!b2l_batt_high(b, B2L_BATT_SCL)) b->stuck = true;
}
static bool b2l_batt_start(struct b2l_batt_bus *b)
{
    b2l_batt_mode(b, B2L_BATT_SDA, 0);
    b2l_batt_scl_rel(b);
    if (!b2l_batt_high(b, B2L_BATT_SDA)) b->stuck = true;
    if (b->stuck) return false;
    uint32_t con = b2l_batt_rd(b->pins, B2L_BATT_SDA);
    b2l_batt_wr(b->pins, B2L_BATT_SDA, (con & ~0xfu) | 1);
    /* SCL pulled low over that edge: no START was seen, and address+R would be written as data */
    bool seen = b2l_batt_high(b, B2L_BATT_SCL);
    b2l_batt_delay(5);
    if (!seen || !b2l_batt_high(b, B2L_BATT_SCL)) { b->stuck = true; return false; }
    b2l_batt_mode(b, B2L_BATT_SCL, 1);
    return true;
}
static bool b2l_batt_put(struct b2l_batt_bus *b, uint8_t v)
{
    for (int i = 7; i >= 0; --i) {
        bool one = (v >> i) & 1;
        b2l_batt_mode(b, B2L_BATT_SDA, one ? 0 : 1);
        b2l_batt_scl_rel(b);
        if (one && !b2l_batt_high(b, B2L_BATT_SDA)) b->stuck = true;
        if (b->stuck) return false;
        b2l_batt_mode(b, B2L_BATT_SCL, 1);
    }
    b2l_batt_mode(b, B2L_BATT_SDA, 0);
    b2l_batt_scl_rel(b);
    if (b->stuck) return false;
    bool ack = !b2l_batt_high(b, B2L_BATT_SDA);
    b2l_batt_mode(b, B2L_BATT_SCL, 1);
    return ack;
}
static uint8_t b2l_batt_get(struct b2l_batt_bus *b, bool more)
{
    uint8_t v = 0;
    b2l_batt_mode(b, B2L_BATT_SDA, 0);
    for (unsigned i = 0; i < 8; ++i) {
        b2l_batt_scl_rel(b);
        if (b->stuck) return 0;
        v = (uint8_t)((v << 1) | b2l_batt_high(b, B2L_BATT_SDA));
        b2l_batt_mode(b, B2L_BATT_SCL, 1);
    }
    b2l_batt_mode(b, B2L_BATT_SDA, more ? 1 : 0);
    b2l_batt_scl_rel(b);
    if (b->stuck) return 0;
    b2l_batt_mode(b, B2L_BATT_SCL, 1);
    return v;
}
static int b2l_batt_read(struct b2l_batt_bus *b, unsigned which, uint16_t *out)
{
    if (which >= BATT_COUNT) return BATT_NACK;
    const uint8_t addr = b2l_batt_regs[which].addr, len = b2l_batt_regs[which].len;
    bool ok = b2l_batt_start(b) && b2l_batt_put(b, (uint8_t)(addr << 1)) && b2l_batt_put(b, b2l_batt_regs[which].reg) &&
        b2l_batt_start(b) && b2l_batt_put(b, (uint8_t)((addr << 1) | 1));
    uint16_t v = 0;
    for (unsigned i = 0; ok && i < len; ++i) {
        v |= (uint16_t)(b2l_batt_get(b, i + 1 < len) << (8 * i));
        ok = !b->stuck;
    }
    if (!b->stuck) {
        b2l_batt_mode(b, B2L_BATT_SDA, 1);
        b2l_batt_scl_rel(b);
        b2l_batt_mode(b, B2L_BATT_SDA, 0);
        if (!b2l_batt_high(b, B2L_BATT_SDA)) b->stuck = true;
    }
    if (b->stuck) {                                      /* no more clocks: both lines let go */
        b2l_batt_mode(b, B2L_BATT_SDA, 0);
        b2l_batt_mode(b, B2L_BATT_SCL, 0);
        return BATT_STUCK;
    }
    if (!ok) return BATT_NACK;
    *out = v;
    return BATT_OK;
}
/* All registers, or none. The pins are given back as found unless the bus is stuck. */
static int b2l_batt_round(struct b2l_batt_bus *b, uint16_t out[BATT_COUNT], unsigned *at)
{
    *at = BATT_COUNT;
    b->stuck = false;
    b->scl_con = b2l_batt_rd(b->pins, B2L_BATT_SCL); b->scl_dat = b2l_batt_rd(b->pins, B2L_BATT_SCL + 4);
    b->sda_con = b2l_batt_rd(b->pins, B2L_BATT_SDA); b->sda_dat = b2l_batt_rd(b->pins, B2L_BATT_SDA + 4);
    b2l_batt_mode(b, B2L_BATT_SDA, 0);
    b2l_batt_mode(b, B2L_BATT_SCL, 0);
    if (!b2l_batt_high(b, B2L_BATT_SCL) || !b2l_batt_high(b, B2L_BATT_SDA)) return BATT_STUCK;
    b2l_batt_wr(b->pins, B2L_BATT_SCL + 4, b->scl_dat & ~1u);
    b2l_batt_wr(b->pins, B2L_BATT_SDA + 4, b->sda_dat & ~1u);
    uint16_t v[BATT_COUNT];
    int err = BATT_OK;
    for (unsigned i = 0; i < BATT_COUNT && err == BATT_OK; ++i) {
        err = b2l_batt_read(b, i, &v[i]);
        if (err != BATT_OK) *at = i;
    }
    if (err == BATT_STUCK) return err;
    if ((b->scl_con & 0xfu) == 1) b2l_batt_wr(b->pins, B2L_BATT_SCL + 4, b->scl_dat);
    if ((b->sda_con & 0xfu) == 1) b2l_batt_wr(b->pins, B2L_BATT_SDA + 4, b->sda_dat);
    b2l_batt_wr(b->pins, B2L_BATT_SCL, b->scl_con);      /* SCL first: no false START */
    b2l_batt_wr(b->pins, B2L_BATT_SDA, b->sda_con);
    if (err == BATT_OK) for (unsigned i = 0; i < BATT_COUNT; ++i) out[i] = v[i];
    return err;
}
/* fg_resistor = 2 in the phone's DTB: 312.5 uA and 1 mAh a step */
static int b2l_batt_ma(uint16_t raw) { return (int16_t)raw * 3125 / 10000; }
static unsigned b2l_batt_mv(uint16_t raw) { return (unsigned)raw * 5u / 64u; }     /* 78.125 uV a step */
static unsigned b2l_batt_centi(uint16_t raw) { return (unsigned)raw * 100u / 256u; }
static bool b2l_batt_sane(const uint16_t *v)
{
    unsigned mv = b2l_batt_mv(v[BATT_VCELL]);
    return !(v[BATT_STATUS] & 8u) && mv >= 2500 && mv <= 4600 && b2l_batt_centi(v[BATT_SOC]) <= 11000;
}
/* What iOS is told. Charger states from Samsung's max77705_get_charger_state: 0-2 charging, 3-4 full,
 * 5-7 not charging, 8/A/B discharging (B: its watchdog ran out, which nobody here may reset). */
struct b2l_batt_view { unsigned percent, mv, raw, full; int ma; bool external, charging, charged; };
static struct b2l_batt_view b2l_batt_view_of(const uint16_t *v)
{
    struct b2l_batt_view w;
    unsigned c = b2l_batt_centi(v[BATT_SOC]) / 100, state = v[BATT_DTLS1] & 0xfu;
    w.external = (v[BATT_INT_OK] & 0x60u) != 0;          /* CHGIN_OK or WCIN_OK */
    w.charging = w.external && state <= 2;
    w.charged = w.external && (state == 3 || state == 4);
    w.percent = w.charged ? 100 : c > 100 ? 100 : c;    /* whole percent down, as Android shows it */
    w.mv = b2l_batt_mv(v[BATT_VCELL]);
    w.ma = b2l_batt_ma(v[BATT_AVG]);
    w.raw = v[BATT_REPCAP];
    w.full = v[BATT_FULLREP];
    return w;
}
/* B2L BATT CORE END */

class B2LBatterySource : public IOPMPowerSource {
    OSDeclareDefaultStructors(B2LBatterySource);
public:
    void put(const char *key, OSObject *value)
    {
        const OSSymbol *k = OSSymbol::withCString(key);
        if (k && value) setPSProperty(k, value);
        OSSafeReleaseNULL(k);
    }
    void num(const char *key, long long value)
    {
        OSNumber *n = OSNumber::withNumber((unsigned long long)value, 32);
        put(key, n);
        OSSafeReleaseNULL(n);
    }
    void report(const struct b2l_batt_view &w)
    {
        setBatteryInstalled(true);
        setExternalConnected(w.external);
        setExternalChargeCapable(w.external);
        setIsCharging(w.charging);
        put("FullyCharged", w.charged ? kOSBooleanTrue : kOSBooleanFalse);
        put("AppleRawExternalConnected", w.external ? kOSBooleanTrue : kOSBooleanFalse);
        setCurrentCapacity(w.percent);
        setMaxCapacity(100);
        setVoltage(w.mv);
        setAmperage(w.ma);
        num("InstantAmperage", w.ma);
        num("AppleRawCurrentCapacity", w.raw);
        num("AppleRawMaxCapacity", w.full);
        num("NominalChargeCapacity", w.full);
        num("DesignCapacity", 3400);                       /* battery,battery_full_capacity in the phone's DTB */
        OSDictionary *adapter = OSDictionary::withCapacity(2);
        OSNumber *family = OSNumber::withNumber(w.external ? 0xe0004007ull : 0ull, 32);  /* USBUnknown, Disconnected */
        if (adapter && family) {
            adapter->setObject(kIOPMPSAdapterDetailsFamilyKey, family);
            put(kIOPMPSAdapterDetailsKey, adapter);
        }
        OSSafeReleaseNULL(family);
        OSSafeReleaseNULL(adapter);
        updateStatus();                                  /* tells powerd when something changed */
    }
};
OSDefineMetaClassAndStructors(B2LBatterySource, IOPMPowerSource);

/* Only valid readings get here: a failed read never becomes 0 %. */
static void b2l_batt_publish(const uint16_t *v)
{
    static B2LBatterySource *source;
    struct b2l_batt_view w = b2l_batt_view_of(v);
    if (source) { source->report(w); return; }
    IOService *platform = IOService::getPlatform();
    B2LBatterySource *made = OSTypeAlloc(B2LBatterySource);
    if (!platform || !made || !made->init()) {
        OSSafeReleaseNULL(made);
        b2l_diag_record("B2L BATT: power source not made\n");
        return;
    }
    made->report(w);                                     /* complete before powerd can see it */
    if (!made->attach(platform)) { made->release(); return; }
    made->setName("B2LBattery");
    if (!made->start(platform)) { made->detach(platform); made->release(); return; }
    made->registerService();
    source = made;
    b2l_diag_record("B2L BATT: given to iOS, %u%% %s\n", w.percent,
        w.charging ? "charging" : w.charged ? "charged" : w.external ? "on a cable, not charging" : "on battery");
}

static void b2l_batt_thread(void *, wait_result_t)
{
    IOSleep(20000);                                      /* never on the boot path */
    static struct b2l_batt_bus bus;
    bus.pins = (volatile uint8_t *)ml_io_map(0x15C30000, 0x1000);
    if (!bus.pins) {
        b2l_diag_record("B2L BATT: pin map failed, off\n");
        thread_terminate(current_thread());
        return;
    }
    b2l_diag_record("B2L BATT: start (read only) gpm12 con=%x dat=%x pud=%x | gpm13 con=%x dat=%x pud=%x\n",
        b2l_batt_rd(bus.pins, B2L_BATT_SCL), b2l_batt_rd(bus.pins, B2L_BATT_SCL + 4), b2l_batt_rd(bus.pins, B2L_BATT_SCL + 8),
        b2l_batt_rd(bus.pins, B2L_BATT_SDA), b2l_batt_rd(bus.pins, B2L_BATT_SDA + 4), b2l_batt_rd(bus.pins, B2L_BATT_SDA + 8));
    unsigned failures = 0, good = 0, quiet = 0, last = ~0u;
    for (;;) {
        uint16_t v[BATT_COUNT];
        unsigned at;
        uint64_t t0 = mach_absolute_time(), ns = 0;
        int err = b2l_batt_round(&bus, v, &at);
        absolutetime_to_nanoseconds(mach_absolute_time() - t0, &ns);
        if (err != BATT_OK) {
            ++failures;
            b2l_diag_record("B2L BATT: read %u failed (%s), attempt %u, scl=%x/%x sda=%x/%x\n", at,
                err == BATT_STUCK ? "bus held" : "no ack", failures,
                b2l_batt_rd(bus.pins, B2L_BATT_SCL), b2l_batt_rd(bus.pins, B2L_BATT_SCL + 4),
                b2l_batt_rd(bus.pins, B2L_BATT_SDA), b2l_batt_rd(bus.pins, B2L_BATT_SDA + 4));
            if (err == BATT_STUCK || failures >= 3) break;
        } else {
            failures = 0;
            if (b2l_batt_sane(v)) b2l_batt_publish(v);
            /* the percent, the cable and the charge state */
            unsigned key = (b2l_batt_centi(v[BATT_SOC]) / 100) << 16 | (v[BATT_INT_OK] & 0x60u) << 4 | (v[BATT_DTLS1] & 0xfu);
            if (good < 3 || key != last || ++quiet >= 40) {  /* else every 10 minutes */
                if (good < 3) ++good;
                quiet = 0;
                last = key;
                b2l_diag_record("B2L BATT: %s soc=%u.%02u%% %umV now=%dmA avg=%dmA cap=%u full=%u nom=%umAh usb=%u wireless=%u state=%x | "
                    "id=%02x rev=%02x status=%04x vcell=%04x int_ok=%02x dtls=%02x/%02x %lluus\n",
                    b2l_batt_sane(v) ? "ok" : "INVALID", b2l_batt_centi(v[BATT_SOC]) / 100, b2l_batt_centi(v[BATT_SOC]) % 100,
                    b2l_batt_mv(v[BATT_VCELL]), b2l_batt_ma(v[BATT_CURRENT]), b2l_batt_ma(v[BATT_AVG]),
                    v[BATT_REPCAP], v[BATT_FULLREP], v[BATT_FULLNOM], (v[BATT_INT_OK] >> 6) & 1, (v[BATT_INT_OK] >> 5) & 1,
                    v[BATT_DTLS1] & 0xf, v[BATT_ID], v[BATT_REV], v[BATT_STATUS], v[BATT_VCELL], v[BATT_INT_OK],
                    v[BATT_DTLS0], v[BATT_DTLS1], (unsigned long long)(ns / 1000));
            }
        }
        IOSleep(15000);
    }
    b2l_diag_record("B2L BATT: off until the next start\n");
    thread_terminate(current_thread());
}

static void b2l_battery_probe(void)
{
    uint32_t on = 1;
    PE_parse_boot_argn("b2l_batt", &on, sizeof(on));
    if (!on) return;
    thread_t th = THREAD_NULL;
    if (kernel_thread_start(b2l_batt_thread, nullptr, &th) == KERN_SUCCESS) thread_deallocate(th);
    else b2l_diag_record("B2L BATT: thread start failed\n");
}
#endif
