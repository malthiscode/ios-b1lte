#!/usr/bin/env python3
# SPDX-License-Identifier: BSD-2-Clause
# B1L-0354/0355: B2LBattery.h's own I2C core against a simulated MAX77705, with a line held at every possible access.
# A register write (a data byte after the register number) must be impossible, whatever fails.
import pathlib, subprocess, tempfile

h = (pathlib.Path(__file__).resolve().parent.parent / 'xnu-overlay/iokit/Kernel/arm/B2LBattery.h').read_text()
core = h[h.index('/* B2L BATT CORE BEGIN'):h.index('/* B2L BATT CORE END */')]

test = r'''#include <cassert>
#include <cstdint>
#include <cstdio>
#include <initializer_list>
static uint32_t b2l_batt_rd(volatile uint8_t *, uint32_t);
static void b2l_batt_wr(volatile uint8_t *, uint32_t, uint32_t);
static void b2l_batt_delay(unsigned);
''' + core + r'''
enum { IDLE, DEV, REG, DATA, SEND };
static uint32_t mem[0x200];                 // the pin registers
static uint32_t offs[2], pins[2], other;    // the bus under test; other: the bank's other pins at the start
static uint32_t nib(int l) { return (mem[offs[l]] >> (4 * pins[l])) & 0xf; }
static int st, bits, byte_, ackPhase, pull, wasC, wasD, index_, reg_, addr_, writes, nackAt, acked, badRead;
static long op, faultAt, faultLen;
static int faultLine;
static long worst, wrote;
static int wreg = -1, wval = -1;

static bool held(int l) { return faultLine == l && op >= faultAt && op < faultAt + faultLen; }
static bool line(int l)
{
	if (held(l)) return false;
	if (nib(l) == 1) return (mem[offs[l] + 4] >> pins[l]) & 1;
	return !(l == 1 && pull);
}
static uint8_t value(int i) { return (uint8_t)(0xa5 ^ addr_ ^ reg_ ^ (i * 0x5a)); }
static void sendBit() { pull = !((value(index_) >> (7 - bits)) & 1); }
static void step()
{
	int c = line(0), d = line(1);
	if (c && wasC && d != wasD) {
		if (!d) { st = DEV; bits = byte_ = ackPhase = 0; } else st = IDLE;
		pull = 0;
	} else if (c && !wasC && st != IDLE) {
		if (ackPhase == 1 && st == SEND) { if (d) ackPhase = 3; }
		else if (!ackPhase) {
			if (st != SEND) byte_ = byte_ << 1 | d;
			++bits;
		}
	} else if (!c && wasC && st != IDLE) {
		if (ackPhase == 3) { st = IDLE; pull = ackPhase = 0; }
		else if (ackPhase) {
			ackPhase = bits = 0; pull = 0;
			if (st == DEV) {
				if (byte_ & 1) {
					bool listed = false;
					for (auto &r : b2l_batt_regs) listed |= r.addr == addr_ && r.reg == reg_;
					badRead |= !listed;
					st = SEND; index_ = 0; sendBit();
				} else st = REG;
			} else if (st == REG) st = DATA;
			else if (st == SEND) { ++index_; sendBit(); }
			byte_ = 0;
		} else if (bits == 8) {
			ackPhase = 1; pull = 0;
			if (st != SEND) {
				bool ok = true;
				if (st == DEV) { addr_ = byte_ >> 1; ok = addr_ == 0x36 || addr_ == 0x69 || addr_ == 0x66 || addr_ == 0x59; }
				else if (st == REG) reg_ = byte_;
				else if (st == DATA) { ++writes; wreg = reg_; wval = byte_; }
				if (!ok || ++acked == nackAt) { st = IDLE; ackPhase = 0; } else pull = 1;
			}
		} else if (st == SEND) sendBit();
	}
	wasC = c; wasD = line(1);
}
static uint32_t b2l_batt_rd(volatile uint8_t *, uint32_t off)
{
	++op; step();
	assert((off & ~4u) == offs[0] || (off & ~4u) == offs[1]);
	if (!(off & 4)) return mem[off];
	uint32_t v = mem[off];
	for (int l = 0; l < 2; ++l) if (offs[l] == (off & ~4u)) v = (v & ~(1u << pins[l])) | ((uint32_t)line(l) << pins[l]);
	return v;
}
static void b2l_batt_wr(volatile uint8_t *, uint32_t off, uint32_t v)
{
	++op; step();
	assert((off & ~4u) == offs[0] || (off & ~4u) == offs[1]);   // nothing but the two pins' banks
	uint32_t mine = 0;
	for (int l = 0; l < 2; ++l) if (offs[l] == (off & ~4u)) mine |= (off & 4) ? 1u << pins[l] : 0xfu << (4 * pins[l]);
	assert((v & ~mine) == (mem[off] & ~mine));                     // the bank's other pins never change
	mem[off] = v;
	step();
}
static void b2l_batt_delay(unsigned us) { while (us--) { ++op; step(); } }
static uint32_t start[0x200];
static int run(uint16_t *v, uint32_t boot, int nack = 0, long at = -1, long len = 0, int which = 0)
{
	for (int l = 0; l < 2; ++l) {
		mem[offs[l]] = (other & ~(0xfu << (4 * pins[l]))) | 0;
		mem[offs[l] + 4] = other & ~(1u << pins[l]);
	}
	for (int l = 0; l < 2; ++l) { mem[offs[l]] |= (boot & 0xf) << (4 * pins[l]); mem[offs[l] + 4] |= 1u << pins[l]; }
	for (int l = 0; l < 2; ++l) { start[offs[l]] = mem[offs[l]]; start[offs[l] + 4] = mem[offs[l] + 4]; }
	st = IDLE; bits = byte_ = ackPhase = pull = writes = badRead = acked = 0;
	nackAt = nack; op = 0; faultAt = at; faultLen = len; faultLine = which; wasC = wasD = 1;
	b2l_batt_bus b = {};
	unsigned bad;
	int e = b2l_batt_round(&b, v, &bad);
	if (writes) { if (len > worst) worst = len; ++wrote; }
	if (e == BATT_STUCK) assert(!nib(0) && !nib(1));                       // both lines let go
	else for (int l = 0; l < 2; ++l) {                                      // as found
		assert(mem[offs[l]] == start[offs[l]]);
		if ((boot & 0xf) == 1) assert(mem[offs[l] + 4] == start[offs[l] + 4]);
	}
	return e;
}
static void suite(long &cases)
{
	uint16_t v[32];
	for (uint32_t boot : { 0u, 2u, 1u, 0x32u }) {
		assert(run(v, boot) == BATT_OK && !badRead);
		for (unsigned i = 0; i < BATT_COUNT; ++i) {
			addr_ = b2l_batt_regs[i].addr; reg_ = b2l_batt_regs[i].reg;
			assert(v[i] == (b2l_batt_regs[i].len == 2 ? value(0) | value(1) << 8 : value(0)));
		}
	}
	run(v, 2);
	long total = op;
	for (int n = 1; n <= 3 * (int)BATT_COUNT; ++n) {
		uint16_t keep[32] = { 0x1234 };
		assert(run(keep, 2, n) == BATT_NACK && keep[0] == 0x1234 && !badRead);
	}
	for (long at = 0; at <= total; ++at)
		for (long len : { 1L, 2L, 3L, 4L, 5L, 6L, 7L, 8L, 10L, 13L, 20L, 40L, 200L, 1500L, 1L << 30 })
			for (int l = 0; l < 2; ++l, ++cases) run(v, 2, 0, at, len, l);
}
int main()
{
	long cases = 0;
	offs[0] = B2L_BATT_SCL; offs[1] = B2L_BATT_SDA; pins[0] = pins[1] = 0; other = 0x22222220;
	suite(cases);

	uint16_t s[BATT_COUNT] = {};
	assert(b2l_batt_ma(0xffff) == 0 && b2l_batt_ma(0xfd60) == -210 && b2l_batt_ma(672) == 210);
	assert(b2l_batt_mv(55411) == 4328 && b2l_batt_centi(0x6400) == 10000 && b2l_batt_centi(0x3280) == 5050);
	assert(!b2l_batt_sane(s));
	s[BATT_VCELL] = 55411; s[BATT_SOC] = 0x6400; assert(b2l_batt_sane(s));
	s[BATT_STATUS] = 8; assert(!b2l_batt_sane(s));
	s[BATT_STATUS] = 0; s[BATT_SOC] = 0xffff; assert(!b2l_batt_sane(s));
	// B1L-0355: what iOS is told, from the 0354 log's own readings
	uint16_t r[BATT_COUNT] = {};
	r[BATT_SOC] = 0x60d9; r[BATT_VCELL] = 0xc6fa; r[BATT_INT_OK] = 0xdf; r[BATT_DTLS1] = 0x31; r[BATT_AVG] = 0xfb37;
	b2l_batt_view w = b2l_batt_view_of(r);
	assert(w.percent == 96 && w.external && w.charging && !w.charged && w.mv == 3979 && w.ma < 0);
	r[BATT_DTLS1] = 0x3b; w = b2l_batt_view_of(r); assert(w.external && !w.charging && !w.charged);    // watchdog out
	r[BATT_DTLS1] = 0x34; w = b2l_batt_view_of(r); assert(w.charged && w.percent == 100);
	r[BATT_INT_OK] = 0x8f; w = b2l_batt_view_of(r); assert(!w.external && !w.charging && !w.charged && w.percent == 96);
	r[BATT_SOC] = 0xffff; w = b2l_batt_view_of(r); assert(w.percent == 100);
	printf("writes in %ld cases, longest hold %ld\n", wrote, worst);
	assert(worst < 5);                                                      // a half period
	printf("battery i2c: ok, %ld held-line cases, no register write from a hold of 5 us or more\n", cases);
}
'''
with tempfile.TemporaryDirectory() as t:
    (pathlib.Path(t) / 't.cpp').write_text(test)
    subprocess.run(['clang++', '-std=c++17', '-O2', '-UNDEBUG', '-o', f'{t}/t', f'{t}/t.cpp'], check=True)
    subprocess.run([f'{t}/t'], check=True)
