// SPDX-License-Identifier: BSD-2-Clause
// B1L-0342: OpenGL ES for apps (Geometry Dash) over the EAGL bridge (build-gl/src/eagl.c, which only does what the
// window server asks of EAGLContext). What an app asks besides, read off OpenGLES.framework 23G90 (otool):
//  - -initWithAPI:sharegroup: goes to -commonInitWithAPI:properties:, not to the -initWithAPI:properties: the bridge
//    replaces: sent there instead (-initWithAPI: already is).
//  - +setCurrentContext: puts the context's ivar _private (offset 8) into TLS slot 30; every gl* function then calls
//    entry(*(P + 0x20), ...) at P + 0x20 + offset, and +currentContext answers *(P + 0). setCurrentContext also reads
//    the byte at *(P + 8) + 0x18 and, when its bit 0 is clear, calls two functions of the GL engine (none here): set.
//    So each context gets a P: [0] the context, [1] that flag block, from 0x20 a copy of the bridge's table.
//  - -renderbufferStorage:fromDrawable: / -presentRenderbuffer: (CAEAGLLayer): the renderbuffer gets ordinary storage
//    of the layer's size in pixels; a present has the GPU copy it, turned over, into the next of three IOSurfaces
//    (B1L-0350: textures of their memory, hakios_gl_surface_texture), which becomes the layer's contents. Where the
//    library does not take the surfaces, it is read back and converted by the processor as before 0350.
//    shortcut: a present waits for the GPU (glFinish), upgrade once the surfaces' use counts are real.
#import <Foundation/Foundation.h>
#import <QuartzCore/QuartzCore.h>
#import <UIKit/UIKit.h>
#include <IOSurface/IOSurfaceRef.h>
#include <objc/message.h>
#include <objc/runtime.h>
#include <pthread.h>
#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/sysctl.h>
#include <time.h>
#include <unistd.h>

typedef unsigned int GLenum;
typedef unsigned int GLuint;
typedef int GLint;
typedef int GLsizei;
#define GL_RGBA 0x1908
#define GL_UNSIGNED_BYTE 0x1401
#define GL_PACK_ALIGNMENT 0x0d05
#define GL_RENDERBUFFER 0x8d41
#define GL_RENDERBUFFER_BINDING 0x8ca7
#define GL_READ_FRAMEBUFFER 0x8ca8
#define GL_READ_FRAMEBUFFER_BINDING 0x8caa
#define GL_COLOR_ATTACHMENT0 0x8ce0
#define GL_RGBA8 0x8058
#define GL_TEXTURE_2D 0x0de1
#define GL_TEXTURE_BINDING_2D 0x8069
#define GL_DRAW_FRAMEBUFFER 0x8ca9
#define GL_DRAW_FRAMEBUFFER_BINDING 0x8ca6
#define GL_FRAMEBUFFER_COMPLETE 0x8cd5
#define GL_SCISSOR_TEST 0x0c11
#define GL_COLOR_BUFFER_BIT 0x4000
#define GL_NEAREST 0x2600
// Mesa's (libOSMesa, linked), on the context the bridge made current
extern void glGetIntegerv(GLenum, GLint *);
extern void glRenderbufferStorage(GLenum, GLenum, GLsizei, GLsizei);
extern void glGenFramebuffers(GLsizei, GLuint *);
extern void glDeleteFramebuffers(GLsizei, const GLuint *);
extern void glBindFramebuffer(GLenum, GLuint);
extern void glFramebufferRenderbuffer(GLenum, GLenum, GLenum, GLuint);
extern void glReadPixels(GLint, GLint, GLsizei, GLsizei, GLenum, GLenum, void *);
extern void glPixelStorei(GLenum, GLint);
extern void glGenTextures(GLsizei, GLuint *);
extern void glDeleteTextures(GLsizei, const GLuint *);
extern void glBindTexture(GLenum, GLuint);
extern void glFramebufferTexture2D(GLenum, GLenum, GLenum, GLuint, GLint);
extern GLenum glCheckFramebufferStatus(GLenum);
extern void glBlitFramebuffer(GLint, GLint, GLint, GLint, GLint, GLint, GLint, GLint, unsigned, GLenum);
extern unsigned char glIsEnabled(GLenum);
extern void glEnable(GLenum);
extern void glDisable(GLenum);
extern void glFinish(void);
extern int hakios_gl_surface_texture(void *gl, uint32_t surface);   // gl_osmesa.c

