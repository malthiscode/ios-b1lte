/*
 * B1L-0113: where backboardd's time goes, now that the GPU is busy 8 % of the time and the interface is still slow.
 *
 * Two sources, merged by one kernel thread and said every 10 s:
 *   - ON THE CPU: each interrupt that lands while a backboardd thread runs records where it was (user pc and lr; for an
 *     interrupt in the kernel, the user pc and lr of the call that brought the thread into the kernel). Written by the
 *     interrupt into a per-CPU ring, read by the thread below; nothing is locked in the interrupt.
 *   - OFF THE CPU: every 2 ms the thread looks at each backboardd thread: running or runnable (TH_RUN), or waiting, and
 *     for a waiting one, where in user space it waits (pc, lr and four frames of its frame-pointer chain).
 *
 *   B2L PROF: 10 s: swaps N (n/s), zc flips N, cpu samples user N kernel N lost N, scans N
 *   B2L PROF T: tid 0x.. name run N% wait N% cpu-user N cpu-kernel N
 *   B2L PROF CPU: N user|kern pc 0x.. lr 0x.. tid 0x..
 *   B2L PROF WAIT: N tid 0x.. pc 0x.. lr 0x.. f 0x.. 0x.. 0x.. 0x..
 * Addresses are slid; scripts in b1l-tools/dsc find the slide from return addresses.
 */
#include <kern/task.h>
#include <kern/thread.h>
#include <kern/processor.h>
#include <kern/sched_prim.h>
#include <kern/clock.h>
#include <kern/misc_protos.h>
#include <kern/locks.h>
#include <vm/vm_kern_xnu.h>
#include <arm/cpu_number.h>
#include <arm/thread.h>
#include <arm64/proc_reg.h>
#include <mach/arm/thread_status.h>
#include <libkern/OSAtomic.h>
#include <kern/counter.h>

extern char *proc_name_address(void *p);
extern void *get_bsdtask_info(task_t t);
extern unsigned b2l_fb_swap_count(void);
extern unsigned b2l_fb_zc_flip_count(void);

#define B2L_PROF_CPUS     8
#define B2L_PROF_RING     512
#define B2L_PROF_THREADS  64
#define B2L_PROF_CPU_HASH 2048
#define B2L_PROF_WAIT_HASH 2048
#define B2L_PROF_FRAMES   8
/* B1L-0160: configd for the Wi-Fi (its KernelEventMonitor never primes); "backboardd" for the interface */
#define B2L_PROF_TARGET   "backboardd"

struct b2l_prof_raw { uint64_t pc, lr, tid, x0, x1, far, kpc, klr, kf[4]; int64_t x16; uint32_t kernel, esr; };   /* B1L-0304: kf */
static struct b2l_prof_raw b2l_prof_ring[B2L_PROF_CPUS][B2L_PROF_RING];
static volatile uint32_t b2l_prof_head[B2L_PROF_CPUS];
static uint32_t b2l_prof_tail[B2L_PROF_CPUS];

task_t volatile b2l_prof_task;   /* backboardd's task while it lives, one reference held */

struct b2l_prof_thr { uint64_t tid; uint32_t run, wait, cpu_user, cpu_kernel; uint32_t on[3]; char name[32]; };   /* B1L-0314: on[] samples on A55 (cpu 0-3), A75 (4-5), M4 (6-7) */
static struct b2l_prof_thr b2l_prof_thr[B2L_PROF_THREADS];

struct b2l_prof_cpu { uint64_t pc, lr, tid, x0, x1, far; uint32_t kernel, n, esr; };
/* B1L-0114: kernel samples by kernel pc, and by exception class / fault status */
struct b2l_prof_kpc { uint64_t kpc, klr, far; uint32_t n, esr; };
#define B2L_PROF_KPC_HASH 1024
static struct b2l_prof_kpc b2l_prof_kpc[B2L_PROF_KPC_HASH];
static uint32_t b2l_prof_ec[64][64];   /* [esr EC][ISS low 6 bits] */
/* B1L-0295: kernel samples taken inside a system call (EC 0x15), by its number: x16, Mach traps below 0 */
#define B2L_PROF_SVC_LOW 128
#define B2L_PROF_SVC_N (B2L_PROF_SVC_LOW + 640)
static uint32_t b2l_prof_svc[B2L_PROF_SVC_N];
/* B1L-0304: kernel samples by the kernel's call chain (4 return addresses up from the interrupted frame, by frame
 * pointer, only inside the thread's own kernel stack): the samples gather where interrupts come back on
 * (ml_set_interrupts_enabled), which hides who spent the time; the chain shows it. */
