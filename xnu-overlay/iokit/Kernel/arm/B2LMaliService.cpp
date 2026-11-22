/*
 * B2L (Galaxy S10) port of HakiOS's HakiosMali IOKit service, B1L-0029 / GPU step G3 (docs/gpu/plan.md). Taken from
 * Scion-Project/hackios-xnu-kernel drivers/gpu/hakios.mali.cpp under the Apple Public Source License 2.0, as that file
 * is. Changes, marked "B2L": the IOSurfaces are this kernel's B2LSurface (pixels at offset 0 of `memory`, no scan-out
 * pool, no client-memory surfaces, no display use counts); the service keeps HakiOS's class names so that its libdrm
 * (tools/mesa/hakios-drm, matching "HakiosMali") is used unchanged; the test picture request is answered but not shown.
 * HakiOS's own description follows.
 */
/*
 * HakiosMali: the Galaxy S9's Mali-G72 for programs to draw with.
 *
 * Where it comes from: HakiOS's RedwoodMali, the same service for the Infinix Hot 20's Mali-G52,
 * kernel/iokit/Kernel/RedwoodMali.cpp of github.com/oosexgang/hakios-mt6768 at commit 93ea250 (Apple Public Source
 * License 2.0, as every file of that repository's kernel/ is), renamed: RedwoodMali to HakiosMali, its user client
 * HakiosMaliUserClient, redwood_mali_* to hakios_mali_*, its own requests' names kRedwood* to kHakiosMali* (their numbers
 * and structures are the same: a program built for RedwoodMali needs only the service's new name). Nothing in it is
 * the X6826B's: it changes with the GPU only in what drivers/gpu/hakios.mali.c answers it.
 *
 * drivers/gpu/hakios.mali.c brings the GPU up (boot-arg mali=N) and, at its last step, once a job has run through the
 * GPU's page tables, starts this service. A program that opens it speaks the interface of Linux's panfrost DRM driver,
 * the one Mesa's panfrost knows: every request is a call of the user client with the request's number as the selector
 * and its structure passed in and back -- as HakiosGPU (hakios.gpu.cpp) does it for the POCO's Adreno and freedreno.
 * What Linux does with mmap() on the device is the client memory of the type that is the buffer's handle.
 *
 * What there is: parameters, buffers (made, entered into the GPU's address space, told their address, mapped, given
 * back), and job chains, queued and run one at a time by a thread of hakios.mali.c's, with a number to wait for. A
 * buffer is wired kernel memory, zero when made and write-combined, so what the program writes goes to memory where the
 * GPU, which does not look into the processors' caches here, reads it. Requests 0x7c to 0x7f are ours: an IOSurface's
 * memory as a buffer, the job a surface is drawn by, a wait for a job by its number, and a test's pixels shown. Not here
 * yet: an address space for each program, interrupts, buffers shared between programs as file descriptors.
 *
 * Who may open it: whoever asks, as HakiosGPU. The window server's process, backboardd, does not run as root (PixOS
 * found that out on another phone), so nothing here asks for root.
 *
 * The display waits for a surface's pixels before it shows them (MiniIOSurfaceDrawn(), the display's driver): for one
 * the Mali draws into that is HakiosMaliDrawn() here, which waits for the surface's job (`maliSeq`, MiniIOSurface.h).
 * The original's common tree had that wait in MiniIOSurfaceDrawn() itself; this tree's is the Adreno's
 * (hakios.gpu.cpp), or drivers/core/hakios.stubs.cpp's without it, and both ask this one.
 *
 * Status: written and built into the S9's kernel (CONFIG_HAKIOS_MALI); not yet run on the phone.
 */

#include <IOKit/IOLib.h>
#include <sys/proc.h>   /* B1L-0247: proc_selfname */
#include <IOKit/IOLocks.h>
#include <IOKit/IOKitKeys.h>
#include <IOKit/IOPlatformExpert.h>
#include <IOKit/IOService.h>
#include <IOKit/IOUserClient.h>
#include <IOKit/IOBufferMemoryDescriptor.h>
#include <IOKit/IOSubMemoryDescriptor.h>
#include <kern/clock.h>
#include <mach/mach_time.h>
#include "B2LPerf.h"
#include <mach/vm_param.h>
#include <sys/systm.h>

#include "B2LSurface.h"
#define MALI_SEQ_PENDING (~0ull)      /* B2L: as MiniIOSurface.h */
typedef B2LSurface MiniIOSurface;
static inline MiniIOSurface *MiniIOSurfaceCopy(uint32_t id) { return b2l_surface_copy(id); }
static inline void MiniIOSurfaceDrop(MiniIOSurface *s) { if (s) s->release(); }
static inline void MiniIOMobileFramebufferShow(const uint8_t *, uint32_t, uint32_t, uint32_t) {}
#include <pexpert/pexpert.h>

extern "C" void b2l_metal_accel_start(void);  /* B2LMetalAccel.cpp (B1L-0044) */
extern "C" void hakios_keep(const char * format, ...) __printflike(1, 2);

extern "C" {
/* drivers/gpu/hakios.mali.c (declared in bsd/sys/hakios.input.h, which is not seen from here) */
bool hakios_mali_ready(void);
bool hakios_mali_parameter(uint32_t which, uint64_t * value);
uint64_t hakios_mali_address(uint64_t size, bool executable);
bool hakios_mali_map(uint64_t gpu, uint64_t phys);
void hakios_mali_mapped(void);
void hakios_mali_unmap(uint64_t gpu, uint64_t size);
void hakios_mali_tag(uint64_t address, uint32_t kind, uint32_t width, uint32_t height, uint32_t row_bytes,
    uint32_t format, uint32_t offset);
uint32_t hakios_mali_submit(uint64_t chain, bool fragment);
uint64_t hakios_mali_enqueue(uint64_t chain, bool fragment);
bool hakios_mali_wait(uint64_t seq, uint64_t timeout_ns);

void HakiosMaliStart(void);
void MiniIOMobileFramebufferShow(const uint8_t * pixels, uint32_t width, uint32_t height, uint32_t rowBytes);
void FlushPoC_DcacheRegion(vm_offset_t va, size_t length);      /* osfmk/arm64/caches_asm.s */

/* what programs have done with the GPU so far, for drivers/gpu/hakios.mali.c to repeat in the log */
uint32_t gMaliClients, gMaliBuffers, gMaliSubmits, gMaliFailed, gMaliShown;
/* B1L-0119: what memory looks like when a buffer is refused */
static uint64_t gMaliLiveBytes;
extern "C" unsigned int vm_page_free_count, vm_page_wire_count;
extern "C" uint32_t memorystatus_get_available_page_count(void);   /* B1L-0302: bsd/sys/kern_memorystatus_xnu.h */
extern "C" uint64_t b2l_surface_wired_bytes(void);
/* the first buffers ever made, for matching a GPU fault's address to one of them */
struct mali_made { uint64_t address, size; uint32_t asked, flags; };
struct mali_made gMaliMade[32];
}

