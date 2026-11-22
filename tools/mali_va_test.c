/* B1L-0323: checks xnu-overlay/bsd/dev/hakios.mali_va.h on the Mac: cc -I xnu-overlay/bsd/dev tools/mali_va_test.c */
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "hakios.mali_va.h"
static struct mali_va va;
int main(void)
{
	/* small buffers stay under 1 GB, big ones above */
	uint64_t s = mali_va_alloc(&va, 4096, false), b = mali_va_alloc(&va, 36u << 20, false);
	assert(s >= MALI_VA_FIRST && s < MALI_VA_SMALL_END);
	assert(b >= MALI_VA_SMALL_END && b + (36u << 20) <= MALI_VA_END);
	/* 0322's case: churn of small buffers, then a 128 MB heap still finds a run */
	static uint64_t live[3000];   /* ~700 MB of small buffers kept */
	unsigned n = 0;
	for (unsigned i = 0; i < 200000; i++) {
		uint64_t a = mali_va_alloc(&va, (i % 7 + 1) * 40960, i % 3 == 0);
		assert(a != 0);
		if (n < 3000 && i % 2) live[n++] = a;
		else assert(mali_va_free(&va, a, (i % 7 + 1) * 40960));
	}
	for (unsigned i = 0; i < 30; i++) assert(mali_va_alloc(&va, 35u << 20, false) != 0);   /* ~1 GB of targets */
	uint64_t heap = mali_va_alloc(&va, 128u << 20, false);
	assert(heap >= MALI_VA_SMALL_END);
	/* executable buffers never cross 16 MB */
	for (unsigned i = 0; i < 1000; i++) {
		uint64_t a = mali_va_alloc(&va, 300000, true);
		assert(a / MALI_VA_EXEC_LINE == (a + 300000 - 1) / MALI_VA_EXEC_LINE);
	}
	/* a full small zone spills anywhere rather than failing */
	struct mali_va v2; memset(&v2, 0, sizeof(v2));
	unsigned k = 0;
	while (mali_va_alloc(&v2, 512u << 10, false) != 0) k++;
	assert(k > (unsigned)((MALI_VA_SMALL_END - MALI_VA_FIRST) / (576u << 10)));
	puts("mali_va_test: ok");
	return 0;
}