struct b2l_prof_kstk { uint64_t kpc, kf[4]; int64_t x16; uint32_t n; };
#define B2L_PROF_KSTK_HASH 1024
static struct b2l_prof_kstk b2l_prof_kstk[B2L_PROF_KSTK_HASH];
static uint64_t b2l_prof_faults0;
static struct b2l_prof_cpu b2l_prof_cpu[B2L_PROF_CPU_HASH];

struct b2l_prof_wait { uint64_t tid, pc, lr, f[B2L_PROF_FRAMES]; uint32_t n, used, run; };
static struct b2l_prof_wait b2l_prof_wait[B2L_PROF_WAIT_HASH];

static uint64_t b2l_prof_samples_user, b2l_prof_samples_kernel, b2l_prof_lost, b2l_prof_scans;

/* From sleh_irq, interrupts off, only when current_task() is b2l_prof_task. */
void
b2l_prof_irq(arm_saved_state_t *state)
{
	int cpu = cpu_number();
	if (cpu < 0 || cpu >= B2L_PROF_CPUS) {
		return;
	}
	uint32_t h = b2l_prof_head[cpu];
	if (h - b2l_prof_tail[cpu] >= B2L_PROF_RING) {
		return;   /* full: counted by the reader as lost */
	}
	struct b2l_prof_raw *r = &b2l_prof_ring[cpu][h % B2L_PROF_RING];
	if (is_saved_state64(state) && PSR64_IS_USER64(get_saved_state_cpsr(state))) {
		r->pc = get_saved_state_pc(state);
		r->lr = get_saved_state_lr(state);
		r->kernel = 0;
		r->far = r->kpc = r->klr = 0;
		r->esr = 0;
	} else {
		arm_saved_state_t *u = get_user_regs(current_thread());
		r->pc = u ? get_saved_state_pc(u) : 0;
		r->lr = u ? get_saved_state_lr(u) : 0;
		r->kernel = 1;
		/* B1L-0114: why the thread is in the kernel (the exception it entered by: far, esr) and where */
		r->far = u ? get_saved_state_far(u) : 0;
		r->esr = u ? (uint32_t)get_saved_state_esr(u) : 0;
		r->kpc = get_saved_state_pc(state);
		r->klr = get_saved_state_lr(state);
		/* B1L-0304: the chain, frame record by frame record, never outside this thread's kernel stack */
		vm_offset_t lo = current_thread()->kernel_stack, hi = lo + kernel_stack_size;
		uint64_t fp = get_saved_state_fp(state);
		for (int f = 0; f < 4; f++) {
			r->kf[f] = 0;
			if (lo == 0 || fp < lo || fp + 16 > hi || (fp & 15) != 0) {
				continue;
			}
			uint64_t *rec = (uint64_t *)fp;
			r->kf[f] = rec[1];
			fp = rec[0] > fp ? rec[0] : 0;   /* frames go up the stack only */
		}
	}
	{
		arm_saved_state_t *u = r->kernel ? get_user_regs(current_thread()) : state;
		r->x0 = u ? get_saved_state_reg(u, 0) : 0;
		r->x1 = u ? get_saved_state_reg(u, 1) : 0;
		r->x16 = u ? (int64_t)get_saved_state_reg(u, 16) : 0;   /* B1L-0295 */
	}
	r->tid = current_thread()->thread_id;
	__asm__ volatile ("dmb ish" ::: "memory");
	b2l_prof_head[cpu] = h + 1;
}

/*
 * B1L-0200: hang detector. Every interrupt on every CPU notes where that CPU was. When the log thread
 * (b2l_persistent_log.c, every second) has not run for 6 s, the first CPU to see it draws, once, each CPU's last
 * interrupted pc/lr, how long ago, and the frame-pointer chain of the kernel ones (kernel addresses unslid, for
 * mach.development.b2l.unstripped). A CPU spinning with interrupts masked shows as an old age.
 */
