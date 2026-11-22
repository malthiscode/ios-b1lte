#!/usr/bin/env python3
# SPDX-License-Identifier: BSD-2-Clause
# B1L-0351: B2LMaliService.cpp's own flushBeforeJob / HakiosMaliCpuReads with B2LSurface.h's own fields, on the Mac:
# a cleaning after every use by a processor, none otherwise, and an invalidation for a reader when one was skipped.
import pathlib, subprocess, tempfile

root = pathlib.Path(__file__).resolve().parent.parent / 'xnu-overlay/iokit/Kernel/arm'
h = (root / 'B2LSurface.h').read_text()
fields = h[h.index('\tbool                      flushOnce;'):h.index('\tbool init()', h.index('flushOnce;'))]
c = (root / 'B2LMaliService.cpp').read_text()
code = c[c.index('static void\nflushBeforeJob('):c.index('static void\nflushSurface(MiniIOSurface * s)\n{')]

test = '''#include <cassert>
#include <cstdint>
#include <thread>
#include <vector>
struct MiniIOSurface {
''' + fields + '''};
static uint32_t gMaliFlushLazy = 1;
static unsigned cleans;
static void flushSurface(MiniIOSurface *) { __atomic_fetch_add(&cleans, 1, __ATOMIC_SEQ_CST); }
void HakiosMaliCpuReads(MiniIOSurface *);
''' + code + '''
static unsigned job(MiniIOSurface &s) { cleans = 0; flushBeforeJob(&s); return cleans; }
static unsigned read(MiniIOSurface &s) { cleans = 0; s.cpuUsed(); HakiosMaliCpuReads(&s); s.cpuUsed(); return cleans; }
int main() {
	MiniIOSurface s{};
	flushBeforeJob(nullptr);
	assert(job(s) == 1);                                    // the zeroes it was made with
	assert(job(s) == 0 && job(s) == 0);
	s.cpuUsed(); assert(job(s) == 1);                       // a write lock: still open, what is written so far
	s.cpuUsed(); assert(job(s) == 1 && job(s) == 0);        // its unlock
	assert(read(s) == 1);                                   // a job went without: the reader invalidates
	assert(read(s) == 0);                                   // and only once
	assert(job(s) == 1);                                    // what the reader left in the caches
	assert(read(s) == 0);                                   // nothing skipped since
	MiniIOSurface m{}; m.cpuMapped = true;
	assert(job(m) == 1 && job(m) == 1);
	MiniIOSurface o{}; o.flushOnce = true;
	assert(job(o) == 1 && job(o) == 0);
	o.cpuUsed(); assert(job(o) == 0);
	MiniIOSurface w{}; w.cpuUses = 0xfffffffeu;
	assert(job(w) == 1); w.cpuUsed();
	assert(job(w) == 1); w.cpuUsed();                       // past the wrap
	assert(job(w) == 1 && job(w) == 0);
	// submits racing an unlock: the job after the unlock is never let through on an older cleaning
	for (int i = 0; i < 20000; i++) {
		MiniIOSurface r{};
		std::thread a([&] { flushBeforeJob(&r); }), b([&] { r.cpuUsed(); });
		a.join(); b.join();
		flushBeforeJob(&r);
		assert(r.cleanedAt == r.cpuUses + 1);
	}
	gMaliFlushLazy = 0;
	MiniIOSurface off{};
	assert(job(off) == 1 && job(off) == 1);
}
'''
with tempfile.TemporaryDirectory() as d:
    p = pathlib.Path(d, 't.cpp'); p.write_text(test)
    subprocess.run(['clang++', '-std=c++17', '-Wall', '-fsanitize=address,undefined', str(p), '-o', f'{d}/t'], check=True)
    subprocess.run([f'{d}/t'], check=True)
print('test-surface-flush: ok')