// B1L-0344: a ring of three, and never the surface on the screen or one the compositor still holds (IOSurfaceIsInUse):
// with two, half the frames were written into the one being shown and the picture went back and forth. Idea from
// Jesvi-iOS userland/src/eagl_app.m present() (MIT, the HakiOS authors; commit 1b41d5b), written anew here.
@interface NSObject (B2LEAGL)
- (void)setContentsChanged;
@end
#define SURFACES 3
#define TABLE_BYTES 0x2200      // eagl.c: void *table[SLOTS], SLOTS 0x440
#define OFF_GET_ERROR 0x338     // eagl_slots.txt
#define OFF_FINISH 0x2d0

static void say(const char *format, ...)
{
	char line[400];
	va_list a;
	va_start(a, format);
	int n = vsnprintf(line, sizeof(line), format, a);
	va_end(a);
	if (n < 0) return;
	if (n >= (int)sizeof(line)) n = sizeof(line) - 1;
	(void)write(2, line, (size_t)n);
	(void)sysctlbyname("kern.hakios.say", NULL, NULL, line, (size_t)n);
}

struct drawable {
	id context;                 // not retained: the slot goes with the context (my_dealloc)
	void *priv;                 // the P above
	CALayer *layer;             // retained
	GLuint renderbuffer, reader;
	GLsizei width, height;
	IOSurfaceRef surfaces[SURFACES];
	GLuint textures[SURFACES], writers[SURFACES];   // B1L-0350: each surface's memory, to draw into
	int by_gpu;
	uint64_t present_ns, present_max_ns;
	int shown;                  // index + 1 of the surface the layer shows, 0: none
	unsigned presents;
	uint8_t *pixels;
};
static struct drawable drawables[8];

static struct drawable *drawable_of(id context, int make)
{
	for (unsigned i = 0; i < 8; i++)
		if (drawables[i].context == context) return &drawables[i];
	if (!make) return NULL;
	for (unsigned i = 0; i < 8; i++)
		if (drawables[i].context == nil) { drawables[i].context = context; return &drawables[i]; }
	return NULL;
}

// The bridge's table, through the context (the bridge's -getMacroContextPrivate): entry(table[0], ...)
static void **table_of(id context)
{
	return ((void **(*)(id, SEL))objc_msgSend)(context, sel_registerName("getMacroContextPrivate"));
}

// Any call through the table makes the context the thread's current one in the bridge (eagl_enter)
static void make_current(void **table)
{
	((GLenum(*)(void *))table[OFF_GET_ERROR / 8])(table[0]);
}

static id (*bridge_init)(id, SEL, unsigned long, id);   // -initWithAPI:properties: as the bridge left it

static id give_private(id self)
{
	if (self == nil) return nil;
	void **table = table_of(self);
	struct drawable *d = drawable_of(self, 1);
	uint8_t *p = calloc(1, 0x20 + TABLE_BYTES), *flags = calloc(1, 0x20);
	if (table == NULL || d == NULL || p == NULL || flags == NULL) {
		say("b2l-gl-app: no room for context %p\n", self);
		free(p); free(flags);
		return self;            // the bridge's own context still draws; only gl* through OpenGLES will not reach it
	}
	flags[0x18] = 1;
	((void **)p)[0] = (__bridge void *)self;
	((void **)p)[1] = flags;
	memcpy(p + 0x20, table, TABLE_BYTES);
	*(void **)((uint8_t *)(__bridge void *)self + 8) = p;   // _OBJC_IVAR_$_EAGLContext._private == 8
	d->priv = p;
	say("b2l-gl-app: context %p: _private %p, table copied\n", self, p);
	return self;
}

static id my_initWithAPIProperties(id self, SEL s, unsigned long api, id properties)
{
	return give_private(bridge_init(self, s, api, properties));
}

static id my_initWithAPISharegroup(id self, SEL s, unsigned long api, id sharegroup)
{
	(void)s;
	if (sharegroup != nil) say("b2l-gl-app: sharegroup %p asked for, not shared\n", sharegroup);
	return ((id(*)(id, SEL, unsigned long, id))objc_msgSend)(self, sel_registerName("initWithAPI:properties:"), api, nil);
}