extern uint64_t volatile b2l_log_beat;
extern void b2l_early_panic(const char *fmt, va_list *args);
extern void b2l_early_trace(const char *fmt, ...);
extern vm_offset_t vm_kernel_slide;
struct b2l_hang_cpu { uint64_t pc, lr, fp, at, tid; uint32_t user; };
static struct b2l_hang_cpu b2l_hang[B2L_PROF_CPUS];
static uint32_t b2l_hang_shown;

static bool
b2l_hang_kva(uint64_t a)
{
	return a >= 0xffffff8000000000ull && (a & 0xf) == 0;
}

void
b2l_hang_irq(arm_saved_state_t *state)
{
	int cpu = cpu_number();
	if (cpu < 0 || cpu >= B2L_PROF_CPUS || !is_saved_state64(state)) {
		return;
	}
	uint64_t now = mach_absolute_time(), ns = 0;
	struct b2l_hang_cpu *h = &b2l_hang[cpu];
	h->user = PSR64_IS_USER64(get_saved_state_cpsr(state)) ? 1 : 0;
	h->pc = get_saved_state_pc(state);
	h->lr = get_saved_state_lr(state);
	h->fp = get_saved_state_fp(state);
	h->tid = current_thread()->thread_id;
	__atomic_store_n(&h->at, now, __ATOMIC_RELEASE);
	uint64_t beat = __atomic_load_n(&b2l_log_beat, __ATOMIC_ACQUIRE);
	if (beat == 0 || now <= beat) {
		return;
	}
	absolutetime_to_nanoseconds(now - beat, &ns);
	if (ns < 6000000000ull || __atomic_exchange_n(&b2l_hang_shown, 1, __ATOMIC_ACQ_REL)) {
		return;
	}
	uint64_t slide = (uint64_t)vm_kernel_slide;
	b2l_early_panic("B2L HANG: the log thread is stuck", NULL);
	b2l_early_trace("B2L HANG: log quiet %llu ms, seen by C%d (kernel addresses unslid)\n", ns / 1000000, cpu);
	for (unsigned c = 0; c < B2L_PROF_CPUS; c++) {
		struct b2l_hang_cpu *x = &b2l_hang[c];
		uint64_t at = __atomic_load_n(&x->at, __ATOMIC_ACQUIRE), age = 0;
		if (at == 0) {
			b2l_early_trace("C%u: never\n", c);
			continue;
		}
		absolutetime_to_nanoseconds(now > at ? now - at : 0, &age);
		uint64_t pc = x->user ? x->pc : x->pc - slide, lr = x->user ? x->lr : x->lr - slide;
		b2l_early_trace("C%u %llums %s pc %llx lr %llx t%llx\n", c, age / 1000000, x->user ? "U" : "K", pc, lr, x->tid);
		if (!x->user) {
			uint64_t fp = x->fp;
			b2l_early_trace("  bt");
			for (unsigned d = 0; d < 10 && b2l_hang_kva(fp); d++) {
				uint64_t next = ((volatile uint64_t *)fp)[0], ret = ((volatile uint64_t *)fp)[1];
				b2l_early_trace(" %llx", (ret | 0xffffff8000000000ull) - slide);
				if (next <= fp) {
					break;
				}
				fp = next;
			}
			b2l_early_trace("\n");
		}
	}
}

static struct b2l_prof_thr *
b2l_prof_thread(uint64_t tid)
{
	for (unsigned i = 0; i < B2L_PROF_THREADS; i++) {
		if (b2l_prof_thr[i].tid == tid) {
			return &b2l_prof_thr[i];
		}
		if (b2l_prof_thr[i].tid == 0) {
			b2l_prof_thr[i].tid = tid;
			return &b2l_prof_thr[i];
		}
	}
	return NULL;
}

