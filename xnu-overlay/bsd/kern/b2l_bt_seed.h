/* SPDX-License-Identifier: BSD-2-Clause */
/* Bluetooth step 3 (docs/bluetooth-plan.md section 6). Included by b2l_userspace_seed.h
 * after b2l_apps_publish() and the b2l_bt_fetch() declaration; called from
 * b2l_userspace_before_init() in place of the bare b2l_bt_fetch().
 * 1. b2l_bt_fetch(): bt.hcd from SYSTEM:/b2l-wifi/ into the kernel (B2LBluetooth.cpp).
 * 2. /usr/local/lib/b2l-bluetool-v3.dylib (B1L-0274; v1 and v2 stay on disk, unused) (BlueTool stand-in, userspace/b2l-bluetool.c),
 *    once; a later version takes a new file name. Kill switch: b2l_bt_ios=0 (no dylib,
 *    and b2l_exec_bluetooth.h then adds nothing either). */
#ifndef B2L_BT_SEED_H
#define B2L_BT_SEED_H
#include "b2l_bluetool_payload.h"

/* B1L-0272: bluetoothd never started in 0271 -- the clean USERDATA of B1L-0250 (scripts/prepare-ios-root.py,
 * launchd_disabled()) lists com.apple.bluetoothd as disabled. Before launchd reads it, the key is renamed in place to
 * com.apple.bluetoothX (same length: nothing moves, binary or XML plist), so bluetoothd is enabled again.
 * Returns 1 renamed, 0 key absent, -errno. Boot-arg b2l_bt_launchd: 1 (default) enable, 2 rename it back (bluetoothd disabled again), 0 leave the file alone. */
#define B2L_BT_DISABLED "/private/var/db/com.apple.xpc.launchd/disabled.plist"
static int
b2l_bt_launchd(vfs_context_t ctx, int mode)
{
	static char buf[64 * 1024];
	const char *from = mode == 2 ? "com.apple.bluetoothX" : "com.apple.bluetoothd";
	const size_t kl = 20;
	vnode_t vp = NULL;
	int error = vnode_open(B2L_BT_DISABLED, FREAD | FWRITE, 0, VNODE_LOOKUP_NOFOLLOW_ANY, &vp, ctx);
	if (error) return error == ENOENT ? 0 : -error;
	struct vnode_attr va;
	VATTR_INIT(&va);
	VATTR_WANTED(&va, va_data_size);
	error = vnode_getattr(vp, &va, ctx);
	size_t size = error ? 0 : (size_t)va.va_data_size;
	if (!error && (size < kl || size > sizeof(buf))) error = EFBIG;
	int resid = 0;
	if (!error) error = vn_rdwr(UIO_READ, vp, buf, (int)size, 0, UIO_SYSSPACE, 0, kauth_cred_get(), &resid, current_proc());
	if (!error && resid) error = EIO;
	bool found = false;
	for (size_t i = 3; !error && i + kl <= size; i++) {
		if (memcmp(buf + i, from, kl)) continue;
		bool xml = buf[i - 1] == '>' && i + kl < size && buf[i + kl] == '<';
		bool bin = (uint8_t)buf[i - 3] == 0x5f && buf[i - 2] == 0x10 && buf[i - 1] == (char)kl;   /* ASCII string, 20 bytes */
		if (!xml && !bin) continue;
		char last = mode == 2 ? 'd' : 'X';
		error = vn_rdwr(UIO_WRITE, vp, &last, 1, (off_t)(i + kl - 1), UIO_SYSSPACE, IO_SYNC, kauth_cred_get(), &resid, current_proc());
		if (!error && resid) error = EIO;
		found = true;
		break;
	}
	if (!error && found) error = VNOP_FSYNC(vp, MNT_WAIT, ctx);
	int ce = vnode_close(vp, FREAD | FWRITE, ctx);
	if (error || ce) return -(error ? error : ce);
	return found;
}

static void
b2l_bt_seed(vfs_context_t ctx)
{
	b2l_bt_fetch();
	int enabled = 1;
	PE_parse_boot_argn("b2l_bt_ios", &enabled, sizeof(enabled));
	if (!enabled) return;
	int lmode = 1;
	PE_parse_boot_argn("b2l_bt_launchd", &lmode, sizeof(lmode));
	if (lmode == 1 || lmode == 2) {
		int r = b2l_bt_launchd(ctx, lmode);
		printf("B2L BLUETOOTH SEED: launchd override of bluetoothd: %s (%d)\n",
		    r == 1 ? (lmode == 2 ? "put back, bluetoothd disabled" : "renamed, bluetoothd enabled") :
		    r == 0 ? "key absent, nothing written" : "error", r);
	}
	int e = b2l_apps_directory("/usr/local", ctx);
	if (!e) e = b2l_apps_directory("/usr/local/lib", ctx);
	if (!e) e = b2l_apps_publish("/usr/local/lib/b2l-bluetool-v3.dylib", "/usr/local/lib/.b2l-bluetool-v3.dylib",
	    b2l_bt_tool_dylib, sizeof(b2l_bt_tool_dylib), 0755, ctx);
	printf("B2L BLUETOOTH SEED: BlueTool stand-in publication error=%d\n", e);
}
#endif
