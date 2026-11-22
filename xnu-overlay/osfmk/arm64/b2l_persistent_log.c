/* SPDX-License-Identifier: BSD-2-Clause */
#include "b2l_persistent_log.h"
#include <arm64/b2l_arm64e_compat.h>
#include <arm64/b2l_early.h>
#include <arm64/b2l_diag.h>
#include <kern/thread.h>
#include <kern/sched_prim.h>
#include <kern/clock.h>
#include <mach/mach_time.h>
#include <pexpert/pexpert.h>
#include <string.h>
#include <machine/machine_routines.h>
#include <kern/cpu_data.h>
static struct b2l_log_ring console_ring;
static struct b2l_log_writer disk_writer;
static uint32_t start_state;
static uint64_t flush_failures;
static int last_error;
uint64_t volatile b2l_log_beat;
/* B1L-0201: write now (a caller about to do something that may freeze) */
void b2l_log_kick(void){thread_wakeup((event_t)&disk_writer);}
void b2l_log_append(char c){b2l_log_ring_append(&console_ring,(uint8_t)c);}
void b2l_log_append_bytes(const void*p,size_t n){b2l_log_ring_append_bytes(&console_ring,p,n);}
static void b2l_log_worker(void*argument __unused,wait_result_t result __unused)
{
    unsigned reports=0;
    /* B1L-0195: seconds between writes, 1 (was 5); boot-arg b2l_log_sec=1..5 overrides it (the boot-args live
     * in XnuLoader, embedded in the UEFI FD). The progress lines stay ~30 s apart. */
    uint32_t sec=1;
    if(!PE_parse_boot_argn("b2l_log_sec",&sec,sizeof(sec))||sec<1||sec>5)sec=1;
    unsigned every=30/sec;
    for(;;){
        int err=b2l_log_writer_drain(&disk_writer,&console_ring,128);
        __atomic_store_n(&b2l_log_beat,mach_absolute_time(),__ATOMIC_RELEASE);   /* B1L-0200: b2l_prof.c hang detector */
        if(err){__atomic_fetch_add(&flush_failures,1,__ATOMIC_RELAXED);__atomic_store_n(&last_error,err,__ATOMIC_RELAXED);}
        /* Outside every storage lock/callback. One bounded progress line per
         * ~30s distinguishes an awake scheduler from silent EL0 execution. */
        if(++reports>=every){
            uint64_t v[4];b2l_arm64e_compat_snapshot(v);reports=0;
            uint64_t perf[6];b2l_arm64e_perf_snapshot(perf);
            b2l_early_trace("B2L EMU PERF: samples=%llu wrapper_ns=%llu vm_ns=%llu hash_ns=%llu paired=%llu pair_fallback=%llu\n",
                perf[0],perf[1],perf[2],perf[3],perf[4],perf[5]);
            b2l_diag_progress();
            {   /* B1L-0050: the traps by kind (b2l_arm64e_compat.c) */
                extern void b2l_arm64e_class_snapshot(uint64_t[9]);
                uint64_t k[9]; b2l_arm64e_class_snapshot(k);
                b2l_early_trace("B2L EMU CLASS: pacaut=%llu hint=%llu ret=%llu br=%llu ldra=%llu ldapr=%llu pacga=%llu other=%llu in-shared-cache=%llu\n",
                    k[0],k[1],k[2],k[3],k[4],k[5],k[6],k[7],k[8]);
            }
            {   /* B1L-0101: the most frequent trap places of the last 30 s */
                extern void b2l_arm64e_prof_report(void);
                b2l_arm64e_prof_report();
            }
            uint64_t ns=0;absolutetime_to_nanoseconds(mach_absolute_time(),&ns);
            b2l_early_trace("B2L LIVE: traps=%llu done=%llu pid=%llu pc=0x%llx flush_errors=%llu last_error=%d sec=%llu pacga=%s\n",
                v[0],v[1],v[2],v[3],__atomic_load_n(&flush_failures,__ATOMIC_RELAXED),__atomic_load_n(&last_error,__ATOMIC_RELAXED),ns/1000000000,b2l_arm64e_pacga_name());
        }
        assert_wait_timeout((event_t)&disk_writer,THREAD_UNINT,sec,NSEC_PER_SEC);
        thread_block(THREAD_CONTINUE_NULL);
    }
}
/* B1L-0220: opening Safari freezes the whole phone right after WebContent starts, and the last second of log never
 * reaches the disk. For com.apple.WebKit.* processes, each kernel entry worth knowing (GPU/IOSurface/framebuffer
 * calls, EL0 cache maintenance, arm64e emulation) is logged; for the first 300, the log thread is woken and given
 * 40 ms to write the line before the action runs, so the line just before the freeze survives.
 * shortcut: diagnostic only, remove once the freeze is understood. */
extern void proc_selfname(char *buf, int size);
extern int proc_selfpid(void);
void b2l_wk_trace(const char *what, uint64_t a, uint64_t b, uint64_t c)
{
    /* B1L-0221: in 0220 the 300 waited lines all went to corecrypto's PAC traps ("emu"), then the phone froze
     * unlogged. Now "emu" gets 100 lines without waiting; calls and cache traps get the waits. */
    static uint32_t count, emu;
    char name[40];
    bool is_emu = what[0] == 'e';
    if (__atomic_load_n(is_emu ? &emu : &count, __ATOMIC_RELAXED) >= (is_emu ? 100u : 4000u)) return;
    proc_selfname(name, sizeof(name));
    if (strncmp(name, "com.apple.WebKi", 15) != 0) return;
    uint32_t k = __atomic_fetch_add(is_emu ? &emu : &count, 1, __ATOMIC_RELAXED);
    if (k >= (is_emu ? 100u : 4000u)) return;
    b2l_early_trace("B2L WK %u: %s pid %d 0x%llx 0x%llx 0x%llx\n", k, what, proc_selfpid(), a, b, c);
    if (!is_emu && k < 400 && ml_get_interrupts_enabled() && get_preemption_level() == 0) {
        b2l_log_kick();
        delay_for_interval(40, NSEC_PER_MSEC);
    }
}
int b2l_log_start(const struct b2l_log_sink*sink)
{
    uint32_t expected=0;
    if(!__atomic_compare_exchange_n(&start_state,&expected,1,0,__ATOMIC_ACQUIRE,__ATOMIC_RELAXED))return B2L_LOG_BAD_ARGUMENT;
    int err=b2l_log_writer_open(&disk_writer,sink);
    if(err){__atomic_store_n(&start_state,0,__ATOMIC_RELEASE);return err;}
    thread_t worker; kern_return_t kr=kernel_thread_start(b2l_log_worker,0,&worker);
    if(kr!=KERN_SUCCESS){disk_writer.active=0;__atomic_store_n(&start_state,0,__ATOMIC_RELEASE);return (int)kr;}
    thread_deallocate(worker);__atomic_store_n(&start_state,2,__ATOMIC_RELEASE);return 0;
}