static void
b2l_prof_drain(void)
{
	for (int cpu = 0; cpu < B2L_PROF_CPUS; cpu++) {
		uint32_t h = b2l_prof_head[cpu];
		__asm__ volatile ("dmb ish" ::: "memory");
		while (b2l_prof_tail[cpu] != h) {
			struct b2l_prof_raw *r = &b2l_prof_ring[cpu][b2l_prof_tail[cpu] % B2L_PROF_RING];
			uint64_t key = (r->pc * 0x9e3779b97f4a7c15ULL) ^ (r->lr * 31) ^ r->kernel;
			unsigned i = (unsigned)(key >> 40) % B2L_PROF_CPU_HASH, probes = 0;
			while (probes < 32) {
				struct b2l_prof_cpu *c = &b2l_prof_cpu[i];
				if (c->n == 0) {
					c->pc = r->pc; c->lr = r->lr; c->tid = r->tid; c->kernel = r->kernel; c->n = 1;
					c->x0 = r->x0; c->x1 = r->x1; c->far = r->far; c->esr = r->esr;
					break;
				}
				if (c->pc == r->pc && c->lr == r->lr && c->kernel == r->kernel) {
					c->n++;
					c->x0 = r->x0; c->x1 = r->x1; c->far = r->far; c->esr = r->esr;
					break;
				}
				i = (i + 1) % B2L_PROF_CPU_HASH;
				probes++;
			}
			if (probes == 32) {
				b2l_prof_lost++;
			}
			if (r->kernel) {
				b2l_prof_samples_kernel++;
				b2l_prof_ec[(r->esr >> 26) & 63][r->esr & 63]++;
				if (((r->esr >> 26) & 63) == 0x15 && r->x16 >= -B2L_PROF_SVC_LOW &&
				    r->x16 < B2L_PROF_SVC_N - B2L_PROF_SVC_LOW) {
					b2l_prof_svc[r->x16 + B2L_PROF_SVC_LOW]++;   /* B1L-0295 */
				}
				{   /* B1L-0304 */
					uint64_t h = r->kf[0] * 0x9e3779b97f4a7c15ULL ^ r->kf[1] * 31 ^ r->kf[2] * 7 ^ r->kf[3];
					unsigned ks = (unsigned)(h >> 40) % B2L_PROF_KSTK_HASH, kq;
					for (kq = 0; kq < 16; kq++, ks = (ks + 1) % B2L_PROF_KSTK_HASH) {
						struct b2l_prof_kstk *e = &b2l_prof_kstk[ks];
						if (e->n == 0 || (e->kf[0] == r->kf[0] && e->kf[1] == r->kf[1] &&
						    e->kf[2] == r->kf[2] && e->kf[3] == r->kf[3])) {
							if (e->n == 0) {
								e->kpc = r->kpc; e->x16 = r->x16;
								for (int f = 0; f < 4; f++) e->kf[f] = r->kf[f];
							}
							e->n++;
							break;
						}
					}
				}
				unsigned k = (unsigned)((r->kpc * 0x9e3779b97f4a7c15ULL) >> 40) % B2L_PROF_KPC_HASH, kp;
				for (kp = 0; kp < 16; kp++, k = (k + 1) % B2L_PROF_KPC_HASH) {
					struct b2l_prof_kpc *e = &b2l_prof_kpc[k];
					if (e->n == 0 || e->kpc == r->kpc) {
						e->kpc = r->kpc; e->klr = r->klr; e->far = r->far; e->esr = r->esr; e->n++;
						break;
					}
				}
			} else {
				b2l_prof_samples_user++;
			}
			struct b2l_prof_thr *t = b2l_prof_thread(r->tid);
			if (t) { if (r->kernel) t->cpu_kernel++; else t->cpu_user++; t->on[cpu < 4 ? 0 : cpu < 6 ? 1 : 2]++; }   /* B1L-0314 */
			b2l_prof_tail[cpu]++;
		}
	}
}

/* backboardd's task, found by name (one reference held), or NULL */
static task_t
b2l_prof_find(void)
{
	task_t found = TASK_NULL, t;
	lck_mtx_lock(&tasks_threads_lock);
	queue_iterate(&tasks, t, task_t, tasks) {
		void *p = get_bsdtask_info(t);
		if (p != NULL && t->active && !strncmp(proc_name_address(p), B2L_PROF_TARGET, sizeof(B2L_PROF_TARGET))) {
			task_reference(t);
			found = t;
			break;
		}
	}
	lck_mtx_unlock(&tasks_threads_lock);
	return found;
}