/* Linux's include/uapi/drm/panfrost_drm.h and drm.h: the requests' numbers and structures. */
enum {
	kDrmGemClose            = 0x09,
	kPanfrostSubmit         = 0x40,
	kPanfrostWaitBO         = 0x41,
	kPanfrostCreateBO       = 0x42,
	kPanfrostMmapBO         = 0x43,
	kPanfrostGetParam       = 0x44,
	kPanfrostGetBOOffset    = 0x45,
	kPanfrostMadvise        = 0x48,
	kHakiosMaliSurfaceSeq   = 0x7c,         /* ours: a surface's last job, or "drawing, not yet sent" (struct mali_surface_seq) */
	kHakiosMaliWait         = 0x7d,         /* ours: wait for a job by its number (struct mali_wait) */
	kHakiosMaliSurface      = 0x7e,         /* ours, as HakiosGPU's: an IOSurface's memory as a buffer */
	kHakiosMaliShow         = 0x7f,         /* ours, as HakiosGPU's: a test's pixels onto the panel */
};

struct mali_surface {
	uint32_t surface;                       /* the IOSurface's ID */
	uint32_t handle;                        /* kMaliDescribe: no buffer is wanted; else, back: the buffer, */
	uint64_t size;                          /* how big it is, */
	uint32_t offset, rowBytes;              /* where the pixels start in it and how long a row of them is, */
	uint32_t width, height, pixelFormat, planes;    /* and what they are */
};
#define kMaliDescribe           0xffffffffu

/* kHakiosMaliWait: job `seq` (what kPanfrostSubmit gave back in `jc`) done within timeout_ns, or kIOReturnTimeout */
struct mali_wait {
	uint64_t seq;
	int64_t timeout_ns;
};

/* kHakiosMaliSurfaceSeq: the job after which the surface is drawn (HakiosMaliDrawn), or MALI_SEQ_PENDING while the
 * Metal driver's thread has its frame still to send */
struct mali_surface_seq {
	uint32_t surface, pad;
	uint64_t seq;
};
#define MALI_SEQ_PENDING        (~0ull)

struct mali_show {
	uint64_t pixels;
	uint32_t width, height, rowBytes, pad;
};

struct drm_gem_close {
	uint32_t handle, pad;
};

struct drm_panfrost_submit {
	uint64_t jc;
	uint64_t in_syncs;
	uint32_t in_sync_count;
	uint32_t out_sync;
	uint64_t bo_handles;
	uint32_t bo_handle_count;
	uint32_t requirements;
};
#define PANFROST_JD_REQ_FS      (1u << 0)

struct drm_panfrost_wait_bo {
	uint32_t handle, pad;
	int64_t timeout_ns;
};

struct drm_panfrost_create_bo {
	uint32_t size, flags, handle, pad;
	uint64_t offset;
};
/* its flags: NOEXEC, no code is run from it (its addresses may then cross a 16 MB line); HEAP, the tiler's heap, which
 * Linux grows on a fault and which is made whole here (Mesa's is 128 MB). Both taken from Mesa as 1.1 of the interface
 * has them. */
#define PANFROST_BO_NOEXEC      (1u << 0)
#define PANFROST_BO_HEAP        (1u << 1)

struct drm_panfrost_mmap_bo {
	uint32_t handle, flags;
	uint64_t offset;
};

struct drm_panfrost_get_param {
	uint32_t param, pad;
	uint64_t value;
};

struct drm_panfrost_get_bo_offset {
	uint32_t handle, pad;
	uint64_t offset;
};

struct drm_panfrost_madvise {
	uint32_t handle, madv, retained;
};

#define MALI_BUFFERS            4096
#define MALI_BUFFER_MAX         (256ull << 20)
/* B1L-0037: of a HEAP buffer (Mesa's tiler heap, 128 MB of addresses) only this much is memory; past it the GPU finds no
 * entry, faults, and the job is reset after its second (Linux grows the heap on that fault instead). */
#define MALI_HEAP_BACKED        (16ull << 20)

struct mali_buffer {
	IOMemoryDescriptor * memory;            /* made here, or the pixels of an IOSurface (`surface`) */
	MiniIOSurface * surface;
	uint64_t address, size;
	uint64_t last_seq;                      /* the last job given it (kPanfrostSubmit's bo_handles) */
	uint32_t dontneed;                      /* B1L-0247: Mesa's cache marked it purgeable (PANFROST_MADV_DONTNEED) */
	uint32_t purged;                        /* B1L-0258: its memory was taken back; the handle waits for GEM_CLOSE */
};

static IOLock * gMaliLock;

/* B1L-0128: the clients' buffer tables, for hakios_mali_scan (a job's faulting address, looked for in its buffers) */
#define B2L_SCAN_CLIENTS 32   /* B1L-0247: 8 -> 32, every client's memory is now reported */
static struct mali_buffer * volatile b2l_scan_tables[B2L_SCAN_CLIENTS];
static char b2l_scan_names[B2L_SCAN_CLIENTS][24];   /* B1L-0247: the process that opened each table */


/*
 * B1L-0258: memory pressure takes back what Mesa's buffer cache marked purgeable (PANFROST_MADV_DONTNEED), as Linux's
 * panfrost shrinker does (panfrost_gem_shrinker.c): pages and GPU addresses go, the handle stays until GEM_CLOSE, and
 * the next PANFROST_MADV_WILLNEED answers retained = 0, on which Mesa frees the buffer (pan_bo.c,
 * panfrost_bo_cache_fetch). Only buffers no job still uses (done without waiting) and that are not an IOSurface's.
 * With gMaliLock held. Stops once `want` bytes are back; returns what came back.
 */
