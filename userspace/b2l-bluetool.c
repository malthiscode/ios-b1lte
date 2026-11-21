/* SPDX-License-Identifier: BSD-2-Clause */
/* b2l-bluetool-v3.dylib: stand-in for Apple's BlueTool on the S10 (Bluetooth step 3,
 * docs/bluetooth-plan.md section 6). The kernel (B2LBluetooth.cpp) already powers the
 * BCM4375, loads its patch RAM and sets the address, so the only job left for BlueTool
 * is to answer bluetoothd's XPC requests on com.apple.BlueTool:
 *   {kMsgId: "RunBuiltinScript", kMsgArgs: {script: boot|init|deepsleep, ...}}
 * with a reply {result: 0, firmware, manufacturer} (keys read in bluetoothd 23G90,
 * reply block at 0x10040076c). "boot" and "init" wait until the kernel's H4 control
 * exists (= chip up), everything else answers 0, as ios-pipa's stand-in does.
 * Inserted into /usr/sbin/BlueTool by the kernel's exec hook (b2l_exec_bluetooth.h);
 * BlueTool's main() calls getopt("c:hf:F:R") first, which is where we take over.
 * Also inserted into bluetoothd (v2, fixed in v3): see b2l_MGIsDeviceOneOfType below. */
#include <dispatch/dispatch.h>
#include <xpc/xpc.h>
#include <sys/socket.h>
#include <sys/ioctl.h>
#include <stdarg.h>
#include <stdio.h>
#include <string.h>
#include <time.h>
#include <unistd.h>
#include <stdbool.h>
#include <stdlib.h>
#include <ptrauth.h>
#include <os/log.h>
#include <sys/sysctl.h>
#include <mach-o/dyld.h>
#include <mach-o/loader.h>
#include <dlfcn.h>

#define BT_CTL_NAME   "com.apple.uart.sk.bluetooth"
#define BT_CTL_LOGOPT 0xb2
#define BT_READY_WAIT_MS 60000

/* <sys/kern_control.h> and <sys/sys_domain.h> are macOS-only in the SDK; same ABI on iOS. */
#define SYSPROTO_CONTROL 2
#define AF_SYS_CONTROL   2
struct ctl_info { uint32_t ctl_id; char ctl_name[96]; };
#define CTLIOCGINFO _IOWR('N', 3, struct ctl_info)
struct sockaddr_ctl {
	u_char sc_len, sc_family;
	uint16_t ss_sysaddr;
	uint32_t sc_id, sc_unit, sc_reserved[5];
};
_Static_assert(CTLIOCGINFO == 0xc0644e03, "CTLIOCGINFO as used by bluetoothd 0x100193058");

/* Not marked available on iOS in the SDK headers; present in libxpc. */
extern xpc_connection_t b2l_xpc_mach_service(const char *, dispatch_queue_t, uint64_t)
    __asm__("_xpc_connection_create_mach_service");

static int log_fd = -1;

/* 0: control found (fd connected for logging), -1: not registered yet, 1: socket refused */
static int ctl_open(void)
{
	if (log_fd >= 0) return 0;
	int fd = socket(PF_SYSTEM, SOCK_STREAM, SYSPROTO_CONTROL);
	if (fd < 0) return 1;
	struct ctl_info info;
	memset(&info, 0, sizeof(info));
	strlcpy(info.ctl_name, BT_CTL_NAME, sizeof(info.ctl_name));
	if (ioctl(fd, CTLIOCGINFO, &info)) { close(fd); return -1; }
	struct sockaddr_ctl sc;
	memset(&sc, 0, sizeof(sc));
	sc.sc_len = sizeof(sc);
	sc.sc_family = AF_SYSTEM;
	sc.ss_sysaddr = AF_SYS_CONTROL;
	sc.sc_id = info.ctl_id;
	if (connect(fd, (struct sockaddr *)&sc, sizeof(sc))) { close(fd); return 1; }
	log_fd = fd;
	return 0;
}

static void tool_log(const char *fmt, ...)
{
	char line[160];
	va_list ap;
	va_start(ap, fmt);
	vsnprintf(line, sizeof(line), fmt, ap);
	va_end(ap);
	if (ctl_open() == 0) setsockopt(log_fd, SYSPROTO_CONTROL, BT_CTL_LOGOPT, line, (socklen_t)strlen(line));
}