struct b2l_prof_snap { uint64_t tid, pc, lr, fp; int state; thread_t th; };
static struct b2l_prof_snap b2l_prof_snaps[B2L_PROF_THREADS];

static void
b2l_prof_scan(task_t task)
{
	unsigned n = 0;
	thread_t th;

	task_lock(task);
	if (!task->active) {
		task_unlock(task);
		return;
	}
	queue_iterate(&task->threads, th, thread_t, task_threads) {
		if (n >= B2L_PROF_THREADS) break;
		arm_saved_state_t *u = get_user_regs(th);
		b2l_prof_snaps[n].tid = th->thread_id;
		b2l_prof_snaps[n].state = th->state;
		b2l_prof_snaps[n].pc = u ? get_saved_state_pc(u) : 0;
		b2l_prof_snaps[n].lr = u ? get_saved_state_lr(u) : 0;
		b2l_prof_snaps[n].fp = u ? get_saved_state_fp(u) : 0;
		n++;
	}
	task_unlock(task);
	b2l_prof_scans++;

	for (unsigned k = 0; k < n; k++) {
		struct b2l_prof_snap *s = &b2l_prof_snaps[k];
		struct b2l_prof_thr *t = b2l_prof_thread(s->tid);
		bool waiting = (s->state & TH_WAIT) != 0;
		if (t) { if (waiting) t->wait++; else if (s->state & TH_RUN) t->run++; }
		/* B1L-0114: a running thread's stack too (exact while it is in the kernel, stale while in user space) */
		bool run = !waiting && (s->state & TH_RUN);
		if (!waiting && !run) continue;
		uint64_t f[B2L_PROF_FRAMES] = { 0 }, fp = s->fp;
		for (int d = 0; d < B2L_PROF_FRAMES && fp != 0 && (fp & 7) == 0; d++) {
			uint64_t rec[2];
			if (copyinmap(task->map, fp, rec, sizeof(rec)) != KERN_SUCCESS) break;
			f[d] = rec[1];
			if (rec[0] <= fp) break;
			fp = rec[0];
		}
		uint64_t key = s->tid * 7 + s->lr * 0x9e3779b97f4a7c15ULL + (run ? 0x5bd1e995ULL : 0);
		for (int d = 0; d < B2L_PROF_FRAMES; d++) key = key * 1099511628211ULL ^ f[d];
		unsigned i = (unsigned)(key >> 40) % B2L_PROF_WAIT_HASH, probes = 0;
		for (; probes < 32; probes++, i = (i + 1) % B2L_PROF_WAIT_HASH) {
			struct b2l_prof_wait *w = &b2l_prof_wait[i];
			if (!w->used) {
				w->used = 1; w->n = 1; w->tid = s->tid; w->pc = s->pc; w->lr = s->lr; w->run = run;
				memcpy(w->f, f, sizeof(f));
				break;
			}
			if (w->tid == s->tid && w->lr == s->lr && w->run == (uint32_t)run && !memcmp(w->f, f, sizeof(f))) {
				w->n++;
				break;
			}
		}
		if (probes == 32) b2l_prof_lost++;
	}
	/* names, once per thread */
	for (unsigned k = 0; k < n; k++) {
		struct b2l_prof_thr *t = b2l_prof_thread(b2l_prof_snaps[k].tid);
		if (t && t->name[0] == 0) {
			thread_t tt;
			task_lock(task);
			queue_iterate(&task->threads, tt, thread_t, task_threads) {
				if (tt->thread_id == t->tid) {
					char nm[64] = { 0 };   /* MAXTHREADNAMESIZE */
					thread_get_thread_name(tt, nm);
					strlcpy(t->name, nm[0] ? nm : "-", sizeof(t->name));
					break;
				}
			}
			task_unlock(task);
		}
	}
}