static uint64_t
b2l_mali_purge(uint64_t want)
{
	uint64_t back = 0;
	uint32_t n = 0;
	for (int c = 0; c < B2L_SCAN_CLIENTS && back < want; c++) {
		struct mali_buffer * table = b2l_scan_tables[c];
		if (table == NULL) {
			continue;
		}
		for (uint32_t h = 0; h < MALI_BUFFERS && back < want; h++) {
			struct mali_buffer * b = &table[h];
			if (b->memory == NULL || b->surface != NULL || !b->dontneed || b->purged) {
				continue;
			}
			/* B1L-0317: still mapped in the program (Mesa's cache keeps its mapping since B1L-0306; each IOMemoryMap
			 * holds a reference). Released here, its pages and kernel addresses stayed held by that map, and Mesa's
			 * later unmap found no buffer behind the handle (clientMemoryForType), so the map was never removed:
			 * 0316 leaked wired memory and shared-range addresses until 5850 creations failed. */
			if (b->memory->getRetainCount() > 1) {
				continue;
			}
			if (b->last_seq != 0 && !hakios_mali_wait(b->last_seq, 0)) {
				continue;
			}
			hakios_mali_unmap(b->address, b->size);
			OSAddAtomic64(-(SInt64)b->size, (volatile SInt64 *)&gMaliLiveBytes);
			b->memory->release();
			back += b->size;
			n++;
			bzero(b, sizeof(*b));
			b->purged = 1;
		}
	}
	static uint32_t said;
	if (n != 0 && said < 40) {
		said++;
		IOLog("HakiosMali: B1L-0258: %u purgeable buffers taken back, %llu MB (free now %llu MB, available %llu MB)\n",
		    n, back >> 20, (uint64_t)vm_page_free_count * PAGE_SIZE >> 20,
		    (uint64_t)memorystatus_get_available_page_count() * PAGE_SIZE >> 20);   /* B1L-0302: available too */
	}
	return back;
}

/* * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * */

/*
 * Whether the Mali is done with a surface's pixels, waiting for at most so many milliseconds: what the display asks
 * before it shows a surface (MiniIOSurfaceDrawn()). The surface's `maliSeq` is the last job that names it (0: none, so
 * nothing to wait for), or MALI_SEQ_PENDING while the program drawing into it has not sent its frame yet: that is waited
 * out first, asking again every 200 us, and then the job.
 */
#define MALI_ASK_AGAIN_US       200

/*
 * The GPU here is not coherent with the processors (COHERENCY_ENABLE = 31, drivers/gpu/hakios.mali.c): it reads and
 * writes memory, never the processors' caches. The buffers this client makes are mapped write-combining and need
 * nothing; an IOSurface's pixels are ordinary cached memory, written by programs (CoreAnimation's layers drawn by the
 * processor, uploads) and read by the processor (the display, which copies them to the panel; a program reading back).
 * So their cache lines are written back and invalidated, to the point of coherency, around the GPU's use of them:
 *   - before a job that names one is queued (what a processor wrote, in memory for the GPU to read);
 *   - after a job that wrote one is waited for, by the display (HakiosMaliDrawn()) or by the program (WAIT_BO): what
 *     the GPU wrote is not hidden behind lines the processor fetched before.
 * Through the kernel's own mapping of the pixels (kpixels): the data caches are physically tagged, so that covers every
 * mapping of the same pages. An upstream port on a non-coherent Mali (the Infinix Hot 20's G52) saw some frames drawn
 * wrong and nothing here did this. Boot-arg mali_flush=0: not done (for measuring what it costs).
 */
static uint32_t gMaliFlush = 1;

static void flushSurface(MiniIOSurface * s);

/*
 * B1L-0097: before a job, what the processors wrote, out to memory. backboardd's panel-sized render targets are drawn
 * by the GPU only: once out (the zeroes they were made with), nothing of theirs is ever dirty in a cache again -- the
 * display's copy only reads them, after HakiosMaliDrawn() has invalidated them. 17 MB cleaned at every submit was up
 * to 2.9 s of processor every 10 s (B1L-0095).
 */
static void
flushBeforeJob(MiniIOSurface * s)
{
	if (s != NULL && s->flushOnce) {
		if (s->flushedOnce) {
			return;
		}
		s->flushedOnce = true;
	}
	flushSurface(s);
}

static void
flushSurface(MiniIOSurface * s)
{
	/* (a surface of fb_zc's scan-out pool is write-combined everywhere: no line of it is in a cache) */
	if (s != NULL && s->mapCache == kIOMapWriteCombineCache) {
		return;   /* B1L-0096: write-combined everywhere (the panel, the scan-out surfaces): no line of it is cached */
	}
	uintptr_t k = s != NULL ? s->kernelAddress() : 0;     /* B2L */
	if (gMaliFlush && k != 0 && s->allocSize != 0) {
		uint64_t t0 = mach_absolute_time();   /* B1L-0095 */
		FlushPoC_DcacheRegion((vm_offset_t) k, s->allocSize);
		b2l_perf_note(B2L_PERF_FLUSH, mach_absolute_time() - t0, s->allocSize);
	}
}

bool
HakiosMaliDrawn(MiniIOSurface * surface, uint64_t milliseconds)
{
	uint64_t seq = surface->maliSeq, interval, deadline, now, left = 0;

	if (seq == 0 || !hakios_mali_ready()) {
		return true;
	}
	nanoseconds_to_absolutetime(milliseconds * NSEC_PER_MSEC, &interval);
	deadline = mach_absolute_time() + interval;
	while ((seq = surface->maliSeq) == MALI_SEQ_PENDING && mach_absolute_time() < deadline) {
		delay_for_interval(MALI_ASK_AGAIN_US, NSEC_PER_USEC);
	}
	if (seq == MALI_SEQ_PENDING) {
		return false;
	}
	if (seq == 0) {
		return true;
	}
	now = mach_absolute_time();
	if (now < deadline) {
		absolutetime_to_nanoseconds(deadline - now, &left);
	}
	if (!hakios_mali_wait(seq, left)) {
		return false;
	}
	flushSurface(surface);                  /* what the GPU drew, not lines the processor read before it */
	return true;
}

/* * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * */

class HakiosMali : public IOService
{
	OSDeclareDefaultStructors(HakiosMali);

public:
	virtual bool start(IOService * provider) APPLE_KEXT_OVERRIDE;
};

OSDefineMetaClassAndStructors(HakiosMali, IOService);

bool
HakiosMali::start(IOService * provider)
{
	if (!IOService::start(provider)) {
		return false;
	}
	setProperty(kIOUserClientClassKey, "HakiosMaliUserClient");
	setName("HakiosMali");
	registerService();
	return true;
}

