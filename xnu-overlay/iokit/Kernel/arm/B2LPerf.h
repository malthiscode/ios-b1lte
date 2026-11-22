/*
 * B1L-0095: where backboardd's kernel time goes when the interface is drawn by the Mali ("caught burning CPU",
 * 83 s of 90 in the kernel). Each kind adds its time and bytes; every 10 s, one line in the log:
 *   B2L PERF: 10 s: blit N x, T ms, MB | flush N x, T ms, MB | submit N x, T ms | flip N x, T ms (B1L-0096)
 * Defined in B2LFramebuffer.cpp.
 */
#pragma once
#include <stdint.h>

enum { B2L_PERF_BLIT, B2L_PERF_FLUSH, B2L_PERF_SUBMIT, B2L_PERF_FLIP, B2L_PERF_KINDS };

#ifdef __cplusplus
extern "C"
#endif
void b2l_perf_note(int kind, uint64_t absolute_time, uint64_t bytes);

/*
 * B1L-0102: the kernel time of each user-client call, by client and selector: backboardd still spends ~82 s of 90 in
 * the kernel (B1L-0101) once the copy, the cache cleaning and the PAC emulation are out of the way. Every 10 s, with
 * the line above, the most expensive ones:
 *   B2L PERF CALLS: mali 3: 1200 x 840 ms | surf 9: ...
 */
enum { B2L_CALL_MALI, B2L_CALL_SURF, B2L_CALL_FB, B2L_CALL_KINDS };
#ifdef __cplusplus
extern "C"
#endif
void b2l_perf_call(int kind, uint32_t selector, uint64_t absolute_time);
#ifdef __cplusplus
extern "C" void b2l_wk_trace(const char *what, uint64_t a, uint64_t b, uint64_t c);   /* B1L-0220 */
#include <mach/mach_time.h>
struct B2LCallTimer {
	int kind;
	uint32_t selector;
	uint64_t began;
	B2LCallTimer(int k, uint32_t s) : kind(k), selector(s), began(mach_absolute_time()) { b2l_wk_trace("call", k, s, 0); }
	~B2LCallTimer() { b2l_perf_call(kind, selector, mach_absolute_time() - began); b2l_wk_trace("ret", kind, selector, 0); }
};
#endif