static void
b2l_prof_report(task_t task, uint64_t ms, unsigned swaps, unsigned flips)
{
	unsigned scans = (unsigned)b2l_prof_scans ? (unsigned)b2l_prof_scans : 1;
	printf("B2L PROF: %s main image at 0x%llx\n", B2L_PROF_TARGET, (unsigned long long)task->mach_header_vm_address);
	printf("B2L PROF: %llu s: swaps %u (%u.%u/s), zc flips %u, cpu samples user %llu kernel %llu lost %llu, scans %llu\n",
	    ms / 1000, swaps, (unsigned)(swaps * 1000ULL / (ms ? ms : 1)), (unsigned)((swaps * 10000ULL / (ms ? ms : 1)) % 10),
	    flips, b2l_prof_samples_user, b2l_prof_samples_kernel, b2l_prof_lost, b2l_prof_scans);
	{   /* B1L-0114: page faults of backboardd in the window */
		uint64_t f = counter_load(&task->faults);
		printf("B2L PROF FAULTS: %llu in %llu s (cow %llu total, pageins %llu total)\n", f - b2l_prof_faults0, ms / 1000,
		    counter_load(&task->cow_faults), counter_load(&task->pageins));
		b2l_prof_faults0 = f;
	}
	for (int ec = 0; ec < 64; ec++) {
		for (int iss = 0; iss < 64; iss++) {
			if (b2l_prof_ec[ec][iss] >= 8) {
				printf("B2L PROF EXC: ec 0x%x iss-low 0x%x: %u kernel samples\n", ec, iss, b2l_prof_ec[ec][iss]);
			}
		}
	}
	bzero(b2l_prof_ec, sizeof(b2l_prof_ec));
	for (int i = 0; i < B2L_PROF_SVC_N; i++) {   /* B1L-0295 */
		if (b2l_prof_svc[i] >= 8) {
			printf("B2L PROF SVC: x16 %d: %u kernel samples\n", i - B2L_PROF_SVC_LOW, b2l_prof_svc[i]);
		}
	}
	bzero(b2l_prof_svc, sizeof(b2l_prof_svc));
	for (int shown = 0; shown < 16; shown++) {
		int best = -1; uint32_t bv = 0;
		for (int i = 0; i < B2L_PROF_KPC_HASH; i++) {
			if (b2l_prof_kpc[i].n > bv) { bv = b2l_prof_kpc[i].n; best = i; }
		}
		if (best < 0) break;
		struct b2l_prof_kpc *e = &b2l_prof_kpc[best];
		printf("B2L PROF KPC: %u kpc 0x%llx klr 0x%llx (last far 0x%llx esr 0x%x)\n", e->n, e->kpc, e->klr, e->far, e->esr);
		e->n = 0;
	}
	bzero(b2l_prof_kpc, sizeof(b2l_prof_kpc));
	for (int shown = 0; shown < 20; shown++) {   /* B1L-0304 */
		int best = -1; uint32_t bv = 0;
		for (int i = 0; i < B2L_PROF_KSTK_HASH; i++) {
			if (b2l_prof_kstk[i].n > bv) { bv = b2l_prof_kstk[i].n; best = i; }
		}
		if (best < 0) break;
		struct b2l_prof_kstk *e = &b2l_prof_kstk[best];
		printf("B2L PROF KSTK: %u kpc 0x%llx < 0x%llx < 0x%llx < 0x%llx < 0x%llx x16 %lld\n", e->n, e->kpc,
		    e->kf[0], e->kf[1], e->kf[2], e->kf[3], e->x16);
		e->n = 0;
	}
	bzero(b2l_prof_kstk, sizeof(b2l_prof_kstk));
	/* threads, busiest first (run + cpu samples) */
	for (int shown = 0; shown < 16; shown++) {
		int best = -1; uint32_t bv = 0;
		for (int i = 0; i < B2L_PROF_THREADS; i++) {
			struct b2l_prof_thr *t = &b2l_prof_thr[i];
			uint32_t v = t->run * 4 + t->cpu_user + t->cpu_kernel + 1;
			if (t->tid && !(t->name[0] == '!') && v > bv) { bv = v; best = i; }
		}
		if (best < 0) break;
		struct b2l_prof_thr *t = &b2l_prof_thr[best];
		printf("B2L PROF T: tid 0x%llx %s run %u%% wait %u%% cpu-user %u cpu-kernel %u on A55/A75/M4 %u/%u/%u\n", t->tid,
		    t->name[0] ? t->name : "-", t->run * 100 / scans, t->wait * 100 / scans, t->cpu_user, t->cpu_kernel,
		    t->on[0], t->on[1], t->on[2]);
		t->name[0] = '!';   /* shown; cleared below */
	}
	for (int shown = 0; shown < 24; shown++) {
		int best = -1; uint32_t bv = 0;
		for (int i = 0; i < B2L_PROF_CPU_HASH; i++) {
			if (b2l_prof_cpu[i].n > bv) { bv = b2l_prof_cpu[i].n; best = i; }
		}
		if (best < 0) break;
		struct b2l_prof_cpu *c = &b2l_prof_cpu[best];
		printf("B2L PROF CPU: %u %s pc 0x%llx lr 0x%llx tid 0x%llx x0 0x%llx x1 0x%llx far 0x%llx esr 0x%x\n", c->n,
		    c->kernel ? "kern" : "user", c->pc, c->lr, c->tid, c->x0, c->x1, c->far, c->esr);
		c->n = 0;
	}
	for (int pass = 1; pass >= 0; pass--) {   /* running stacks first, then waiting ones */
		for (int shown = 0; shown < 20; shown++) {
			int best = -1; uint32_t bv = 0;
			for (int i = 0; i < B2L_PROF_WAIT_HASH; i++) {
				if (b2l_prof_wait[i].used && b2l_prof_wait[i].run == (uint32_t)pass && b2l_prof_wait[i].n > bv) {
					bv = b2l_prof_wait[i].n; best = i;
				}
			}
			if (best < 0) break;
			struct b2l_prof_wait *w = &b2l_prof_wait[best];
			printf("B2L PROF %s: %u tid 0x%llx pc 0x%llx lr 0x%llx f 0x%llx 0x%llx 0x%llx 0x%llx 0x%llx 0x%llx 0x%llx 0x%llx\n",
			    pass ? "RUN" : "WAIT", w->n, w->tid, w->pc, w->lr, w->f[0], w->f[1], w->f[2], w->f[3], w->f[4], w->f[5],
			    w->f[6], w->f[7]);
			w->used = 0;
		}
	}
	bzero(b2l_prof_cpu, sizeof(b2l_prof_cpu));
	bzero(b2l_prof_wait, sizeof(b2l_prof_wait));
	bzero(b2l_prof_thr, sizeof(b2l_prof_thr));
	b2l_prof_samples_user = b2l_prof_samples_kernel = b2l_prof_lost = b2l_prof_scans = 0;
}