void
HakiosMaliStart(void)
{
	IOService * provider = IOService::getPlatform();
	HakiosMali * gpu;

	gMaliLock = IOLockAlloc();
	(void)PE_parse_boot_argn("mali_flush", &gMaliFlush, sizeof(gMaliFlush));
	gpu = OSTypeAlloc(HakiosMali);
	if (gpu == NULL || provider == NULL || !gpu->init() || !gpu->attach(provider)) {
		OSSafeReleaseNULL(gpu);
		IOLog("HakiosMali: cannot create the service\n");
		return;
	}
	if (!gpu->start(provider)) {
		gpu->detach(provider);
		IOLog("HakiosMali: the service did not start\n");
	}
	gpu->release();
	IOLog("HakiosMali: the GPU is there for programs to draw with\n");
	b2l_metal_accel_start();        /* B1L-0044: Metal's IOAcceleratorES, now that the GPU's service is up */
}

/* * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * */

class HakiosMaliUserClient : public IOUserClient
{
	OSDeclareDefaultStructors(HakiosMaliUserClient);

	struct mali_buffer *    fBuffers;               /* handle n is fBuffers[n - 1] */
	uint32_t                fNextBuffer;
	uint32_t                fSubmits, fFailed, fRequests;

	struct mali_buffer * buffer(uint32_t handle);
	void freeBuffer(struct mali_buffer * b);
	IOReturn createBO(struct drm_panfrost_create_bo * request);
	struct mali_buffer * newBuffer(IOMemoryDescriptor * memory, uint64_t from, uint64_t size, uint32_t * handle,
	    uint32_t flags, uint64_t backed = ~0ull);
	IOReturn surfaceBuffer(struct mali_surface * request);

public:
	virtual bool start(IOService * provider) APPLE_KEXT_OVERRIDE;
	virtual IOReturn clientClose(void) APPLE_KEXT_OVERRIDE;
	virtual void free(void) APPLE_KEXT_OVERRIDE;
	virtual IOReturn clientMemoryForType(UInt32 type, IOOptionBits * options, IOMemoryDescriptor ** memory) APPLE_KEXT_OVERRIDE;
	virtual IOReturn externalMethod(uint32_t selector, IOExternalMethodArguments * args,
	    IOExternalMethodDispatch * dispatch, OSObject * target, void * reference) APPLE_KEXT_OVERRIDE;
};

OSDefineMetaClassAndStructors(HakiosMaliUserClient, IOUserClient);

bool
HakiosMaliUserClient::start(IOService * provider)
{
	if (!hakios_mali_ready() || !IOUserClient::start(provider)) {
		IOLog("HakiosMali: a client could not be started\n");
		return false;
	}
	IOLog("HakiosMali: a client opened the GPU\n");
	OSIncrementAtomic((SInt32 *) &gMaliClients);
	fBuffers = IONewZero(struct mali_buffer, MALI_BUFFERS);
	for (int i = 0; fBuffers != NULL && i < B2L_SCAN_CLIENTS; i++) {   /* B1L-0128 */
		if (OSCompareAndSwapPtr(NULL, fBuffers, (void * volatile *) &b2l_scan_tables[i])) {
			proc_selfname(b2l_scan_names[i], sizeof(b2l_scan_names[i]));   /* B1L-0247 */
			break;
		}
	}
	return fBuffers != NULL;
}

struct mali_buffer *
HakiosMaliUserClient::buffer(uint32_t handle)
{
	if (handle == 0 || handle > MALI_BUFFERS || fBuffers[handle - 1].memory == NULL) {
		return NULL;
	}
	return &fBuffers[handle - 1];
}

/* With the lock held. */
void
HakiosMaliUserClient::freeBuffer(struct mali_buffer * b)
{
	/* not while a job queued or running may still use it (jobs are run in a queue) */
	if (b->last_seq != 0 && !hakios_mali_wait(b->last_seq, 3ull * NSEC_PER_SEC)) {
		/* B1L-0037: a job that may still use it: the memory is kept (leaked) and stays in the GPU's tables, never
		 * given back to the kernel while the GPU can write it */
		IOLog("HakiosMali: a buffer of %llu bytes still in a job's hands: kept\n", b->size);
		bzero(b, sizeof(*b));
		return;
	}
	hakios_mali_unmap(b->address, b->size);
	OSAddAtomic64(-(SInt64)b->size, (volatile SInt64 *)&gMaliLiveBytes);   /* B1L-0119 */
	b->memory->release();
	if (b->surface != NULL) {
		MiniIOSurfaceDrop(b->surface);
	}
	bzero(b, sizeof(*b));
}

/*
 * A handle and GPU addresses for `size` bytes of `memory` from byte `from` on, every page entered into the GPU's tables.
 * `flags` are Linux's PANFROST_BO_*: without NOEXEC the GPU may run code from it, and its addresses do not cross a 16 MB
 * line (hakios_mali_address()).
 * With the lock held; NULL: no handle, no addresses, or a page that would not go in. The caller sets `memory` (and
 * `surface`) of what comes back.
 */
struct mali_buffer *
HakiosMaliUserClient::newBuffer(IOMemoryDescriptor * memory, uint64_t from, uint64_t size, uint32_t * handle,
    uint32_t flags, uint64_t backed)
{
	struct mali_buffer * b = NULL;
	bool mapped = true;

	for (uint32_t n = 0; n < MALI_BUFFERS; n++) {
		uint32_t i = (fNextBuffer + n) % MALI_BUFFERS;

		if (fBuffers[i].memory == NULL && !fBuffers[i].purged) {   /* B1L-0258: a purged handle is still Mesa's */
			b = &fBuffers[i];
			*handle = i + 1;
			fNextBuffer = i + 1;
			break;
		}
	}
	if (b == NULL) {
		IOLog("HakiosMali: no handles left\n");
		return NULL;
	}
	b->address = hakios_mali_address(size, !(flags & PANFROST_BO_NOEXEC));
	/* every 4 KB of it: the GPU's pages, a quarter of the kernel's 16 KB ones */
	for (uint64_t offset = 0; offset < size && offset < backed && mapped && b->address != 0; offset += 4096) {
		IOByteCount length = 0;
		addr64_t phys = memory->getPhysicalSegment(from + offset, &length, kIOMemoryMapperNone);

		/* (hakios_mali_map() takes only memory the kernel manages: pmap_valid_address()) */
		mapped = phys != 0 && hakios_mali_map(b->address + offset, phys);
	}
	if (b->address == 0 || !mapped) {
		if (b->address != 0) {
			hakios_mali_unmap(b->address, size);
		}
		bzero(b, sizeof(*b));
		IOLog("HakiosMali: a buffer of %llu bytes cannot be entered into the GPU's address space\n", size);
		return NULL;
	}
	hakios_mali_mapped();
	if (gMaliBuffers < 32) {
		gMaliMade[gMaliBuffers] = (struct mali_made) { b->address, size, (uint32_t) size, flags };
	}
	OSIncrementAtomic((SInt32 *) &gMaliBuffers);
	OSAddAtomic64((SInt64)size, (volatile SInt64 *)&gMaliLiveBytes);   /* B1L-0119 */
	b->size = size;
	return b;
}

