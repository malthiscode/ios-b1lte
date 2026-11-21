/* SPDX-License-Identifier: BSD-2-Clause */
/* Audio lots 3-4 (docs/audio-revue-et-plan.md section 11). Included by b2l_userspace_seed.h after
 * b2l_gl_publish_one(); called from b2l_userspace_before_init().
 * 1. /dev/hakios-audio, mode 0666 (sys/hakios.audio.h of HakiOS): write() takes whole frames, s16 stereo
 *    48 kHz, never blocks, and hands them to b2l_audio_sink() (B2LAudio.h, A6), which keeps what fits;
 *    ioctl 'r' 1 = the volume iOS sets, ioctl 'r' 2 = a log line of the plug-in (audiomxd has no console).
 * 2. HakiOS's AudioServerPlugIn (MIT, Port iOS 26 to S10/audio/plugin), from SYSTEM:/B2L_HAUD_V/ to
 *    /System/Library/Audio/Plug-Ins/HAL/HakiosAudio.driver/, once per version: the bundle carries an empty folder
 *    .B2L_HAUD_V; without it the bundle there is an older one, moved aside to Plug-Ins/HakiosAudio.driver.before-V first
 *    (B1L-0272: v2 = v1 + an I/O trace to this log and a clock domain).
 * Kill switch: b2l_audio_ios=0 (no device, no plug-in). */
#ifndef B2L_AUDIO_SEED_H
#define B2L_AUDIO_SEED_H
#include <sys/conf.h>
#include <sys/ioccom.h>
#include <miscfs/devfs/devfs.h>

extern uint32_t b2l_audio_sink(const int16_t *frames, uint32_t count);   /* B2LAudio.h */
extern void b2l_audio_volume_set(uint32_t below, uint32_t muted);

struct b2l_haud_volume { uint32_t below, muted; };
#define B2L_HAUD_SET_VOLUME _IOW('r', 1, struct b2l_haud_volume)
#define B2L_HAUD_LOG        _IOW('r', 2, char[200])

#define B2L_HAUD_V   "b2l-audio-v2"
#define B2L_HAUD_DRV "/System/Library/Audio/Plug-Ins/HAL/HakiosAudio.driver"
static unsigned b2l_haud_log_lines;

static int
b2l_haud_open(__unused dev_t dev, __unused int flags, __unused int devtype, __unused struct proc *p)
{
	return 0;
}
static int
b2l_haud_close(__unused dev_t dev, __unused int flags, __unused int devtype, __unused struct proc *p)
{
	return 0;
}
static int
b2l_haud_write(__unused dev_t dev, struct uio *uio, __unused int ioflag)
{
	int16_t buf[512];   /* 256 frames */
	while (uio_resid(uio) > 0) {
		user_ssize_t n = uio_resid(uio);
		if (n > (user_ssize_t)sizeof(buf)) n = sizeof(buf);
		int e = uiomove((caddr_t)buf, (int)n, uio);
		if (e) return e;
		if (n >= 4) (void)b2l_audio_sink(buf, (uint32_t)(n / 4));   /* what is not taken is lost, as on HakiOS */
	}
	return 0;
}
static int
b2l_haud_ioctl(__unused dev_t dev, u_long cmd, caddr_t data, __unused int flag, __unused struct proc *p)
{
	if (cmd == B2L_HAUD_SET_VOLUME) {
		const struct b2l_haud_volume *v = (const struct b2l_haud_volume *)data;
		b2l_audio_volume_set(v->below, v->muted);
		printf("B2L AUDIO: ios-volume-set below=%u muted=%u\n", v->below, v->muted);
		return 0;
	}
	if (cmd == B2L_HAUD_LOG) {
		// shortcut: the first 1500 plug-in lines of a boot only, so a chatty plug-in cannot fill the log.
		if (b2l_haud_log_lines++ < 1500) {
			data[199] = 0;
			printf("B2L AUDIO HAL: %s", data);
		}
		return 0;
	}
	return ENOTTY;
}
static const struct cdevsw b2l_haud_cdevsw = {
	.d_open = b2l_haud_open, .d_close = b2l_haud_close, .d_read = eno_rdwrt,
	.d_write = b2l_haud_write, .d_ioctl = b2l_haud_ioctl, .d_stop = eno_stop,
	.d_reset = eno_reset, .d_ttys = NULL, .d_select = eno_select,
	.d_mmap = eno_mmap, .d_strategy = eno_strat, .d_reserved_1 = eno_getc,
	.d_reserved_2 = eno_putc, .d_type = 0,
};

static void
b2l_audio_seed(vfs_context_t ctx)
{
	int on = 1;
	PE_parse_boot_argn("b2l_audio_ios", &on, sizeof(on));
	if (!on) { printf("B2L AUDIO: ios side disabled (b2l_audio_ios=0)\n"); return; }
	int major = cdevsw_add(-1, &b2l_haud_cdevsw);
	void *node = major < 0 ? NULL : devfs_make_node(makedev(major, 0), DEVFS_CHAR, UID_ROOT, GID_WHEEL, 0666, "hakios-audio");
	int e = 0, moved = 0;
	// shortcut: if a publication fails after the move, the next boot finds .before-V taken and stops (logged); rename it by hand.
	if (b2l_layout_exists(B2L_HAUD_DRV "/." B2L_HAUD_V, ctx) != 1 && b2l_layout_exists(B2L_HAUD_DRV, ctx) == 1) {
		e = b2l_kernel_rename_excl(B2L_HAUD_DRV, "/System/Library/Audio/Plug-Ins/HakiosAudio.driver.before-" B2L_HAUD_V, ctx);
		moved = !e;
	}
	if (!e) e = b2l_apps_directory(B2L_HAUD_DRV, ctx);
	if (!e) e = b2l_gl_publish_one(B2L_HAUD_V "/HakiosAudio", B2L_HAUD_DRV "/HakiosAudio", B2L_HAUD_DRV "/.HakiosAudio", ctx);
	if (!e) e = b2l_gl_publish_one(B2L_HAUD_V "/Info.plist", B2L_HAUD_DRV "/Info.plist", B2L_HAUD_DRV "/.Info.plist", ctx);
	if (!e) e = b2l_apps_directory(B2L_HAUD_DRV "/." B2L_HAUD_V, ctx);
	printf("B2L AUDIO: ios-seed /dev/hakios-audio major=%d node=%d plug-in %s older-moved-aside=%d publication error=%d\n",
	    major, node != NULL, B2L_HAUD_V, moved, e);
}
#endif
