/* SPDX-License-Identifier: BSD-2-Clause
 * B1L-0080, Metal M6b: the plug-in Metal loads for IOAcceleratorES when the kernel's switch b2l_metal_ui is on
 * (MetalPluginName B2LMetalV4, MetalPluginClassName B2LMetalUIDevice; B2LMetalAccel.cpp). It is a thin bundle over
 * Scion's Mali driver, libhakiosmetal (linked by its install name, so every process shares the one image the tests and
 * M6a also load): B2LMetalUIDevice is HakiosMetalDevice, shown ONLY to the window server, backboardd -- QuartzCore
 * there takes it instead of the CPU renderer. Every other process gets no device from Metal's discovery, as before.
 * Log prefix "b2l-plugin:" (kern.hakios.say).
 */
#import <Foundation/Foundation.h>
#include <IOKit/IOKitLib.h>
#include <stdarg.h>
#include <stdio.h>
#include <string.h>
#include <sys/sysctl.h>
#include <unistd.h>

@interface HakiosMetalDevice : NSObject
- (instancetype)initWithAcceleratorPort:(io_service_t)port;
@end

@interface B2LMetalUIDevice : HakiosMetalDevice
@end

static void
psay(const char *format, ...)
{
	char line[400], out[440];
	va_list ap;
	va_start(ap, format);
	vsnprintf(line, sizeof(line), format, ap);
	va_end(ap);
	snprintf(out, sizeof(out), "b2l-plugin: %s", line);
	(void)sysctlbyname("kern.hakios.say", NULL, NULL, out, strlen(out));
}

@implementation B2LMetalUIDevice
- (instancetype)initWithAcceleratorPort:(io_service_t)port
{
	/* B2L (B1L-0185): the window server, and the processes that draw icons, widgets and Control Center glyphs
	 * offscreen and get nothing on screen without a GPU (SpringBoard, chronod, iconservicesagent) */
	/* B1L-0269: the window server alone again, as before B1L-0185. Icons, widgets and Control Center glyphs are
	 * drawn without the GPU by Apple's own code (right, but Clock, Calendar and widgets stay empty as in 0184).
	 * Boot-arg b2l_gpu_icons=1 brings back the 0185 list, b2l_gpu_all=1 every process (the 0248 behaviour).
	 * A process the sandbox stops from reading kern.bootargs reads neither, so it stays without a GPU. */
	/* B1L-0292 (O1, docs/journal/suivi-plan-gpu.md): every process gets the Mali but those that draw the home screen's icons,
	 * widgets, snapshots and posters, whose GPU drawing is still wrong (enquete-icones-lenteur.md). The first three
	 * come back with b2l_gpu_icons=1, all of them with b2l_gpu_all=1; b2l_gpu_apps=0 is 0269 again (backboardd alone).
	 * Shaders are now kept on disk for every process (B1L-0290), so each compiles once, not at every boot. */
	static const char *const denied[] = { "SpringBoard", "chronod", "iconservicesagent", "WidgetRenderer_Default",
	    "SBRendererService", "PosterBoard", "MercuryPosterExtension",
	    "com.apple.WebKit.GPU" };   /* B1L-0294: Safari's pages came out white with it on the Mali (0292/0293) */
	const char *me = getprogname();
	char args[1024] = "";
	size_t len = sizeof(args) - 1;
	(void)sysctlbyname("kern.bootargs", args, &len, NULL, 0);
	bool icons = strstr(args, "b2l_gpu_icons=1") != NULL;
	bool ok = strstr(args, "b2l_gpu_all=1") != NULL || (me != NULL && strcmp(me, "backboardd") == 0);
	if (!ok && me != NULL && strstr(args, "b2l_gpu_apps=0") == NULL) {
		ok = true;
		for (unsigned i = 0; i < sizeof(denied) / sizeof(denied[0]); i++)
			if (strcmp(me, denied[i]) == 0)
				ok = icons && i < 3;
	}
	if (!ok) {
		/* not the window server: no device, as the hidden plug-in of B1L-0049. NOT released: -[_MTLDevice dealloc]
		 * on an object _MTLDevice never initialised crashed SpringBoard four times a boot (B1L-0085..0087, EXC_BAD_ACCESS
		 * in its dealloc, via -[HakiosMetalDevice dealloc]); one unused object a process is the price. */
		return nil;
	}
	psay("%s (pid %d) asks for a device: the Mali's", me, getpid());
	self = [super initWithAcceleratorPort:port];
	psay("device %s", self != nil ? "made" : "NOT made");
	return self;
}
@end