// B1L-0350: a framebuffer on each surface's own memory. 0: one was refused, presents go through the processor
static int make_writers(struct drawable *d)
{
	GLint texture = 0, draw = 0;
	int made = 1;
	glGetIntegerv(GL_TEXTURE_BINDING_2D, &texture);
	glGetIntegerv(GL_DRAW_FRAMEBUFFER_BINDING, &draw);
	glGenTextures(SURFACES, d->textures);
	glGenFramebuffers(SURFACES, d->writers);
	for (unsigned i = 0; i < SURFACES && made; i++) {
		glBindTexture(GL_TEXTURE_2D, d->textures[i]);
		made = d->surfaces[i] && hakios_gl_surface_texture(NULL, IOSurfaceGetID(d->surfaces[i]));
		if (!made) break;
		glBindFramebuffer(GL_DRAW_FRAMEBUFFER, d->writers[i]);
		glFramebufferTexture2D(GL_DRAW_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, d->textures[i], 0);
		made = glCheckFramebufferStatus(GL_DRAW_FRAMEBUFFER) == GL_FRAMEBUFFER_COMPLETE;
	}
	glBindTexture(GL_TEXTURE_2D, (GLuint)texture);
	glBindFramebuffer(GL_DRAW_FRAMEBUFFER, (GLuint)draw);
	return made;
}

// The renderbuffer (bound to read) into a writer: GL's first row is the bottom one, a surface's the top one.
// A blit is subject to the scissor test, which the game may have left on.
static void blit_turned_over(GLuint writer, GLsizei w, GLsizei h)
{
	GLint draw = 0;
	unsigned char scissor = glIsEnabled(GL_SCISSOR_TEST);
	glGetIntegerv(GL_DRAW_FRAMEBUFFER_BINDING, &draw);
	if (scissor) glDisable(GL_SCISSOR_TEST);
	glBindFramebuffer(GL_DRAW_FRAMEBUFFER, writer);
	glBlitFramebuffer(0, 0, w, h, 0, h, w, 0, GL_COLOR_BUFFER_BIT, GL_NEAREST);
	glBindFramebuffer(GL_DRAW_FRAMEBUFFER, (GLuint)draw);
	if (scissor) glEnable(GL_SCISSOR_TEST);
}

static BOOL my_renderbufferStorage(id self, SEL s, unsigned long target, id drawable)
{
	(void)s;
	struct drawable *d = drawable_of(self, 0);
	void **table = table_of(self);
	if (d == NULL || table == NULL || ![(id)drawable isKindOfClass:[CALayer class]]) {
		say("b2l-gl-app: -renderbufferStorage: context %p, drawable %p: not done\n", self, drawable);
		return NO;
	}
	CALayer *layer = drawable;
	CGFloat scale = layer.contentsScale;
	GLsizei w = (GLsizei)(layer.bounds.size.width * scale + 0.5), h = (GLsizei)(layer.bounds.size.height * scale + 0.5);
	if (w <= 0 || h <= 0 || w > 8192 || h > 8192) {
		say("b2l-gl-app: -renderbufferStorage: layer of %d x %d: not done\n", w, h);
		return NO;
	}
	make_current(table);
	GLint rb = 0, read = 0;
	glGetIntegerv(GL_RENDERBUFFER_BINDING, &rb);
	glRenderbufferStorage((GLenum)target, GL_RGBA8, w, h);
	if (d->reader == 0) glGenFramebuffers(1, &d->reader);
	glGetIntegerv(GL_READ_FRAMEBUFFER_BINDING, &read);
	glBindFramebuffer(GL_READ_FRAMEBUFFER, d->reader);
	glFramebufferRenderbuffer(GL_READ_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_RENDERBUFFER, (GLuint)rb);
	glBindFramebuffer(GL_READ_FRAMEBUFFER, (GLuint)read);
	if (d->width != w || d->height != h) {
		d->shown = 0;
		if (d->writers[0]) {
			glDeleteFramebuffers(SURFACES, d->writers);
			glDeleteTextures(SURFACES, d->textures);
		}
		size_t row = ((size_t)w * 4 + 63) & ~(size_t)63;        // the Mali's linear rows: a multiple of 64 bytes
		for (unsigned i = 0; i < SURFACES; i++) {
			if (d->surfaces[i]) CFRelease(d->surfaces[i]);
			d->surfaces[i] = IOSurfaceCreate((__bridge CFDictionaryRef) @{
				(id)kIOSurfaceWidth: @(w), (id)kIOSurfaceHeight: @(h), (id)kIOSurfaceBytesPerElement: @4,
				(id)kIOSurfaceBytesPerRow: @(row),
				(id)kIOSurfacePixelFormat: @((uint32_t)'BGRA') });
		}
		free(d->pixels);
		d->pixels = malloc((size_t)w * (size_t)h * 4);
		d->by_gpu = make_writers(d);
	}
	if (d->layer != layer) {
		d->layer = layer;
		CFRetain((__bridge CFTypeRef)layer);
	}
	d->renderbuffer = (GLuint)rb;
	d->width = w;
	d->height = h;
	say("b2l-gl-app: -renderbufferStorage: renderbuffer %d of %d x %d (scale %.2f), %d surfaces, presented by the %s\n",
	    rb, w, h, (double)scale, SURFACES, d->by_gpu ? "GPU" : "processor (surfaces refused)");
	return d->surfaces[0] && d->surfaces[1] && d->surfaces[2] && d->pixels;
}