/* Returns the wait in ms, or -1 when the chip never came up. */
static long wait_ready(void)
{
	for (long ms = 0; ms < BT_READY_WAIT_MS; ms += 250) {
		int r = ctl_open();
		if (r == 0) return ms;
		if (r > 0) return ms;   /* sandbox refused the socket: let bluetoothd try it itself */
		usleep(250 * 1000);
	}
	return -1;
}

static void handle(xpc_object_t msg)
{
	if (xpc_get_type(msg) != XPC_TYPE_DICTIONARY) return;
	const char *id = xpc_dictionary_get_string(msg, "kMsgId");
	xpc_object_t args = xpc_dictionary_get_value(msg, "kMsgArgs");
	const char *script = (args && xpc_get_type(args) == XPC_TYPE_DICTIONARY) ?
	    xpc_dictionary_get_string(args, "script") : NULL;
	int64_t result = 0;
	long waited = 0;
	if (id && !strcmp(id, "RunBuiltinScript") && script && (!strcmp(script, "boot") || !strcmp(script, "init"))) {
		waited = wait_ready();
		if (waited < 0) result = 19;   /* ENODEV: kernel never brought the chip up */
	}
	xpc_object_t reply = xpc_dictionary_create_reply(msg);
	if (reply) {
		xpc_dictionary_set_int64(reply, "result", result);
		xpc_dictionary_set_string(reply, "firmware", "BCM4375B1 b2l");
		xpc_dictionary_set_int64(reply, "manufacturer", 15);   /* Broadcom */
		xpc_connection_send_message(xpc_dictionary_get_remote_connection(msg), reply);
		xpc_release(reply);
	}
	tool_log("%s %s -> %lld (waited %ld ms)%s", id ? id : "?", script ? script : "-",
	    (long long)result, waited, reply ? "" : " no reply wanted");
}

static void __attribute__((noreturn)) tool_main(void)
{
	dispatch_queue_t q = dispatch_queue_create("b2l.bluetool", DISPATCH_QUEUE_SERIAL);
	xpc_connection_t listener = b2l_xpc_mach_service("com.apple.BlueTool", q,
	    XPC_CONNECTION_MACH_SERVICE_LISTENER);
	xpc_connection_set_event_handler(listener, ^(xpc_object_t peer) {
		if (xpc_get_type(peer) != XPC_TYPE_CONNECTION) return;
		xpc_connection_set_target_queue(peer, q);
		xpc_connection_set_event_handler(peer, ^(xpc_object_t m) { handle(m); });
		xpc_connection_resume(peer);
	});
	xpc_connection_resume(listener);
	tool_log("stand-in listening on com.apple.BlueTool (pid %d)", getpid());
	dispatch_main();
}

static int b2l_getopt(int argc, char *const argv[], const char *optstring)
{
	if (optstring && !strcmp(optstring, "c:hf:F:R")) tool_main();
	return getopt(argc, argv, optstring);
}

/* B1L-0273, in bluetoothd: MobileGestalt has no product type on this phone (MGGetProductType() = 0xffffffff), so
 * bluetoothd 23G90's platform table (0x1004312a0) matches no device, its platform object stays NULL and it crashes
 * at 0x1004040bc reading +0x31c (0272). Each table entry is one MGIsDeviceOneOfType() call followed by the platform it
 * creates; we answer "yes" at the one call site of the chosen platform and pass every other call to MobileGestalt.
 * The platform ids follow the order of Apple's Broadcom generations; 0x16 is the most common one (32 device lists).
 * Boot-arg b2l_bt_platform=0x13|0x14|0x16|0x17|0x18|0x19 (default 0x16), 0 = leave MobileGestalt alone. */
extern bool MGIsDeviceOneOfType(const void *, ...);
static const uint8_t btd_uuid[16] = {   /* bluetoothd 23G90: DA7ADF00-04FA-3EE0-8782-E336E0982712 */
	0xda, 0x7a, 0xdf, 0x00, 0x04, 0xfa, 0x3e, 0xe0, 0x87, 0x82, 0xe3, 0x36, 0xe0, 0x98, 0x27, 0x12 };