/*
 * B1L-0300 (measure): where a buffer's creation spends its time, by size, said every 10 s:
 * purge (taking back purgeable buffers), alloc (the IOBufferMemoryDescriptor), map (its pages into the GPU's tables).
 */
static volatile SInt64 b2l_cr_n[5], b2l_cr_bytes[5], b2l_cr_purge[5], b2l_cr_alloc[5], b2l_cr_map[5], b2l_cr_contig[5];
static volatile UInt64 b2l_cr_since;

static void
b2l_create_note(uint64_t size, uint64_t t0, uint64_t t1, uint64_t t2, uint64_t t3, bool contig)
{
	unsigned k = size < (64ull << 10) ? 0 : size <= (256ull << 10) ? 1 : size < (1ull << 20) ? 2 : size < (4ull << 20) ? 3 : 4;
	static const char *const name[5] = { "<64K", "64-256K", "<1M", "1-4M", ">=4M" };

	OSAddAtomic64(1, &b2l_cr_n[k]);
	OSAddAtomic64((SInt64)size, &b2l_cr_bytes[k]);
	OSAddAtomic64((SInt64)(t1 - t0), &b2l_cr_purge[k]);
	OSAddAtomic64((SInt64)(t2 - t1), &b2l_cr_alloc[k]);
	OSAddAtomic64((SInt64)(t3 - t2), &b2l_cr_map[k]);
	if (contig) {
		OSAddAtomic64(1, &b2l_cr_contig[k]);
	}
	uint64_t ns, was = (uint64_t)b2l_cr_since;
	absolutetime_to_nanoseconds(t3 - was, &ns);
	if (was == 0) {
		(void)OSCompareAndSwap64(0, t3, &b2l_cr_since);
		return;
	}
	if (ns < 10000000000ull || !OSCompareAndSwap64(was, t3, &b2l_cr_since)) {
		return;
	}
	for (unsigned i = 0; i < 5; i++) {
		uint64_t n = (uint64_t)OSAddAtomic64(-(SInt64)b2l_cr_n[i], &b2l_cr_n[i]);
		uint64_t by = (uint64_t)OSAddAtomic64(-(SInt64)b2l_cr_bytes[i], &b2l_cr_bytes[i]);
		uint64_t pu, al, ma, co = (uint64_t)OSAddAtomic64(-(SInt64)b2l_cr_contig[i], &b2l_cr_contig[i]);
		absolutetime_to_nanoseconds((uint64_t)OSAddAtomic64(-(SInt64)b2l_cr_purge[i], &b2l_cr_purge[i]), &pu);
		absolutetime_to_nanoseconds((uint64_t)OSAddAtomic64(-(SInt64)b2l_cr_alloc[i], &b2l_cr_alloc[i]), &al);
		absolutetime_to_nanoseconds((uint64_t)OSAddAtomic64(-(SInt64)b2l_cr_map[i], &b2l_cr_map[i]), &ma);
		if (n != 0) {
			printf("B2L CREATE: 10 s %s: %llu x, %llu MB, purge %llu ms, alloc %llu ms, map %llu ms, physmask %llu\n",
			    name[i], n, by >> 20, pu / 1000000, al / 1000000, ma / 1000000, co);
		}
	}
}

/* With the lock held. */
IOReturn
HakiosMaliUserClient::createBO(struct drm_panfrost_create_bo * request)
{
	uint64_t size = round_page_64(request->size);
	uint64_t backed = size;
	IOBufferMemoryDescriptor * memory;
	struct mali_buffer * b;

	if (size == 0 || size > MALI_BUFFER_MAX) {
		return kIOReturnBadArgument;
	}
	if ((request->flags & PANFROST_BO_HEAP) && backed > MALI_HEAP_BACKED) {
		backed = MALI_HEAP_BACKED;      /* B1L-0037: not 128 MB of wired memory for the tiler */
	} else if (backed >= (8ull << 20)) {
		/* B1L-0331: size classes. Full-screen targets come in many sizes (26.2, 27.9, 28.0, 30.3, 30.6, 35.0,
		 * 36.2 MB in 0325-0327) and each freed one left a hole the next, slightly bigger, could not use: refusals
		 * with 500-950 MB free. Rounded up to 4 MB, a hole fits every buffer of its class and below (<= 4 MB wasted). */
		backed = (backed + (4ull << 20) - 1) & ~((4ull << 20) - 1);
	}
	/* B1L-0104: by pages, not from the kernel's shared data heap: buffers of 9 to 36 MB failed there (15 a boot at
	 * B1L-0103, among them the 1088x2304 RGBA16Float textures of the Control Center's Liquid Glass) while the
	 * IOSurfaces of the same heap still came. Any page the GPU's 40-bit addresses reach will do. */
	memory = NULL;
	uint64_t b2l_t0 = mach_absolute_time(), b2l_t1 = 0;   /* B1L-0300 */
	bool b2l_contig = false;
	/* B1L-0258: with less than 256 MB available, take back purgeable buffers first (twice what this one needs).
	 * B1L-0302: available as memorystatus counts it (free + reclaimable), not free alone: iOS keeps only 30-44 MB free,
	 * so every creation purged, and Mesa's cached big buffers were taken back and made again (1.3 GB per 10 s in 0301). */
	if ((uint64_t)memorystatus_get_available_page_count() * PAGE_SIZE < (256ull << 20)) {
		(void)b2l_mali_purge(2 * backed + (32ull << 20));
	}
	b2l_t1 = mach_absolute_time();   /* B1L-0300 */
	for (int attempt = 0; attempt < 2 && memory == NULL; attempt++) {
	if (attempt == 1 && b2l_mali_purge(~0ull) == 0) {
		break;   /* nothing to take back: the failure stands */
	}
	if (backed >= (1ull << 20)) {
		memory = IOBufferMemoryDescriptor::inTaskWithPhysicalMask(kernel_task, kIODirectionInOut |
		    kIOMemoryKernelUserShared | kIOMapWriteCombineCache, (vm_size_t) backed, 0x000000FFFFFFF000ULL);
		b2l_contig = memory != NULL;   /* B1L-0300 */
	}
	if (memory == NULL) {
		memory = IOBufferMemoryDescriptor::inTaskWithOptions(kernel_task, kIODirectionInOut | kIOMemoryKernelUserShared |
		    kIOMapWriteCombineCache, (vm_size_t) backed, PAGE_SIZE);
	}
	}   /* B1L-0258: attempts */
	if (memory == NULL) {
		IOLog("HakiosMali: no memory for a buffer of %llu bytes (B1L-0119: free %llu MB, wired %llu MB, GPU buffers "
		    "alive %llu MB, IOSurfaces %llu MB)\n", size, (uint64_t)vm_page_free_count * PAGE_SIZE >> 20,
		    (uint64_t)vm_page_wire_count * PAGE_SIZE >> 20, gMaliLiveBytes >> 20, b2l_surface_wired_bytes() >> 20);
		return kIOReturnNoMemory;
	}
	uint64_t b2l_t2 = mach_absolute_time();   /* B1L-0300 */
	b = newBuffer(memory, 0, size, &request->handle, request->flags, backed);
	if (b == NULL) {
		memory->release();
		return kIOReturnNoMemory;
	}
	b2l_create_note(size, b2l_t0, b2l_t1, b2l_t2, mach_absolute_time(), b2l_contig);   /* B1L-0300 */
	b->memory = memory;
	request->offset = b->address;
	if (backed < size) {
		IOLog("HakiosMali: heap of %llu MB addresses, %llu MB of memory behind them\n", size >> 20, backed >> 20);
	}
	hakios_mali_tag(b->address, 1, 0, 0, 0, 0, 0);
	return kIOReturnSuccess;
}