static BOOL my_presentRenderbuffer(id self, SEL s, unsigned long target)
{
	(void)s; (void)target;
	struct drawable *d = drawable_of(self, 0);
	void **table = table_of(self);
	if (d == NULL || d->pixels == NULL || table == NULL || !d->surfaces[0] || !d->surfaces[1] || !d->surfaces[2]) return NO;
	((void (*)(void *))table[OFF_FINISH / 8])(table[0]);    // the bridge's glFinish: its clear put off, the frame's end
	uint64_t began = clock_gettime_nsec_np(CLOCK_UPTIME_RAW);
	// the next after the one shown: IOSurfaceIsInUse always answers no here, and from the first only two were used
	int first = d->shown % SURFACES, k = first;     // all held: that one all the same
	for (int j = 0; j < SURFACES - 1; j++) {
		int i = (first + j) % SURFACES;
		if (!IOSurfaceIsInUse(d->surfaces[i])) { k = i; break; }
	}
	IOSurfaceRef surface = d->surfaces[k];
	GLint read = 0, pack = 4;
	glGetIntegerv(GL_READ_FRAMEBUFFER_BINDING, &read);
	glGetIntegerv(GL_PACK_ALIGNMENT, &pack);
	glBindFramebuffer(GL_READ_FRAMEBUFFER, d->reader);
	if (d->by_gpu) {
		blit_turned_over(d->writers[k], d->width, d->height);
		glFinish();     // the compositor takes the surface as soon as it is told of it
	} else {
		glPixelStorei(GL_PACK_ALIGNMENT, 4);
		glReadPixels(0, 0, d->width, d->height, GL_RGBA, GL_UNSIGNED_BYTE, d->pixels);
		glPixelStorei(GL_PACK_ALIGNMENT, pack);
	}
	int log_it = d->presents == 0 || (d->presents + 1) % 600 == 0;
	// B1L-0352: never read the surface back here: mapping it has the kernel clean it before every job (B1L-0351)
	glBindFramebuffer(GL_READ_FRAMEBUFFER, (GLuint)read);
	if (IOSurfaceLock(surface, 0, NULL) != 0) return NO;    // by the GPU too: the unlock moves the seed
	if (!d->by_gpu) {
		uint8_t *base = IOSurfaceGetBaseAddress(surface);
		size_t row = IOSurfaceGetBytesPerRow(surface), w = (size_t)d->width;
		for (GLsizei y = 0; y < d->height; y++) {         // bottom-up RGBA into top-down BGRA
			const uint32_t *from = (const uint32_t *)(d->pixels + (size_t)(d->height - 1 - y) * w * 4);
			uint32_t *to = (uint32_t *)(base + (size_t)y * row);
			for (size_t x = 0; x < w; x++) {
				uint32_t v = from[x];
				to[x] = (v & 0xff00ff00u) | ((v & 0xffu) << 16) | ((v >> 16) & 0xffu);
			}
		}
	}
	IOSurfaceUnlock(surface, 0, NULL);
	[CATransaction begin];
	[CATransaction setDisableActions:YES];
	d->layer.contents = (__bridge id)surface;
	if ([d->layer respondsToSelector:@selector(setContentsChanged)]) [(id)d->layer setContentsChanged];
	[CATransaction commit];
	d->shown = k + 1;
	if (![NSThread isMainThread]) [CATransaction flush];
	uint64_t took = clock_gettime_nsec_np(CLOCK_UPTIME_RAW) - began;
	d->present_ns += took;
	if (took > d->present_max_ns) d->present_max_ns = took;
	d->presents++;
	if (log_it) {
		unsigned n = d->presents == 1 ? 1 : d->presents == 600 ? 599 : 600;
		say("b2l-gl-app: present %u: %d x %d into surface %u by the %s, %llu us (worst %llu)\n",
		    d->presents, d->width, d->height, IOSurfaceGetID(surface), d->by_gpu ? "GPU" : "processor",
		    (unsigned long long)(d->present_ns / n / 1000), (unsigned long long)(d->present_max_ns / 1000));
		d->present_ns = d->present_max_ns = 0;
	}
	return YES;
}

static BOOL my_presentRenderbufferAt(id self, SEL s, unsigned long target, double when)
{
	(void)when;
	return my_presentRenderbuffer(self, s, target);
}