static const struct { uint32_t platform, ret; } plat_sites[] = {   /* return address after each first call */
	{ 0x13, 0x431c68 }, { 0x14, 0x431e50 }, { 0x16, 0x432160 },
	{ 0x17, 0x432628 }, { 0x18, 0x432f30 }, { 0x19, 0x4326b8 },
};
static uint32_t plat_want;   /* 0: MobileGestalt left alone */
static volatile int plat_seen;

static unsigned long bootarg(const char *key, unsigned long dflt)
{
	char args[1024] = { 0 };
	size_t len = sizeof(args) - 1;
	if (sysctlbyname("kern.bootargs", args, &len, NULL, 0)) return dflt;
	size_t kl = strlen(key);
	for (char *p = args; (p = strstr(p, key)); p += kl)
		if ((p == args || p[-1] == ' ') && p[kl] == '=') return strtoul(p + kl + 1, NULL, 0);
	return dflt;
}

static bool is_bluetoothd(const struct mach_header_64 *mh)
{
	const struct load_command *lc = (const void *)(mh + 1);
	for (uint32_t i = 0; i < mh->ncmds; i++, lc = (const void *)((const char *)lc + lc->cmdsize))
		if (lc->cmd == LC_UUID) return !memcmp(((const struct uuid_command *)lc)->uuid, btd_uuid, 16);
	return false;
}

/* B1L-0274: v2 took bluetoothd's header from _dyld_get_image_header(0), probably not bluetoothd once this library is
 * inserted (0273: no "platform ... armed" line, same crash; bluetoothd binds MGIsDeviceOneOfType normally, auth_got). v3 asks dladdr() which image made each call instead. */
__attribute__((constructor)) static void plat_init(void)
{
	if (strcmp(getprogname(), "bluetoothd")) return;
	plat_want = (uint32_t)bootarg("b2l_bt_platform", 0x16);
	os_log(OS_LOG_DEFAULT, "b2l-bluetool v3: in bluetoothd, platform %#x", plat_want);
	tool_log("v3 in bluetoothd (pid %d), platform %#x", getpid(), plat_want);
}

static bool b2l_MGIsDeviceOneOfType(const void *first, ...)
{
	uintptr_t ret = (uintptr_t)ptrauth_strip(__builtin_return_address(0), ptrauth_key_return_address);
	Dl_info di;
	if (plat_want && dladdr((const void *)ret, &di) && di.dli_fbase && is_bluetoothd(di.dli_fbase)) {
		uintptr_t off = ret - (uintptr_t)di.dli_fbase;   /* image load address = vmaddr 0x100000000 + slide */
		if (!plat_seen++) tool_log("bluetoothd (pid %d): first MobileGestalt device check from +%#lx", getpid(), (unsigned long)off);
		for (unsigned i = 0; i < sizeof(plat_sites) / sizeof(plat_sites[0]); i++)
			if (plat_sites[i].platform == plat_want && plat_sites[i].ret == off) {
				os_log(OS_LOG_DEFAULT, "b2l-bluetool: bluetoothd platform forced to %#x", plat_want);
				tool_log("bluetoothd (pid %d): platform forced to %#x (MobileGestalt has no product type)", getpid(), plat_want);
				return true;
			}
	}
	const void *a[32] = { 0 };   /* the list is NULL-terminated; extra NULLs are harmless */
	va_list ap;
	va_start(ap, first);
	for (int n = 0; n < 31 && (a[n] = va_arg(ap, const void *)); n++) {}
	va_end(ap);
	return MGIsDeviceOneOfType(first, a[0], a[1], a[2], a[3], a[4], a[5], a[6], a[7], a[8], a[9], a[10], a[11],
	    a[12], a[13], a[14], a[15], a[16], a[17], a[18], a[19], a[20], a[21], a[22], a[23], a[24], a[25], a[26],
	    a[27], a[28], a[29], a[30], a[31]);
}

__attribute__((used, section("__DATA,__interpose")))
static const struct { const void *replacement, *replacee; } b2l_interpose[] = {
	{ (const void *)b2l_getopt, (const void *)getopt },
	{ (const void *)b2l_MGIsDeviceOneOfType, (const void *)MGIsDeviceOneOfType },
};