/*
 * The pixels of an IOSurface as a buffer, from the page after the surface's shared state on (MiniIOSurface.h): what the
 * window server draws into, and what is shown. The surface is held for as long as the buffer is there; a surface that is
 * a buffer of this client's already is that buffer again. Asked only to describe (kMaliDescribe), no buffer is made. As
 * HakiosGPU's.
 */
IOReturn
HakiosMaliUserClient::surfaceBuffer(struct mali_surface * request)
{
	MiniIOSurface * s = MiniIOSurfaceCopy(request->surface);
	struct mali_buffer * b = NULL;
	IOMemoryDescriptor * memory;
	uint64_t size;

	if (s == NULL) {
		return kIOReturnNotFound;
	}
	if (s->memory == NULL) {                /* B2L */
		MiniIOSurfaceDrop(s);
		return kIOReturnUnsupported;
	}
	size = round_page_64(s->allocSize);
	if (size > s->memory->getLength()) {
		size = s->memory->getLength() & ~(uint64_t) PAGE_MASK;
	}
	/* B2L: a B2LSurface's pixels are its memory from offset 0 */
	memory = IOSubMemoryDescriptor::withSubRange(s->memory, 0, size, kIODirectionInOut);
	if (memory == NULL || size == 0 || size > MALI_BUFFER_MAX) {
		OSSafeReleaseNULL(memory);
		MiniIOSurfaceDrop(s);
		return kIOReturnNoMemory;
	}
	request->size = size;
	request->offset = s->offset;
	request->rowBytes = s->bytesPerRow;
	request->width = s->width;
	request->height = s->height;
	request->pixelFormat = s->pixelFormat;
	request->planes = s->planeCount;
	if (request->handle == kMaliDescribe) {
		request->handle = 0;
		memory->release();
		MiniIOSurfaceDrop(s);
		return kIOReturnSuccess;
	}
	for (uint32_t i = 0; i < MALI_BUFFERS; i++) {
		if (fBuffers[i].memory != NULL && fBuffers[i].surface == s) {
			b = &fBuffers[i];
			request->handle = i + 1;
			break;
		}
	}
	if (b != NULL) {
		memory->release();
		MiniIOSurfaceDrop(s);
		return kIOReturnSuccess;
	}
	b = newBuffer(s->memory, 0, size, &request->handle, PANFROST_BO_NOEXEC);   /* B2L */
	if (b == NULL) {
		memory->release();
		MiniIOSurfaceDrop(s);
		return kIOReturnNoMemory;
	}
	b->memory = memory;
	b->surface = s;
	hakios_mali_tag(b->address, 2, s->width, s->height, s->bytesPerRow, s->pixelFormat, (uint32_t) s->offset);
	return kIOReturnSuccess;
}

IOReturn
HakiosMaliUserClient::clientClose(void)
{
	IOLockLock(gMaliLock);
	for (uint32_t i = 0; fBuffers != NULL && i < MALI_BUFFERS; i++) {
		if (fBuffers[i].memory != NULL) {
			freeBuffer(&fBuffers[i]);
		}
	}
	IOLockUnlock(gMaliLock);
	if (fSubmits != 0) {
		IOLog("HakiosMali: a client goes after %u job chains, %u of them unfinished\n", fSubmits, fFailed);
	}
	terminate();
	return kIOReturnSuccess;
}

void
HakiosMaliUserClient::free(void)
{
	if (fBuffers != NULL) {
		for (int i = 0; i < B2L_SCAN_CLIENTS; i++) {   /* B1L-0128 */
			OSCompareAndSwapPtr(fBuffers, NULL, (void * volatile *) &b2l_scan_tables[i]);
		}
		IODelete(fBuffers, struct mali_buffer, MALI_BUFFERS);
		fBuffers = NULL;
	}
	IOUserClient::free();
}

IOReturn
HakiosMaliUserClient::clientMemoryForType(UInt32 type, IOOptionBits * options, IOMemoryDescriptor ** memory)
{
	struct mali_buffer * b;
	IOReturn ret = kIOReturnBadArgument;

	IOLockLock(gMaliLock);
	b = buffer(type);
	if (b != NULL) {
		b->memory->retain();
		*memory = b->memory;
		/* (fb_zc: the scan-out pool is no memory of the kernel's, and would be mapped as a device's: write-combined) */
		*options = 0;     /* B2L: no scan-out surfaces */
		/* B1L-0293: our own buffers (wired IOBufferMemoryDescriptors, auto-prepared) are entered in the program's map
		 * at once (IOMemoryDescriptor.cpp, kIOMapPrefault), not a page fault per page at first touch: Mesa maps a
		 * buffer again each time it takes it back from its cache, and backboardd's render thread spent most of its
		 * time in the kernel on those faults (0292: ~30000 a second, in panfrost_bo_create's bzero and
		 * panfrost_emit_const_buf's memcpy). An imported IOSurface's memory is left as it was. */
		if (b->surface == NULL) {
			*options = kIOMapPrefault;
		}
		ret = kIOReturnSuccess;
	}
	IOLockUnlock(gMaliLock);
	return ret;
}