static void (*bridge_dealloc)(id, SEL);
static void my_dealloc(id self, SEL s)
{
	struct drawable *d = drawable_of(self, 0);
	if (d != NULL) {
		if (d->layer) CFRelease((__bridge CFTypeRef)d->layer);
		for (unsigned i = 0; i < SURFACES; i++) if (d->surfaces[i]) CFRelease(d->surfaces[i]);
		free(d->pixels);
		void *p = d->priv;
		memset(d, 0, sizeof(*d));
		*(void **)((uint8_t *)(__bridge void *)self + 8) = NULL;
		if (p) { free(((void **)p)[1]); free(p); }
	}
	bridge_dealloc(self, s);
}

// B1L-0343: Apple's +setCurrentContext: (eagl_ctx_tls_set) does pthread_setspecific(30, P), then pthread_setspecific of
// a key made by eagl_init (pthread_once), which runs from the -commonInitWithAPI: the bridge never calls: key 0,
// refused, so it answered NO and cocos2d released the context (0342). Only the first is needed: every gl* and
// +currentContext read slot 30. (The second is for a destructor at thread exit; the context is not retained here.)
static BOOL my_setCurrentContext(id cls, SEL s, id context)
{
	(void)cls; (void)s;
	void *p = context ? *(void **)((uint8_t *)(__bridge void *)context + 8) : NULL;
	if (context && p == NULL) {
		say("b2l-gl-app: +setCurrentContext: %p has no _private: refused\n", context);
		return NO;
	}
	int e = pthread_setspecific(30, p);
	static unsigned said;
	if (said++ < 4) say("b2l-gl-app: +setCurrentContext: %p (_private %p) -> %d\n", context, p, e);
	return e == 0;
}

static void swap(Class c, const char *name, IMP ours, IMP *old)
{
	Method m = class_getInstanceMethod(c, sel_registerName(name));
	if (m == NULL) { say("b2l-gl-app: EAGLContext has no -%s\n", name); return; }
	IMP was = method_setImplementation(m, ours);
	if (old) *old = was;
}

// B1L-0344 diagnostic: what touches reach the game's window (0343: the game showed its menu and did not respond)
static void (*window_send)(id, SEL, id);
static void my_sendEvent(id self, SEL s, id event)
{
	static unsigned said;
	if (said < 40 && [event respondsToSelector:@selector(allTouches)]) {
		for (UITouch *t in [(UIEvent *)event allTouches]) {
			if (t.phase != UITouchPhaseBegan || said >= 40) continue;
			said++;
			CGPoint w = [t locationInView:nil];
			CGRect b = [(UIWindow *)self bounds];
			long o = (long)[[(UIWindow *)self windowScene] interfaceOrientation];
			say("b2l-gl-app: touch %u at %.0f,%.0f in window %.0fx%.0f, orientation %ld, view %s\n", said, w.x, w.y,
			    b.size.width, b.size.height, o, t.view ? object_getClassName(t.view) : "none");
		}
	}
	window_send(self, s, event);
}

// After eagl.c's constructor (linked first, so its initialiser runs first: eagl.c comes before this file)
__attribute__((constructor)) static void install(void)
{
	Class c = objc_getClass("EAGLContext");
	if (c == Nil) return;
	if (class_getInstanceMethod(c, sel_registerName("getMacroContextPrivate")) == NULL) return;
	swap(c, "initWithAPI:properties:", (IMP)my_initWithAPIProperties, (IMP *)&bridge_init);
	swap(c, "initWithAPI:sharegroup:", (IMP)my_initWithAPISharegroup, NULL);
	swap(c, "renderbufferStorage:fromDrawable:", (IMP)my_renderbufferStorage, NULL);
	swap(c, "presentRenderbuffer:", (IMP)my_presentRenderbuffer, NULL);
	swap(c, "presentRenderbuffer:atTime:", (IMP)my_presentRenderbufferAt, NULL);
	swap(c, "presentRenderbuffer:afterMinimumDuration:", (IMP)my_presentRenderbufferAt, NULL);
	swap(c, "dealloc", (IMP)my_dealloc, (IMP *)&bridge_dealloc);
	Method send = class_getInstanceMethod(objc_getClass("UIWindow"), sel_registerName("sendEvent:"));
	if (send) window_send = (void (*)(id, SEL, id))method_setImplementation(send, (IMP)my_sendEvent);
	Method set = class_getClassMethod(c, sel_registerName("setCurrentContext:"));
	if (set) method_setImplementation(set, (IMP)my_setCurrentContext);
	else say("b2l-gl-app: EAGLContext has no +setCurrentContext:\n");
	say("b2l-gl-app: EAGLContext ready for apps (%s)\n", getprogname());
}
