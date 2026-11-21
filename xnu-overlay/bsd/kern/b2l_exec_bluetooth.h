/* SPDX-License-Identifier: BSD-2-Clause */
/* Bluetooth step 3 (docs/bluetooth-plan.md section 6). Included by
 * b2l_exec_graphics_diag.h after b2l_exec_env_has().
 * - bluetoothd: BT_HCI_TRANSPORT=H4:POSIX:<kernel control>,<speed> (format read in
 *   bluetoothd 23G90 at 0x10029798c "transport:driver:path" and 0x100192f90 "name,speed")
 *   and BT_BAUDRATE, so it talks H4 to B2LBluetooth's kernel control.
 * - bluetoothd (B1L-0273): the same library inserted (bluetoothd's platform when MobileGestalt has no product type).
 * - BlueTool: b2l-bluetool.dylib inserted (stand-in answering bluetoothd's
 *   RunBuiltinScript boot/init/deepsleep), its pid allowed by the AMFI dyld policy.
 * Kill switch: boot-arg b2l_bt_ios=0 (nothing added). */
#ifndef B2L_EXEC_BLUETOOTH_H
#define B2L_EXEC_BLUETOOTH_H

#define B2L_BT_TOOL_PATH "/usr/sbin/BlueTool"
#define B2L_BT_TOOL_LIB  "/usr/local/lib/b2l-bluetool-v3.dylib"
int b2l_bt_dyld_pid = -1;
int b2l_btd_dyld_pid = -1;   /* B1L-0273: bluetoothd gets the same library (platform, see userspace/b2l-bluetool.c) */

static int
b2l_exec_bt_add(struct image_params *imgp, int pointer_size, const char *setting, const char *path)
{
	const char *eq = strchr(setting, '=');
	size_t key_len = (size_t)(eq - setting) + 1, len = strlen(setting) + 1;
	if (b2l_exec_env_has(imgp, setting, key_len)) return 0;
	if (imgp->ip_argspace < (int)(len + 3 * pointer_size) || imgp->ip_strspace < (int)len) return 0;
	int error = exec_add_user_string(imgp, CAST_USER_ADDR_T(setting), UIO_SYSSPACE, TRUE);
	if (error) return error;
	imgp->ip_argspace -= pointer_size;
	imgp->ip_envc++;
	printf("B2L BLUETOOTH EXEC: %s for %s\n", setting, path);
	return 0;
}

static int
b2l_exec_bt(struct image_params *imgp, int pointer_size)
{
	int enabled = 1;
	PE_parse_boot_argn("b2l_bt_ios", &enabled, sizeof(enabled));
	if (!enabled || (pointer_size != 4 && pointer_size != 8)) return 0;
	const char *path = imgp->ip_strings + strlen(EXECUTABLE_KEY);
	if (!strcmp(path, "/usr/sbin/bluetoothd")) {
		int error = b2l_exec_bt_add(imgp, pointer_size,
		    "BT_HCI_TRANSPORT=H4:POSIX:com.b2l.bluetooth,115200", path);
		if (error) return error;
		error = b2l_exec_bt_add(imgp, pointer_size, "BT_BAUDRATE=115200", path);
		if (error) return error;
	} else if (strcmp(path, B2L_BT_TOOL_PATH)) return 0;
	bool btd = path[strlen(path) - 1] == 'd';   /* /usr/sbin/bluetoothd, else /usr/sbin/BlueTool */
	vnode_t lib = NULLVP;
	if (vnode_lookup(B2L_BT_TOOL_LIB, 0, &lib, vfs_context_kernel())) {
		printf("B2L BLUETOOTH EXEC: %s missing, %s left alone\n", B2L_BT_TOOL_LIB, path);
		return 0;
	}
	vnode_put(lib);
	*(btd ? &b2l_btd_dyld_pid : &b2l_bt_dyld_pid) = proc_getpid(vfs_context_proc(imgp->ip_vfs_context));
	if (b2l_exec_env_has(imgp, "DYLD_INSERT_LIBRARIES=", sizeof("DYLD_INSERT_LIBRARIES=") - 1)) return 0;
	return b2l_exec_bt_add(imgp, pointer_size, "DYLD_INSERT_LIBRARIES=" B2L_BT_TOOL_LIB, path);
}
#endif