IOReturn
HakiosMaliUserClient::externalMethod(uint32_t selector, IOExternalMethodArguments * args,
    IOExternalMethodDispatch * dispatch, OSObject * target, void * reference)
{
	B2LCallTimer b2l_timer(B2L_CALL_MALI, selector);   /* B1L-0102 */
	union {
		struct drm_gem_close close;
		struct drm_panfrost_submit submit;
		struct drm_panfrost_wait_bo wait;
		struct drm_panfrost_create_bo create;
		struct drm_panfrost_mmap_bo mmap;
		struct drm_panfrost_get_param param;
		struct drm_panfrost_get_bo_offset offset;
		struct drm_panfrost_madvise madvise;
		struct mali_show show;
		struct mali_surface surface;
		struct mali_wait waitseq;
		struct mali_surface_seq sseq;
	} r;
	size_t size = args->structureInputSize;
	struct mali_buffer * b;
	IOReturn ret = kIOReturnSuccess;

	if (args->structureInput == NULL || size > sizeof(r) || fBuffers == NULL) {
		return kIOReturnBadArgument;
	}
	if (!hakios_mali_ready()) {
		return kIOReturnNotReady;
	}
	bzero(&r, sizeof(r));
	memcpy(&r, args->structureInput, size);

	/* the two waits, without holding everybody else up */
	if (selector == kHakiosMaliWait || selector == kPanfrostWaitBO) {
		uint64_t seq = r.waitseq.seq;
		int64_t timeout = r.waitseq.timeout_ns;

		if (selector == kPanfrostWaitBO) {
			IOLockLock(gMaliLock);
			b = buffer(r.wait.handle);
			seq = b != NULL ? b->last_seq : 0;
			/* B1L-0291: a buffer taken back by b2l_mali_purge was idle (it waits for that) and stays a handle until
			 * GEM_CLOSE, as on Linux: done, so Mesa's cache asks MADVISE next, sees it gone and frees it, instead of
			 * taking it for busy and asking again (3412 refused waits a boot in 0290) */
			bool purged = b == NULL && r.wait.handle != 0 && r.wait.handle <= MALI_BUFFERS &&
			    fBuffers[r.wait.handle - 1].purged;
			IOLockUnlock(gMaliLock);
			timeout = r.wait.timeout_ns;
			if (b == NULL && !purged) {
				return kIOReturnNotFound;
			}
		}
		ret = seq == 0 || hakios_mali_wait(seq, timeout > 0 ? (uint64_t) timeout : 0) ? kIOReturnSuccess : kIOReturnTimeout;
		if (selector == kPanfrostWaitBO && ret == kIOReturnSuccess && seq != 0) {
			/* the program is about to read what the GPU wrote: a surface's lines invalidated (flushSurface) */
			IOLockLock(gMaliLock);
			b = buffer(r.wait.handle);
			if (b != NULL && b->surface != NULL) {
				flushSurface(b->surface);
			}
			IOLockUnlock(gMaliLock);
		}
		if (args->structureOutput != NULL && args->structureOutputSize >= size) {
			memcpy(args->structureOutput, &r, size);
			args->structureOutputSize = (uint32_t) size;
		}
		return ret;
	}
	IOLockLock(gMaliLock);
	if (fRequests++ < 4) {                  /* B2L: 4, not 32: every line also lands on the boot console */
		IOLog("HakiosMali: request %#x, %zu bytes\n", selector, size);
	}
	switch (selector) {
	case kPanfrostGetParam:
		if (!hakios_mali_parameter(r.param.param, &r.param.value)) {
			ret = kIOReturnBadArgument;
		}
		break;
	case kPanfrostCreateBO:
		ret = createBO(&r.create);
		break;
	case kPanfrostMmapBO:                   /* the offset is the handle: the program maps client memory of that type */
		ret = buffer(r.mmap.handle) != NULL ? kIOReturnSuccess : kIOReturnNotFound;
		r.mmap.offset = (uint64_t) r.mmap.handle << 12;
		break;
	case kPanfrostGetBOOffset:
		b = buffer(r.offset.handle);
		ret = b != NULL ? kIOReturnSuccess : kIOReturnNotFound;
		r.offset.offset = b != NULL ? b->address : 0;
		break;
	case kPanfrostSubmit: {
		/* queued, not run here: the job's number goes back in `jc`, and every buffer it names
		 * remembers it (kPanfrostWaitBO, and no buffer is freed under a job that uses it) */
		uint64_t seq = 0;

		fSubmits++;
		gMaliSubmits++;
		uint64_t b2l_t0 = mach_absolute_time();   /* B1L-0095: the submit's own processor time */
		/* first what the processors wrote into the surfaces it names, out to memory (flushSurface) */
		for (uint32_t i = 0; gMaliFlush && r.submit.jc != 0 && i < r.submit.bo_handle_count && i < 4096; i += 64) {
			uint32_t handles[64];
			uint32_t n = r.submit.bo_handle_count - i < 64 ? r.submit.bo_handle_count - i : 64;

			if (copyin((user_addr_t) (r.submit.bo_handles + i * sizeof(uint32_t)), handles, n * sizeof(uint32_t)) != 0) {
				break;
			}
			for (uint32_t k = 0; k < n; k++) {
				b = buffer(handles[k]);
				if (b != NULL && b->surface != NULL) {
					flushBeforeJob(b->surface);
				}
			}
		}
		if (r.submit.jc != 0) {
			seq = hakios_mali_enqueue(r.submit.jc, r.submit.requirements & PANFROST_JD_REQ_FS);
		}
		b2l_perf_note(B2L_PERF_SUBMIT, mach_absolute_time() - b2l_t0, 0);
		for (uint32_t i = 0; seq != 0 && i < r.submit.bo_handle_count && i < 4096; i += 64) {
			uint32_t handles[64];
			uint32_t n = r.submit.bo_handle_count - i < 64 ? r.submit.bo_handle_count - i : 64;

			if (copyin((user_addr_t) (r.submit.bo_handles + i * sizeof(uint32_t)), handles, n * sizeof(uint32_t)) != 0) {
				break;
			}
			for (uint32_t k = 0; k < n; k++) {
				b = buffer(handles[k]);
				if (b != NULL) {
					b->last_seq = seq;
					if (b->surface != NULL) {
						b->surface->maliSeq = seq;      /* (HakiosMaliDrawn) */
					}
				}
			}
		}
		r.submit.jc = seq;
		break;
	}
	case kHakiosMaliSurface:
		ret = surfaceBuffer(&r.surface);
		break;
	case kHakiosMaliSurfaceSeq: {
		MiniIOSurface * surface = MiniIOSurfaceCopy(r.sseq.surface);

		if (surface != NULL) {
			/* drawing begins into a surface the display still holds (queued or being copied): what it
			 * shows would be part this frame, part the next -- counted, the first few by name */
			static uint32_t overDisplayed;

			(void) overDisplayed;   /* B2L: no display use counts on B2LSurface */
			surface->maliSeq = r.sseq.seq;
			MiniIOSurfaceDrop(surface);
			wakeup((caddr_t) &surface->maliSeq);
		} else {
			ret = kIOReturnNotFound;
		}
		break;
	}
	case kHakiosMaliShow: {
		struct mali_show * show = (struct mali_show *) &r;
		uint64_t bytes = (uint64_t) show->rowBytes * show->height;
		uint8_t * pixels;

		if (show->width == 0 || show->width > 4096 || show->height == 0 || show->height > 4096 ||
		    show->rowBytes < 4 * show->width || (pixels = (uint8_t *) IOMallocData(bytes)) == NULL) {
			ret = kIOReturnBadArgument;
			break;
		}
		if (copyin((user_addr_t) show->pixels, pixels, bytes) != 0) {
			ret = kIOReturnVMError;
		} else {
			MiniIOMobileFramebufferShow(pixels, show->width, show->height, show->rowBytes);
			gMaliShown++;
		}
		IOFreeData(pixels, bytes);
		break;
	}
	case kPanfrostWaitBO:                   /* (answered above, without the lock) */
		ret = buffer(r.wait.handle) != NULL ? kIOReturnSuccess : kIOReturnNotFound;
		break;
	case kPanfrostMadvise:
		r.madvise.retained = 1;
		/* B1L-0258: a purged buffer stays purged until GEM_CLOSE, as on Linux; Mesa then frees it */
		if (r.madvise.handle != 0 && r.madvise.handle <= MALI_BUFFERS && fBuffers[r.madvise.handle - 1].purged) {
			r.madvise.retained = 0;
			break;
		}
		b = buffer(r.madvise.handle);   /* B1L-0247: counted */
		if (b != NULL) {
			b->dontneed = r.madvise.madv == 1;   /* Linux PANFROST_MADV_DONTNEED */
		}
		break;
	case kDrmGemClose:
		b = buffer(r.close.handle);
		if (b != NULL) {
			freeBuffer(b);
		} else if (r.close.handle != 0 && r.close.handle <= MALI_BUFFERS && fBuffers[r.close.handle - 1].purged) {
			bzero(&fBuffers[r.close.handle - 1], sizeof(fBuffers[0]));   /* B1L-0258: the purged handle, given back */
		} else {
			ret = kIOReturnNotFound;
		}
		break;
	default:
		IOLog("HakiosMali: request %#x (%zu bytes) is not known here\n", selector, size);
		ret = kIOReturnUnsupported;
		break;
	}
	IOLockUnlock(gMaliLock);

	if (args->structureOutput != NULL && args->structureOutputSize >= size) {
		memcpy(args->structureOutput, &r, size);
		args->structureOutputSize = (uint32_t) size;
	}
	return ret;
}