static void
b2l_prof_thread_main(__unused void *arg, __unused wait_result_t wr)
{
	uint64_t since = mach_absolute_time(), last_find = 0;
	unsigned swaps0 = b2l_fb_swap_count(), flips0 = b2l_fb_zc_flip_count();
	task_t task = TASK_NULL;
	static int ev;

	printf("B2L PROF: " B2L_PROF_TARGET " sampler on (every 2 ms, said every 10 s)\n");
	for (;;) {
		uint64_t now = mach_absolute_time(), ns;
		if (task != TASK_NULL && !task->active) {
			b2l_prof_task = TASK_NULL;
			task_deallocate(task);
			task = TASK_NULL;
		}
		absolutetime_to_nanoseconds(now - last_find, &ns);
		if (task == TASK_NULL && ns > 1000000000ULL) {
			last_find = now;
			task = b2l_prof_find();
			b2l_prof_task = task;
		}
		if (task != TASK_NULL) {
			b2l_prof_drain();
			b2l_prof_scan(task);
		}
		absolutetime_to_nanoseconds(now - since, &ns);
		if (ns >= 10000000000ULL) {
			unsigned swaps = b2l_fb_swap_count(), flips = b2l_fb_zc_flip_count();
			if (task != TASK_NULL) {
				b2l_prof_report(task, ns / 1000000ULL, swaps - swaps0, flips - flips0);
			}
			swaps0 = swaps; flips0 = flips; since = now;
		}
		assert_wait_timeout((event_t)&ev, THREAD_UNINT, 2, 1000 * NSEC_PER_USEC);
		thread_block(THREAD_CONTINUE_NULL);
	}
}

void
b2l_prof_start(void)
{
	static int started;
	thread_t th;
	if (!OSCompareAndSwap(0, 1, &started)) return;
	if (kernel_thread_start(b2l_prof_thread_main, NULL, &th) == KERN_SUCCESS) {
		thread_set_thread_name(th, "b2l.prof");
		thread_deallocate(th);
	}
}