/*
 * B1L-0128: where a faulting job got the address it read. Called from the GPU's fault path (b2l_mali_core.c) while the
 * job is unfinished, so none of its buffers (last_seq == seq) can be freed under this: freeBuffer waits for that job.
 * Every 64-bit word of those buffers that points at most 1 MB below the faulting address is said, with where it is
 * and the words around it: which descriptor carried the wrong address.
 */
extern "C" void
hakios_mali_scan(uint64_t seq, uint64_t address)
{
	unsigned said = 0, scanned = 0;
	if (seq == 0) {
		return;
	}
	for (int c = 0; c < B2L_SCAN_CLIENTS && said < 12; c++) {
		struct mali_buffer * table = b2l_scan_tables[c];
		if (table == NULL) {
			continue;
		}
		for (uint32_t h = 0; h < MALI_BUFFERS && said < 12; h++) {
			struct mali_buffer * b = &table[h];
			if (b->memory == NULL || b->last_seq != seq || b->surface != NULL) {
				continue;
			}
			IOBufferMemoryDescriptor * bmd = OSDynamicCast(IOBufferMemoryDescriptor, b->memory);
			const uint64_t * w = bmd != NULL ? (const uint64_t *) bmd->getBytesNoCopy() : NULL;
			if (w == NULL) {
				continue;
			}
			scanned++;
			uint64_t words = bmd->getLength() / 8;
			for (uint64_t k = 0; k < words && said < 12; k++) {
				uint64_t v = w[k] & 0x0000FFFFFFFFFFC0ULL;
				if (v != 0 && v <= address && address - v < (1ull << 20)) {
					printf("mali:   B1L-0128: job %llu: buffer %u (%#llx bytes at GPU %#llx) holds %#llx at byte %#llx "
					    "(%#llx below the fault); around: %#llx %#llx [%#llx] %#llx %#llx\n", seq, h + 1, b->size,
					    b->address, w[k], k * 8, address - v, k >= 2 ? w[k - 2] : 0, k >= 1 ? w[k - 1] : 0, w[k],
					    k + 1 < words ? w[k + 1] : 0, k + 2 < words ? w[k + 2] : 0);
					said++;
				}
			}
		}
	}
	printf("mali:   B1L-0128: job %llu: %u of its buffers scanned, %u words pointing just below the fault\n", seq,
	    scanned, said);
}

/*
 * B1L-0247 (diagnostic): the GPU memory each client holds, and how much of it Mesa's buffer cache has marked purgeable
 * (kept anyway: kPanfrostMadvise always answers "retained"). Printed by the core's 60 s line (b2l_mali_core.c).
 */
extern "C" void
b2l_mali_mem_report(void)
{
	if (gMaliLock == NULL) {
		return;
	}
	IOLockLock(gMaliLock);
	for (int c = 0; c < B2L_SCAN_CLIENTS; c++) {
		struct mali_buffer * table = b2l_scan_tables[c];
		uint64_t live = 0, purgeable = 0, surfaces = 0;
		uint32_t n = 0;
		if (table == NULL) {
			continue;
		}
		for (uint32_t h = 0; h < MALI_BUFFERS; h++) {
			struct mali_buffer * b = &table[h];
			if (b->memory == NULL) {
				continue;
			}
			n++;
			if (b->surface != NULL) {
				surfaces += b->size;
			} else {
				live += b->size;
				if (b->dontneed) {
					purgeable += b->size;
				}
			}
		}
		printf("mali mem: %s: %llu MB in %u buffers, %llu MB of them marked purgeable; IOSurfaces %llu MB\n",
		    b2l_scan_names[c], live >> 20, n, purgeable >> 20, surfaces >> 20);
	}
	IOLockUnlock(gMaliLock);
}
