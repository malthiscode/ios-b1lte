/* SPDX-License-Identifier: BSD-2-Clause
 * B1L-0241: "Test Metal" -- what an ordinary iOS app expects of Metal, run on the Mali by our driver, with the answers
 * on the screen and in the kernel log (prefix "b2l-mtest:"). Written for this port; no code taken from elsewhere. The API
 * used is Apple's public Metal / QuartzCore / IOSurface API only.
 *
 * Installed the quickest way: the kernel already inserts libb2l-metal-app.dylib into an app
 * (Voice Memos since B1L-0242; b2l_exec_graphics_diag.h, B1L-0079, kill switch boot-arg b2l_metal_app=0); this file is now that library. Opening
 * the app runs every test and puts a full-screen list on top of the app; "Fermer" removes it (the app is untouched),
 * "Relancer" runs the tests again.
 *
 * The device is the one an app gets, MTLCreateSystemDefaultDevice(); when that answers nil (third-party apps are not
 * given the GPU yet) the test says so and goes on with the driver's device made by hand, as M6a did.
 * Shaders are compiled from source at run time (newLibraryWithSource:, Apple's compiler service on the phone): the Mac
 * has no Metal toolchain. When that is refused, the tests needing shaders say "pas de shaders" and the others still run.
 */
#import <Foundation/Foundation.h>
#import <Metal/Metal.h>
#import <QuartzCore/QuartzCore.h>
#import <UIKit/UIKit.h>
#import <IOSurface/IOSurfaceRef.h>
#include <IOKit/IOKitLib.h>
#include <dlfcn.h>
#include <mach/mach.h>
#include <math.h>
#include <objc/message.h>
#include <objc/runtime.h>
#include <stdarg.h>
#include <string.h>
#include <sys/sysctl.h>
#include <time.h>

#ifndef B2L_METAL_SET
#define B2L_METAL_SET "/usr/local/lib/b2l-metal-v35"
#endif

static NSMutableString *report;
static unsigned passed, failed, skipped;
static void (^shown)(void);

static double
now_ms(void)
{
	struct timespec t;
	clock_gettime(CLOCK_MONOTONIC, &t);
	return t.tv_sec * 1000.0 + t.tv_nsec / 1e6;
}

static void
say(const char *format, ...)
{
	char line[440], out[480];
	va_list ap;
	va_start(ap, format);
	vsnprintf(line, sizeof(line), format, ap);
	va_end(ap);
	snprintf(out, sizeof(out), "b2l-mtest: %s", line);
	(void)sysctlbyname("kern.hakios.say", NULL, NULL, out, strlen(out));
	NSString *s = [NSString stringWithUTF8String:line];
	dispatch_async(dispatch_get_main_queue(), ^{
		[report appendFormat:@"%@\n", s ?: @"?"];
		if (shown) shown();
	});
}

/* one result line: OK / ÉCHEC / SAUTÉ, the test's name, the time it took and what it saw */
static BOOL
result(int ok, const char *name, double ms, const char *format, ...)
{
	char what[360] = "";
	va_list ap;
	va_start(ap, format);
	vsnprintf(what, sizeof(what), format, ap);
	va_end(ap);
	if (ok > 0) passed++; else if (ok == 0) failed++; else skipped++;
	say("%s %-26s %7.1f ms  %s", ok > 0 ? "OK   " : ok == 0 ? "ÉCHEC" : "SAUTÉ", name, ms, what);
	return ok > 0;
}

/* ---------------------------------------------------------------------------------------------------------------- */

static NSString *const shaderSource = @
"#include <metal_stdlib>\n"
"using namespace metal;\n"
"kernel void add_arrays(device const float *a [[buffer(0)]], device const float *b [[buffer(1)]],\n"
"                       device float *c [[buffer(2)]], uint i [[thread_position_in_grid]]) { c[i] = a[i] + b[i]; }\n"
"kernel void sum_groups(device const uint *in [[buffer(0)]], device atomic_uint *total [[buffer(1)]],\n"
"                       threadgroup uint *tg [[threadgroup(0)]], uint i [[thread_position_in_grid]],\n"
"                       uint l [[thread_position_in_threadgroup]], uint n [[threads_per_threadgroup]]) {\n"
"    tg[l] = in[i];\n"
"    threadgroup_barrier(mem_flags::mem_threadgroup);\n"
"    for (uint s = n / 2; s > 0; s >>= 1) { if (l < s) tg[l] += tg[l + s]; threadgroup_barrier(mem_flags::mem_threadgroup); }\n"
"    if (l == 0) atomic_fetch_add_explicit(total, tg[0], memory_order_relaxed);\n"
"}\n"
"kernel void write_texture(texture2d<float, access::write> t [[texture(0)]], uint2 p [[thread_position_in_grid]]) {\n"
"    t.write(float4(p.x / 255.0, p.y / 255.0, 0.5, 1.0), p);\n"
"}\n"
"struct VIn { float2 pos [[attribute(0)]]; float4 color [[attribute(1)]]; };\n"
"struct VOut { float4 pos [[position]]; float4 color; float2 uv; };\n"
"vertex VOut vs_color(VIn v [[stage_in]], uint inst [[instance_id]], constant float2 *offsets [[buffer(2)]]) {\n"
"    VOut o; o.pos = float4(v.pos + offsets[inst], 0.5, 1.0); o.color = v.color; o.uv = v.pos * 0.5 + 0.5; return o;\n"
"}\n"
"vertex VOut vs_depth(uint vid [[vertex_id]], constant float4 *p [[buffer(0)]]) {\n"
"    VOut o; o.pos = p[vid]; o.color = float4(p[vid].z, 1.0 - p[vid].z, 0.0, 1.0); o.uv = 0; return o;\n"
"}\n"
"vertex VOut vs_quad(uint vid [[vertex_id]]) {\n"
"    float2 q[4] = { float2(-1, -1), float2(1, -1), float2(-1, 1), float2(1, 1) };\n"
"    VOut o; o.pos = float4(q[vid], 0.0, 1.0); o.color = 1; o.uv = float2(q[vid].x * 0.5 + 0.5, 0.5 - q[vid].y * 0.5); return o;\n"
"}\n"
"fragment float4 fs_color(VOut in [[stage_in]]) { return in.color; }\n"
"fragment float4 fs_texture(VOut in [[stage_in]], texture2d<float> t [[texture(0)]], sampler s [[sampler(0)]]) {\n"
"    return t.sample(s, in.uv);\n"
"}\n"
"fragment float4 fs_uniform(VOut in [[stage_in]], constant float4 &c [[buffer(0)]]) { return c; }\n"
/* B1L-0264: what RenderBox's icon shaders lean on -- derivatives (also inside a branch on a uniform, as primitive_shape
 * does) and SpringBoard's three targets R8 + RG16F + BGRA8 in one pass */
"fragment float4 fs_deriv(VOut in [[stage_in]]) {\n"
"    float w = in.pos.x * 3.0 + in.pos.y * 5.0;\n"
"    return float4(dfdx(in.pos.x) * 0.25, dfdy(in.pos.y) * 0.25, fwidth(w) * 0.03, 1.0);\n"
"}\n"
"fragment float4 fs_deriv_branch(VOut in [[stage_in]], constant float4 &c [[buffer(0)]]) {\n"
"    float w = in.pos.x * 3.0 + in.pos.y * 5.0;\n"
"    if (c.x > 0.5) return float4(fwidth(w) * 0.03, dfdy(w) * 0.04, dfdx(w) * 0.08, 1.0);\n"
"    return float4(0.0, 0.0, 0.0, 1.0);\n"
"}\n"
"struct MRT { float r [[color(0)]]; float2 rg [[color(1)]]; float4 c [[color(2)]]; };\n"
"fragment MRT fs_mrt(VOut in [[stage_in]]) {\n"
"    float2 g = in.pos.xy / 64.0; MRT o; o.r = g.x; o.rg = float2(g.y, g.x); o.c = float4(g.x, g.y, 0.5, 1.0); return o;\n"
"}\n"
/* B1L-0265: which of format, component count, several targets or reading the target back loses the R8/RG16F writes */
"fragment float fs_one(VOut in [[stage_in]]) { return in.pos.x / 64.0; }\n"
"fragment float2 fs_two(VOut in [[stage_in]]) { return float2(in.pos.y, in.pos.x) / 64.0; }\n"
"fragment float4 fs_four(VOut in [[stage_in]]) { float2 g = in.pos.xy / 64.0; return float4(g.x, g.y, 0.5, 1.0); }\n"
"struct MRT4 { float4 a [[color(0)]]; float4 b [[color(1)]]; float4 c [[color(2)]]; };\n"
"fragment MRT4 fs_mrt4(VOut in [[stage_in]]) {\n"
"    float2 g = in.pos.xy / 64.0; MRT4 o; o.a = float4(g.x, g.y, 0.5, 1.0); o.b = o.a; o.c = o.a; return o;\n"
"}\n"
"struct FB { half4 c [[color(0)]]; half2 cov [[color(1)]]; };\n"
"fragment FB fs_fetch(VOut in [[stage_in]], FB old) {\n"
"    FB o; o.c = old.c + half4(0.25h); o.cov = old.cov + half2(0.25h, 0.5h); return o;\n"
"}\n";

@interface B2LMetalSuite : NSObject
@property (nonatomic, retain) id<MTLDevice> dev;
@property (nonatomic, retain) id<MTLCommandQueue> queue;
@property (nonatomic, retain) id<MTLLibrary> lib;
@end

@implementation B2LMetalSuite

static id
hand_made_device(void)
{
	void *h = dlopen(B2L_METAL_SET "/libhakiosmetal.dylib", RTLD_NOW);
	Class cls = objc_getClass("HakiosMetalDevice");
	io_service_t svc = IOServiceGetMatchingService(0, IOServiceMatching("IOAcceleratorES"));
	if (h == NULL || cls == Nil || svc == IO_OBJECT_NULL) return nil;
	return ((id (*)(id, SEL, io_service_t))objc_msgSend)([cls alloc], sel_getUid("initWithAcceleratorPort:"), svc);
}

- (BOOL)run:(id<MTLCommandBuffer>)cb name:(const char *)name
{
	[cb commit];
	[cb waitUntilCompleted];
	if (cb.status != MTLCommandBufferStatusCompleted) {
		say("   %s: command buffer status %ld %s", name, (long)cb.status, cb.error.description.UTF8String ?: "");
		return NO;
	}
	return YES;
}

- (id<MTLFunction>)fn:(NSString *)n
{
	return self.lib ? [[self.lib newFunctionWithName:n] autorelease] : nil;
}

- (id<MTLTexture>)target:(MTLPixelFormat)f w:(NSUInteger)w h:(NSUInteger)h samples:(NSUInteger)n
{
	MTLTextureDescriptor *d = [MTLTextureDescriptor texture2DDescriptorWithPixelFormat:f width:w height:h mipmapped:NO];
	d.usage = MTLTextureUsageRenderTarget | MTLTextureUsageShaderRead;
	d.storageMode = n > 1 ? MTLStorageModePrivate : MTLStorageModeShared;
	if (n > 1) { d.textureType = MTLTextureType2DMultisample; d.sampleCount = n; }
	return [[self.dev newTextureWithDescriptor:d] autorelease];
}

/* the BGRA8 texel at (x, y) */
static uint32_t
px(id<MTLTexture> t, NSUInteger x, NSUInteger y)
{
	uint32_t v = 0;
	[t getBytes:&v bytesPerRow:4 fromRegion:MTLRegionMake2D(x, y, 1, 1) mipmapLevel:0];
	return v;
}

static BOOL
near(uint32_t a, uint32_t b, int tol)
{
	for (int s = 0; s < 32; s += 8)
		if (abs((int)((a >> s) & 255) - (int)((b >> s) & 255)) > tol) return NO;
	return YES;
}

- (id<MTLRenderPipelineState>)pipe:(NSString *)vs fs:(NSString *)fs format:(MTLPixelFormat)f
			      blend:(BOOL)blend depth:(BOOL)depth samples:(NSUInteger)n vd:(MTLVertexDescriptor *)vd
{
	MTLRenderPipelineDescriptor *p = [[MTLRenderPipelineDescriptor new] autorelease];
	p.vertexFunction = [self fn:vs];
	p.fragmentFunction = [self fn:fs];
	if (!p.vertexFunction || !p.fragmentFunction) return nil;
	p.colorAttachments[0].pixelFormat = f;
	p.vertexDescriptor = vd;
	p.rasterSampleCount = n;
	if (depth) p.depthAttachmentPixelFormat = MTLPixelFormatDepth32Float;
	if (blend) {
		p.colorAttachments[0].blendingEnabled = YES;
		p.colorAttachments[0].sourceRGBBlendFactor = MTLBlendFactorSourceAlpha;
		p.colorAttachments[0].destinationRGBBlendFactor = MTLBlendFactorOneMinusSourceAlpha;
		p.colorAttachments[0].sourceAlphaBlendFactor = MTLBlendFactorOne;
		p.colorAttachments[0].destinationAlphaBlendFactor = MTLBlendFactorOneMinusSourceAlpha;
	}
	NSError *e = nil;
	id<MTLRenderPipelineState> s = [self.dev newRenderPipelineStateWithDescriptor:p error:&e];
	if (s == nil) say("   pipeline %s/%s: %s", vs.UTF8String, fs.UTF8String, e.localizedDescription.UTF8String ?: "nil");
	return [s autorelease];
}

- (id<MTLComputePipelineState>)kernel:(NSString *)n
{
	id<MTLFunction> f = [self fn:n];
	NSError *e = nil;
	id<MTLComputePipelineState> s = f ? [self.dev newComputePipelineStateWithFunction:f error:&e] : nil;
	if (f && s == nil) say("   kernel %s: %s", n.UTF8String, e.localizedDescription.UTF8String ?: "nil");
	return [s autorelease];
}

/* --------------------------------------------------- the tests --------------------------------------------------- */

/* B1L-0264: icons on the home screen come out in horizontal stripes, and SpringBoard's R8 masks are wrong on the lower
 * row of 2x2 pixel blocks. Each check counts wrong pixels by row parity and column parity, so a quad-shaped fault
 * shows as such. */
static void
parity_say(const char *name, unsigned bad[2][2], unsigned total, const char *first)
{
	result(total == 0, name, 0, "%u faux (ligne paire/col paire %u, paire/impaire %u, impaire/paire %u, impaire/impaire %u)%s%s",
	    total, bad[0][0], bad[0][1], bad[1][0], bad[1][1], first[0] ? " ; 1er : " : "", first);
}

- (void)derivCheck:(NSString *)fs name:(const char *)name expect:(const uint8_t[3])rgb uniform:(BOOL)uniform
{
	const NSUInteger W = 64, H = 64;
	id<MTLRenderPipelineState> p = [self pipe:@"vs_quad" fs:fs format:MTLPixelFormatBGRA8Unorm blend:NO depth:NO
					samples:1 vd:nil];
	if (!p) { result(0, name, 0, "pipeline refusé"); return; }
	id<MTLTexture> t = [self target:MTLPixelFormatBGRA8Unorm w:W h:H samples:1];
	MTLRenderPassDescriptor *rp = [MTLRenderPassDescriptor renderPassDescriptor];
	rp.colorAttachments[0].texture = t;
	rp.colorAttachments[0].loadAction = MTLLoadActionClear;
	rp.colorAttachments[0].clearColor = MTLClearColorMake(1, 0, 1, 1);
	rp.colorAttachments[0].storeAction = MTLStoreActionStore;
	double t0 = now_ms();
	id<MTLCommandBuffer> cb = [self.queue commandBuffer];
	id<MTLRenderCommandEncoder> e = [cb renderCommandEncoderWithDescriptor:rp];
	[e setRenderPipelineState:p];
	float c[4] = { 1, 0, 0, 0 };
	if (uniform) [e setFragmentBytes:c length:sizeof(c) atIndex:0];
	[e drawPrimitives:MTLPrimitiveTypeTriangleStrip vertexStart:0 vertexCount:4];
	[e endEncoding];
	if (![self run:cb name:name]) { result(0, name, now_ms() - t0, "refusé"); return; }
	static uint32_t img[64 * 64];
	[t getBytes:img bytesPerRow:W * 4 fromRegion:MTLRegionMake2D(0, 0, W, H) mipmapLevel:0];
	uint32_t want = rgb[2] | (uint32_t)rgb[1] << 8 | (uint32_t)rgb[0] << 16 | 0xffu << 24;
	unsigned bad[2][2] = { { 0 } }, total = 0;
	char first[96] = "";
	for (unsigned y = 0; y < H; y++)
		for (unsigned x = 0; x < W; x++)
			if (!near(img[y * W + x], want, 3)) {
				if (!total) snprintf(first, sizeof(first), "(%u,%u) %08x au lieu de %08x", x, y, img[y * W + x], want);
				bad[y & 1][x & 1]++; total++;
			}
	parity_say(name, bad, total, first);
}

- (void)testIconShaders
{
	if (!self.lib) { result(-1, "shaders des icônes", 0, "pas de shaders"); return; }
	static const uint8_t d1[3] = { 64, 64, 61 };    /* dfdx(x)/4, dfdy(y)/4, fwidth(3x+5y)*0.03 = 0.24 */
	static const uint8_t d2[3] = { 61, 51, 61 };    /* fwidth*0.03, dfdy(w)*0.04 = 0.2, dfdx(w)*0.08 = 0.24 */
	[self derivCheck:@"fs_deriv" name:"dérivées dfdx/dfdy/fwidth" expect:d1 uniform:NO];
	[self derivCheck:@"fs_deriv_branch" name:"dérivées dans un if" expect:d2 uniform:YES];

	/* SpringBoard's pass: R8 + RG16F + BGRA8 written by one fragment shader, each a gradient of the pixel position */
	const NSUInteger W = 64, H = 64;
	MTLRenderPipelineDescriptor *pd = [[MTLRenderPipelineDescriptor new] autorelease];
	pd.vertexFunction = [self fn:@"vs_quad"];
	pd.fragmentFunction = [self fn:@"fs_mrt"];
	MTLPixelFormat fmt[3] = { MTLPixelFormatR8Unorm, MTLPixelFormatRG16Float, MTLPixelFormatBGRA8Unorm };
	for (int i = 0; i < 3; i++) pd.colorAttachments[i].pixelFormat = fmt[i];
	NSError *er = nil;
	id<MTLRenderPipelineState> p = pd.fragmentFunction ? [[self.dev newRenderPipelineStateWithDescriptor:pd error:&er] autorelease] : nil;
	if (!p) { result(0, "R8 + RG16F + BGRA8", 0, "pipeline refusé : %s", er.localizedDescription.UTF8String ?: "nil"); return; }
	id<MTLTexture> tex[3];
	MTLRenderPassDescriptor *rp = [MTLRenderPassDescriptor renderPassDescriptor];
	for (int i = 0; i < 3; i++) {
		tex[i] = [self target:fmt[i] w:W h:H samples:1];
		rp.colorAttachments[i].texture = tex[i];
		rp.colorAttachments[i].loadAction = MTLLoadActionClear;
		rp.colorAttachments[i].clearColor = MTLClearColorMake(0, 0, 0, 0);
		rp.colorAttachments[i].storeAction = MTLStoreActionStore;
	}
	double t0 = now_ms();
	id<MTLCommandBuffer> cb = [self.queue commandBuffer];
	id<MTLRenderCommandEncoder> e = [cb renderCommandEncoderWithDescriptor:rp];
	[e setRenderPipelineState:p];
	[e drawPrimitives:MTLPrimitiveTypeTriangleStrip vertexStart:0 vertexCount:4];
	[e endEncoding];
	if (![self run:cb name:"mrt"]) { result(0, "R8 + RG16F + BGRA8", now_ms() - t0, "refusé"); return; }
	static uint8_t r8[64 * 64];
	static __fp16 rg[64 * 64 * 2];
	static uint32_t c4[64 * 64];
	[tex[0] getBytes:r8 bytesPerRow:W fromRegion:MTLRegionMake2D(0, 0, W, H) mipmapLevel:0];
	[tex[1] getBytes:rg bytesPerRow:W * 4 fromRegion:MTLRegionMake2D(0, 0, W, H) mipmapLevel:0];
	[tex[2] getBytes:c4 bytesPerRow:W * 4 fromRegion:MTLRegionMake2D(0, 0, W, H) mipmapLevel:0];
	unsigned b0[2][2] = { { 0 } }, b1[2][2] = { { 0 } }, b2[2][2] = { { 0 } }, n0 = 0, n1 = 0, n2 = 0;
	char f0[96] = "", f1[96] = "", f2[96] = "";
	for (unsigned y = 0; y < H; y++)
		for (unsigned x = 0; x < W; x++) {
			float gx = (x + 0.5f) / 64.0f, gy = (y + 0.5f) / 64.0f;
			int ex = (int)lrintf(gx * 255), ey = (int)lrintf(gy * 255);
			unsigned i = y * W + x;
			if (abs(r8[i] - ex) > 1) {
				if (!n0) snprintf(f0, sizeof(f0), "(%u,%u) %u au lieu de %d", x, y, r8[i], ex);
				b0[y & 1][x & 1]++; n0++;
			}
			if (fabsf((float)rg[2 * i] - gy) > 0.002f || fabsf((float)rg[2 * i + 1] - gx) > 0.002f) {
				if (!n1) snprintf(f1, sizeof(f1), "(%u,%u) %.4f,%.4f au lieu de %.4f,%.4f", x, y,
				    (float)rg[2 * i], (float)rg[2 * i + 1], gy, gx);
				b1[y & 1][x & 1]++; n1++;
			}
			uint32_t want = 128u | (uint32_t)ey << 8 | (uint32_t)ex << 16 | 0xffu << 24;
			if (!near(c4[i], want, 2)) {
				if (!n2) snprintf(f2, sizeof(f2), "(%u,%u) %08x au lieu de %08x", x, y, c4[i], want);
				b2[y & 1][x & 1]++; n2++;
			}
		}
	say("   passe à 3 cibles : %.1f ms", now_ms() - t0);
	parity_say("cible R8 (masque)", b0, n0, f0);
	parity_say("cible RG16F", b1, n1, f1);
	parity_say("cible BGRA8", b2, n2, f2);
}

/* B1L-0265: draw the gradient into one kind of target and count wrong pixels. comps: what the shader returns (1, 2, 4).
 * The expected value of channel k is the gradient (x, y, 0.5, 1)[k] for 4 components, (x) for 1, (y, x) for 2. */
- (void)target:(MTLPixelFormat)f fs:(NSString *)fs comps:(int)comps count:(int)count name:(const char *)name
{
	const NSUInteger W = 64, H = 64;
	MTLRenderPipelineDescriptor *pd = [[MTLRenderPipelineDescriptor new] autorelease];
	pd.vertexFunction = [self fn:@"vs_quad"];
	pd.fragmentFunction = [self fn:fs];
	for (int i = 0; i < count; i++) pd.colorAttachments[i].pixelFormat = f;
	NSError *er = nil;
	id<MTLRenderPipelineState> p = pd.fragmentFunction ? [[self.dev newRenderPipelineStateWithDescriptor:pd error:&er] autorelease] : nil;
	if (!p) { result(0, name, 0, "pipeline refusé : %s", er.localizedDescription.UTF8String ?: "nil"); return; }
	MTLRenderPassDescriptor *rp = [MTLRenderPassDescriptor renderPassDescriptor];
	id<MTLTexture> tex[3];
	for (int i = 0; i < count; i++) {
		tex[i] = [self target:f w:W h:H samples:1];
		rp.colorAttachments[i].texture = tex[i];
		rp.colorAttachments[i].loadAction = MTLLoadActionClear;
		rp.colorAttachments[i].clearColor = MTLClearColorMake(0, 0, 0, 0);
		rp.colorAttachments[i].storeAction = MTLStoreActionStore;
	}
	id<MTLCommandBuffer> cb = [self.queue commandBuffer];
	id<MTLRenderCommandEncoder> e = [cb renderCommandEncoderWithDescriptor:rp];
	[e setRenderPipelineState:p];
	[e drawPrimitives:MTLPrimitiveTypeTriangleStrip vertexStart:0 vertexCount:4];
	[e endEncoding];
	if (![self run:cb name:name]) { result(0, name, 0, "refusé"); return; }
	unsigned bpp = f == MTLPixelFormatR8Unorm ? 1 : 4;
	static uint8_t img[64 * 64 * 4];
	char what[300] = "";
	unsigned allbad = 0;
	for (int i = 0; i < count; i++) {
		[tex[i] getBytes:img bytesPerRow:W * bpp fromRegion:MTLRegionMake2D(0, 0, W, H) mipmapLevel:0];
		unsigned bad = 0;
		float got0[2] = { 0, 0 };
		for (unsigned y = 0; y < H; y++)
			for (unsigned x = 0; x < W; x++) {
				float gx = (x + 0.5f) / 64.0f, gy = (y + 0.5f) / 64.0f, v[2];
				const uint8_t *q = img + (y * W + x) * bpp;
				float want[2] = { comps == 2 ? gy : gx, comps == 2 ? gx : gy };
				if (f == MTLPixelFormatR8Unorm) { v[0] = q[0] / 255.0f; v[1] = want[1]; }
				else if (f == MTLPixelFormatRG16Float) { v[0] = ((const __fp16 *)q)[0]; v[1] = ((const __fp16 *)q)[1]; }
				else { v[0] = q[2] / 255.0f; v[1] = q[1] / 255.0f; }   /* BGRA8: R, G */
				if (f == MTLPixelFormatRG16Float && comps == 1) v[1] = want[1];
				if (x == 0 && y == 0) { got0[0] = v[0]; got0[1] = v[1]; }
				if (fabsf(v[0] - want[0]) > 0.006f || fabsf(v[1] - want[1]) > 0.006f) bad++;
			}
		allbad += bad;
		char one[90];
		snprintf(one, sizeof(one), "%sc%d %u faux (0,0)=%.3f,%.3f", i ? " ; " : "", i, bad, got0[0], got0[1]);
		strlcat(what, one, sizeof(what));
	}
	result(allbad == 0, name, 0, "%s", what);
}

- (void)testTargets
{
	if (!self.lib) { result(-1, "cibles", 0, "pas de shaders"); return; }
	[self target:MTLPixelFormatR8Unorm fs:@"fs_one" comps:1 count:1 name:"R8 seule, float"];
	[self target:MTLPixelFormatR8Unorm fs:@"fs_four" comps:4 count:1 name:"R8 seule, float4"];
	[self target:MTLPixelFormatRG16Float fs:@"fs_two" comps:2 count:1 name:"RG16F seule, float2"];
	[self target:MTLPixelFormatRG16Float fs:@"fs_four" comps:4 count:1 name:"RG16F seule, float4"];
	[self target:MTLPixelFormatBGRA8Unorm fs:@"fs_mrt4" comps:4 count:3 name:"3 cibles BGRA8"];

	/* framebuffer fetch, as RenderBox accumulates its coverage: RGBA16F + RG16F read back and added to, twice */
	const NSUInteger W = 64, H = 64;
	MTLRenderPipelineDescriptor *pd = [[MTLRenderPipelineDescriptor new] autorelease];
	pd.vertexFunction = [self fn:@"vs_quad"];
	pd.fragmentFunction = [self fn:@"fs_fetch"];
	pd.colorAttachments[0].pixelFormat = MTLPixelFormatRGBA16Float;
	pd.colorAttachments[1].pixelFormat = MTLPixelFormatRG16Float;
	NSError *er = nil;
	id<MTLRenderPipelineState> p = pd.fragmentFunction ? [[self.dev newRenderPipelineStateWithDescriptor:pd error:&er] autorelease] : nil;
	if (!p) { result(0, "relecture de la cible", 0, "pipeline refusé : %s", er.localizedDescription.UTF8String ?: "nil"); return; }
	id<MTLTexture> a = [self target:MTLPixelFormatRGBA16Float w:W h:H samples:1];
	id<MTLTexture> b = [self target:MTLPixelFormatRG16Float w:W h:H samples:1];
	MTLRenderPassDescriptor *rp = [MTLRenderPassDescriptor renderPassDescriptor];
	rp.colorAttachments[0].texture = a;
	rp.colorAttachments[1].texture = b;
	for (int i = 0; i < 2; i++) {
		rp.colorAttachments[i].loadAction = MTLLoadActionClear;
		rp.colorAttachments[i].clearColor = MTLClearColorMake(0.125, 0.125, 0.125, 0.125);
		rp.colorAttachments[i].storeAction = MTLStoreActionStore;
	}
	id<MTLCommandBuffer> cb = [self.queue commandBuffer];
	id<MTLRenderCommandEncoder> e = [cb renderCommandEncoderWithDescriptor:rp];
	[e setRenderPipelineState:p];
	[e drawPrimitives:MTLPrimitiveTypeTriangleStrip vertexStart:0 vertexCount:4];
	[e drawPrimitives:MTLPrimitiveTypeTriangleStrip vertexStart:0 vertexCount:4];
	[e endEncoding];
	if (![self run:cb name:"fetch"]) { result(0, "relecture de la cible", 0, "refusé"); return; }
	__fp16 va[4] = { 0 }, vb[2] = { 0 };
	[a getBytes:va bytesPerRow:8 fromRegion:MTLRegionMake2D(10, 20, 1, 1) mipmapLevel:0];
	[b getBytes:vb bytesPerRow:4 fromRegion:MTLRegionMake2D(10, 20, 1, 1) mipmapLevel:0];
	BOOL ok = fabsf((float)va[0] - 0.625f) < 0.01f && fabsf((float)vb[0] - 0.625f) < 0.01f && fabsf((float)vb[1] - 1.125f) < 0.01f;
	result(ok, "relecture de la cible", 0, "RGBA16F %.3f (attendu 0,625) ; RG16F %.3f,%.3f (attendu 0,625,1,125)",
	    (float)va[0], (float)vb[0], (float)vb[1]);
}

- (BOOL)testDevice
{
	double t = now_ms();
	id<MTLDevice> sys = MTLCreateSystemDefaultDevice();
	result(sys != nil, "MTLCreateSystemDefaultDevice", now_ms() - t, "%s",
	    sys ? sys.name.UTF8String : "nil : une app ordinaire n'a pas le GPU");
	NSArray *all = MTLCopyAllDevices();
	result(all.count > 0, "MTLCopyAllDevices", 0, "%lu appareil(s)", (unsigned long)all.count);
	self.dev = sys ? [sys autorelease] : [hand_made_device() autorelease];
	if (sys == nil)
		say("   (la suite continue avec l'appareil du pilote fait à la main : %s)",
		    self.dev ? self.dev.name.UTF8String : "absent aussi");
	if (self.dev == nil) return NO;
	id<MTLDevice> d = self.dev;
	MTLSize m = d.maxThreadsPerThreadgroup;
	result(1, "propriétés", 0, "%s ; threadgroup max %lux%lux%lu, %lu o partagés ; tampon max %llu Mo ; mémoire unifiée %d",
	    d.name.UTF8String, (unsigned long)m.width, (unsigned long)m.height, (unsigned long)m.depth,
	    (unsigned long)d.maxThreadgroupMemoryLength, (unsigned long long)d.maxBufferLength >> 20, d.hasUnifiedMemory);
	char fam[160] = "";
	MTLGPUFamily f[] = { MTLGPUFamilyApple1, MTLGPUFamilyApple2, MTLGPUFamilyApple3, MTLGPUFamilyApple4,
		MTLGPUFamilyApple5, MTLGPUFamilyApple6, MTLGPUFamilyApple7, MTLGPUFamilyApple8, MTLGPUFamilyApple9,
		MTLGPUFamilyCommon1, MTLGPUFamilyCommon2, MTLGPUFamilyCommon3, MTLGPUFamilyMetal3 };
	const char *fn[] = { "A1", "A2", "A3", "A4", "A5", "A6", "A7", "A8", "A9", "C1", "C2", "C3", "M3" };
	for (unsigned i = 0; i < sizeof(f) / sizeof(f[0]); i++)
		if ([d supportsFamily:f[i]]) { strlcat(fam, fn[i], sizeof(fam)); strlcat(fam, " ", sizeof(fam)); }
	result(fam[0] != 0, "supportsFamily", 0, "%s", fam[0] ? fam : "aucune famille : beaucoup d'apps refuseront de démarrer");
	result([d supportsTextureSampleCount:4], "MSAA 4x annoncé", 0, "%s", [d supportsTextureSampleCount:4] ? "oui" : "non");
	t = now_ms();
	self.queue = [[d newCommandQueue] autorelease];
	return result(self.queue != nil, "newCommandQueue", now_ms() - t, "%s", self.queue ? "" : "nil");
}

- (void)testShaders
{
	NSError *e = nil;
	double t = now_ms();
	self.lib = [[self.dev newLibraryWithSource:shaderSource options:nil error:&e] autorelease];
	result(self.lib != nil, "newLibraryWithSource", now_ms() - t, "%s",
	    self.lib ? [[self.lib.functionNames componentsJoinedByString:@" "] UTF8String] : e.localizedDescription.UTF8String ?: "nil");
	if (self.lib == nil) {
		/* async form, which SwiftUI/SpriteKit-style code uses */
		__block id<MTLLibrary> got = nil;
		dispatch_semaphore_t s = dispatch_semaphore_create(0);
		t = now_ms();
		[self.dev newLibraryWithSource:shaderSource options:nil completionHandler:^(id<MTLLibrary> l, NSError *er) {
			got = [l retain];
			if (er) say("   asynchrone : %s", er.localizedDescription.UTF8String);
			dispatch_semaphore_signal(s);
		}];
		long late = dispatch_semaphore_wait(s, dispatch_time(DISPATCH_TIME_NOW, 20 * NSEC_PER_SEC));
		result(got != nil, "newLibraryWithSource async", now_ms() - t, "%s", late ? "pas de réponse en 20 s" : got ? "" : "nil");
		self.lib = [got autorelease];
	}
	id<MTLLibrary> def = [self.dev newDefaultLibrary];
	result(-1, "newDefaultLibrary", 0, "%s (normal : cette app n'embarque pas de .metallib)", def ? "trouvée" : "nil");
	[def release];
}

- (void)testBuffers
{
	id<MTLDevice> d = self.dev;
	double t = now_ms();
	float in[1024];
	for (int i = 0; i < 1024; i++) in[i] = i * 0.5f;
	id<MTLBuffer> a = [[d newBufferWithBytes:in length:sizeof(in) options:MTLResourceStorageModeShared] autorelease];
	result(a && memcmp(a.contents, in, sizeof(in)) == 0, "newBufferWithBytes", now_ms() - t, "%lu o", (unsigned long)a.length);
	t = now_ms();
	id<MTLBuffer> big = [[d newBufferWithLength:64 << 20 options:MTLResourceStorageModeShared] autorelease];
	if (big) memset(big.contents, 0x5a, big.length);
	result(big != nil, "tampon de 64 Mo", now_ms() - t, "%s", big ? "alloué et écrit" : "nil");

	/* blit: fill, copy into a private buffer and back */
	id<MTLBuffer> priv = [[d newBufferWithLength:sizeof(in) options:MTLResourceStorageModePrivate] autorelease];
	id<MTLBuffer> back = [[d newBufferWithLength:sizeof(in) options:MTLResourceStorageModeShared] autorelease];
	id<MTLBuffer> fill = [[d newBufferWithLength:4096 options:MTLResourceStorageModeShared] autorelease];
	t = now_ms();
	id<MTLCommandBuffer> cb = [self.queue commandBuffer];
	id<MTLBlitCommandEncoder> b = [cb blitCommandEncoder];
	[b copyFromBuffer:a sourceOffset:0 toBuffer:priv destinationOffset:0 size:sizeof(in)];
	[b copyFromBuffer:priv sourceOffset:0 toBuffer:back destinationOffset:0 size:sizeof(in)];
	[b fillBuffer:fill range:NSMakeRange(0, 4096) value:0xab];
	[b endEncoding];
	BOOL ran = [self run:cb name:"blit"];
	result(ran && memcmp(back.contents, in, sizeof(in)) == 0, "blit tampon privé aller-retour", now_ms() - t, "%s",
	    ran ? (memcmp(back.contents, in, sizeof(in)) ? "contenu différent" : "") : "refusé");
	result(ran && ((uint8_t *)fill.contents)[0] == 0xab && ((uint8_t *)fill.contents)[4095] == 0xab, "fillBuffer", 0,
	    "octets %#x … %#x", ((uint8_t *)fill.contents)[0], ((uint8_t *)fill.contents)[4095]);

	/* the memory of the app wrapped without a copy, written by the GPU: what RenderBox does for the icons */
	vm_address_t page = 0;
	vm_allocate(mach_task_self(), &page, 16384, VM_FLAGS_ANYWHERE);
	memset((void *)page, 0, 16384);
	id<MTLBuffer> nc = [[d newBufferWithBytesNoCopy:(void *)page length:16384 options:MTLResourceStorageModeShared
					    deallocator:nil] autorelease];
	t = now_ms();
	cb = [self.queue commandBuffer];
	b = [cb blitCommandEncoder];
	[b copyFromBuffer:a sourceOffset:0 toBuffer:nc destinationOffset:0 size:sizeof(in)];
	[b endEncoding];
	ran = nc && [self run:cb name:"nocopy"];
	BOOL same = ran && memcmp((void *)page, in, sizeof(in)) == 0;
	result(same, "newBufferWithBytesNoCopy", now_ms() - t, "%s", !nc ? "nil" : !ran ? "refusé" :
	    same ? "le GPU écrit dans la mémoire de l'app" : "la mémoire de l'app n'a pas reçu ce que le GPU a écrit");
}

- (void)testCompute
{
	id<MTLComputePipelineState> add = [self kernel:@"add_arrays"], sum = [self kernel:@"sum_groups"];
	if (!add) { result(-1, "calcul (compute)", 0, "pas de shaders"); return; }
	const unsigned n = 1 << 20;
	id<MTLBuffer> a = [[self.dev newBufferWithLength:n * 4 options:0] autorelease];
	id<MTLBuffer> b = [[self.dev newBufferWithLength:n * 4 options:0] autorelease];
	id<MTLBuffer> c = [[self.dev newBufferWithLength:n * 4 options:0] autorelease];
	float *pa = a.contents, *pb = b.contents, *pc = c.contents;
	for (unsigned i = 0; i < n; i++) { pa[i] = i; pb[i] = 2.0f * i; pc[i] = -1; }
	double t = now_ms();
	id<MTLCommandBuffer> cb = [self.queue commandBuffer];
	id<MTLComputeCommandEncoder> e = [cb computeCommandEncoder];
	[e setComputePipelineState:add];
	[e setBuffer:a offset:0 atIndex:0];
	[e setBuffer:b offset:0 atIndex:1];
	[e setBuffer:c offset:0 atIndex:2];
	[e dispatchThreadgroups:MTLSizeMake(n / 64, 1, 1) threadsPerThreadgroup:MTLSizeMake(64, 1, 1)];
	[e endEncoding];
	BOOL ran = [self run:cb name:"add"];
	unsigned bad = 0;
	for (unsigned i = 0; ran && i < n; i++) if (pc[i] != 3.0f * i) bad++;
	result(ran && bad == 0, "compute : 1 M additions", now_ms() - t, "%s%u faux", ran ? "" : "refusé ; ", bad);
	if (cb.GPUEndTime > cb.GPUStartTime)
		say("   temps GPU %.2f ms", (cb.GPUEndTime - cb.GPUStartTime) * 1000);
	if (!sum) { result(0, "threadgroup + atomique", 0, "pipeline refusé"); return; }
	id<MTLBuffer> total = [[self.dev newBufferWithLength:4 options:0] autorelease];
	*(uint32_t *)total.contents = 0;
	uint32_t *pu = a.contents;
	for (unsigned i = 0; i < 65536; i++) pu[i] = 1;
	t = now_ms();
	cb = [self.queue commandBuffer];
	e = [cb computeCommandEncoder];
	[e setComputePipelineState:sum];
	[e setBuffer:a offset:0 atIndex:0];
	[e setBuffer:total offset:0 atIndex:1];
	[e setThreadgroupMemoryLength:64 * 4 atIndex:0];
	[e dispatchThreadgroups:MTLSizeMake(65536 / 64, 1, 1) threadsPerThreadgroup:MTLSizeMake(64, 1, 1)];
	[e endEncoding];
	ran = [self run:cb name:"sum"];
	result(ran && *(uint32_t *)total.contents == 65536, "threadgroup + atomique", now_ms() - t, "somme %u (attendu 65536)",
	    *(uint32_t *)total.contents);
}

- (void)testTextures
{
	struct { MTLPixelFormat f; unsigned bpp; const char *n; } fm[] = {
		{ MTLPixelFormatRGBA8Unorm, 4, "RGBA8" }, { MTLPixelFormatBGRA8Unorm, 4, "BGRA8" }, { MTLPixelFormatR8Unorm, 1, "R8" },
		{ MTLPixelFormatRG8Unorm, 2, "RG8" }, { MTLPixelFormatRGBA16Float, 8, "RGBA16F" }, { MTLPixelFormatR32Float, 4, "R32F" },
		{ MTLPixelFormatRGB10A2Unorm, 4, "RGB10A2" }, { MTLPixelFormatRGBA8Unorm_sRGB, 4, "RGBA8 sRGB" },
	};
	char bad[200] = "";
	double t = now_ms();
	for (unsigned k = 0; k < sizeof(fm) / sizeof(fm[0]); k++) {
		MTLTextureDescriptor *d = [MTLTextureDescriptor texture2DDescriptorWithPixelFormat:fm[k].f width:37 height:19 mipmapped:NO];
		d.storageMode = MTLStorageModeShared;
		id<MTLTexture> tx = [[self.dev newTextureWithDescriptor:d] autorelease];
		uint8_t src[37 * 19 * 8], dst[37 * 19 * 8];
		for (unsigned i = 0; i < sizeof(src); i++) src[i] = (uint8_t)(i * 7 + k);
		memset(dst, 0, sizeof(dst));
		[tx replaceRegion:MTLRegionMake2D(0, 0, 37, 19) mipmapLevel:0 withBytes:src bytesPerRow:37 * fm[k].bpp];
		[tx getBytes:dst bytesPerRow:37 * fm[k].bpp fromRegion:MTLRegionMake2D(0, 0, 37, 19) mipmapLevel:0];
		if (!tx || memcmp(src, dst, 37 * 19 * fm[k].bpp)) { strlcat(bad, fm[k].n, sizeof(bad)); strlcat(bad, " ", sizeof(bad)); }
	}
	result(bad[0] == 0, "textures écrire/relire 8 formats", now_ms() - t, "%s%s", bad[0] ? "faux : " : "", bad);

	/* mipmaps generated by a blit, a texture filled from a buffer */
	MTLTextureDescriptor *d = [MTLTextureDescriptor texture2DDescriptorWithPixelFormat:MTLPixelFormatRGBA8Unorm width:64 height:64 mipmapped:YES];
	d.storageMode = MTLStorageModeShared;
	id<MTLTexture> mip = [[self.dev newTextureWithDescriptor:d] autorelease];
	uint32_t white[64 * 64];
	for (unsigned i = 0; i < 64 * 64; i++) white[i] = (i & 1) ? 0xffffffff : 0xff000000;
	[mip replaceRegion:MTLRegionMake2D(0, 0, 64, 64) mipmapLevel:0 withBytes:white bytesPerRow:256];
	id<MTLBuffer> src = [[self.dev newBufferWithBytes:white length:sizeof(white) options:0] autorelease];
	id<MTLTexture> fromBuf = [[self.dev newTextureWithDescriptor:
	    [MTLTextureDescriptor texture2DDescriptorWithPixelFormat:MTLPixelFormatRGBA8Unorm width:64 height:64 mipmapped:NO]] autorelease];
	t = now_ms();
	id<MTLCommandBuffer> cb = [self.queue commandBuffer];
	id<MTLBlitCommandEncoder> b = [cb blitCommandEncoder];
	[b generateMipmapsForTexture:mip];
	[b copyFromBuffer:src sourceOffset:0 sourceBytesPerRow:256 sourceBytesPerImage:sizeof(white) sourceSize:MTLSizeMake(64, 64, 1)
		toTexture:fromBuf destinationSlice:0 destinationLevel:0 destinationOrigin:MTLOriginMake(0, 0, 0)];
	[b endEncoding];
	BOOL ran = [self run:cb name:"mip"];
	uint32_t m5 = 0, fb = 0;
	[mip getBytes:&m5 bytesPerRow:4 fromRegion:MTLRegionMake2D(0, 0, 1, 1) mipmapLevel:5];
	[fromBuf getBytes:&fb bytesPerRow:4 fromRegion:MTLRegionMake2D(1, 0, 1, 1) mipmapLevel:0];
	result(ran && near(m5, 0xff7f7f7f, 3), "generateMipmaps", now_ms() - t, "niveau 5 = %#x (attendu ~0xff7f7f7f)", m5);
	result(ran && fb == 0xffffffff, "blit tampon → texture", 0, "texel %#x", fb);
}

- (void)testRender
{
	id<MTLDevice> d = self.dev;
	MTLVertexDescriptor *vd = [MTLVertexDescriptor vertexDescriptor];
	vd.attributes[0].format = MTLVertexFormatFloat2; vd.attributes[0].bufferIndex = 0; vd.attributes[0].offset = 0;
	vd.attributes[1].format = MTLVertexFormatFloat4; vd.attributes[1].bufferIndex = 0; vd.attributes[1].offset = 8;
	vd.layouts[0].stride = 24;
	float tri[] = { -0.5f, -0.5f, 1, 0, 0, 1,   0.5f, -0.5f, 1, 0, 0, 1,   0.0f, 0.5f, 1, 0, 0, 1 };
	float noOffset[2] = { 0, 0 };
	id<MTLBuffer> vb = [[d newBufferWithBytes:tri length:sizeof(tri) options:0] autorelease];
	id<MTLBuffer> off = [[d newBufferWithBytes:noOffset length:8 options:0] autorelease];

	/* a clear alone needs no shader at all */
	id<MTLTexture> rt = [self target:MTLPixelFormatBGRA8Unorm w:64 h:64 samples:1];
	MTLRenderPassDescriptor *rp = [MTLRenderPassDescriptor renderPassDescriptor];
	rp.colorAttachments[0].texture = rt;
	rp.colorAttachments[0].loadAction = MTLLoadActionClear;
	rp.colorAttachments[0].storeAction = MTLStoreActionStore;
	rp.colorAttachments[0].clearColor = MTLClearColorMake(0, 0, 1, 1);
	double t = now_ms();
	id<MTLCommandBuffer> cb = [self.queue commandBuffer];
	[[cb renderCommandEncoderWithDescriptor:rp] endEncoding];
	BOOL ran = [self run:cb name:"clear"];
	result(ran && px(rt, 5, 5) == 0xff0000ff, "passe de rendu : effacement", now_ms() - t, "pixel %#x (attendu 0xff0000ff)", px(rt, 5, 5));

	id<MTLRenderPipelineState> color = [self pipe:@"vs_color" fs:@"fs_color" format:MTLPixelFormatBGRA8Unorm blend:NO depth:NO samples:1 vd:vd];
	if (!color) { result(-1, "dessin", 0, "pas de shaders : les autres tests de dessin sont sautés"); return; }
	t = now_ms();
	cb = [self.queue commandBuffer];
	id<MTLRenderCommandEncoder> e = [cb renderCommandEncoderWithDescriptor:rp];
	[e setRenderPipelineState:color];
	[e setVertexBuffer:vb offset:0 atIndex:0];
	[e setVertexBuffer:off offset:0 atIndex:2];
	[e drawPrimitives:MTLPrimitiveTypeTriangle vertexStart:0 vertexCount:3];
	[e endEncoding];
	ran = [self run:cb name:"triangle"];
	result(ran && px(rt, 32, 34) == 0xffff0000 && px(rt, 2, 2) == 0xff0000ff, "triangle (vertex descriptor)", now_ms() - t,
	    "centre %#x, coin %#x", px(rt, 32, 34), px(rt, 2, 2));

	/* instancing + indexed draw */
	float offs[4] = { -0.5f, 0, 0.5f, 0 };
	id<MTLBuffer> ob = [[d newBufferWithBytes:offs length:16 options:0] autorelease];
	uint16_t idx[3] = { 0, 1, 2 };
	id<MTLBuffer> ib = [[d newBufferWithBytes:idx length:6 options:0] autorelease];
	t = now_ms();
	cb = [self.queue commandBuffer];
	e = [cb renderCommandEncoderWithDescriptor:rp];
	[e setRenderPipelineState:color];
	[e setVertexBuffer:vb offset:0 atIndex:0];
	[e setVertexBuffer:ob offset:0 atIndex:2];
	[e drawIndexedPrimitives:MTLPrimitiveTypeTriangle indexCount:3 indexType:MTLIndexTypeUInt16 indexBuffer:ib
		 indexBufferOffset:0 instanceCount:2];
	[e endEncoding];
	ran = [self run:cb name:"instances"];
	result(ran && px(rt, 16, 34) == 0xffff0000 && px(rt, 48, 34) == 0xffff0000 && px(rt, 32, 34) == 0xff0000ff,
	    "indexé + 2 instances", now_ms() - t, "gauche %#x droite %#x milieu %#x", px(rt, 16, 34), px(rt, 48, 34), px(rt, 32, 34));

	/* alpha blending */
	float half[] = { -1, -1, 1, 0, 0, 0.5f,   3, -1, 1, 0, 0, 0.5f,   -1, 3, 1, 0, 0, 0.5f };
	id<MTLBuffer> hb = [[d newBufferWithBytes:half length:sizeof(half) options:0] autorelease];
	id<MTLRenderPipelineState> blend = [self pipe:@"vs_color" fs:@"fs_color" format:MTLPixelFormatBGRA8Unorm blend:YES depth:NO samples:1 vd:vd];
	t = now_ms();
	cb = [self.queue commandBuffer];
	e = [cb renderCommandEncoderWithDescriptor:rp];
	[e setRenderPipelineState:blend];
	[e setVertexBuffer:hb offset:0 atIndex:0];
	[e setVertexBuffer:off offset:0 atIndex:2];
	[e drawPrimitives:MTLPrimitiveTypeTriangle vertexStart:0 vertexCount:3];
	[e endEncoding];
	ran = blend && [self run:cb name:"blend"];
	result(ran && near(px(rt, 10, 10), 0xff7f0080, 3), "mélange alpha", now_ms() - t, "pixel %#x (attendu ~0xff7f0080)", px(rt, 10, 10));

	/* scissor + viewport */
	t = now_ms();
	cb = [self.queue commandBuffer];
	e = [cb renderCommandEncoderWithDescriptor:rp];
	[e setRenderPipelineState:color];
	[e setVertexBuffer:vb offset:0 atIndex:0];
	[e setVertexBuffer:off offset:0 atIndex:2];
	[e setViewport:(MTLViewport){ 0, 0, 32, 32, 0, 1 }];
	[e setScissorRect:(MTLScissorRect){ 0, 0, 16, 64 }];
	[e drawPrimitives:MTLPrimitiveTypeTriangle vertexStart:0 vertexCount:3];
	[e endEncoding];
	ran = [self run:cb name:"viewport"];
	result(ran && px(rt, 14, 17) == 0xffff0000 && px(rt, 18, 17) == 0xff0000ff && px(rt, 40, 40) == 0xff0000ff,
	    "viewport + scissor", now_ms() - t, "dedans %#x, coupé %#x, hors viewport %#x", px(rt, 14, 17), px(rt, 18, 17), px(rt, 40, 40));

	/* depth: the near triangle (z 0.2, green-ish) is drawn second but behind is drawn after: depth must keep the near one */
	float quads[] = { -1, -1, 0.2f, 1,  3, -1, 0.2f, 1,  -1, 3, 0.2f, 1,     -1, -1, 0.8f, 1,  3, -1, 0.8f, 1,  -1, 3, 0.8f, 1 };
	id<MTLBuffer> qb = [[d newBufferWithBytes:quads length:sizeof(quads) options:0] autorelease];
	id<MTLRenderPipelineState> dp = [self pipe:@"vs_depth" fs:@"fs_color" format:MTLPixelFormatBGRA8Unorm blend:NO depth:YES samples:1 vd:nil];
	MTLTextureDescriptor *dd = [MTLTextureDescriptor texture2DDescriptorWithPixelFormat:MTLPixelFormatDepth32Float width:64 height:64 mipmapped:NO];
	dd.usage = MTLTextureUsageRenderTarget;
	dd.storageMode = MTLStorageModePrivate;
	MTLDepthStencilDescriptor *dsd = [[MTLDepthStencilDescriptor new] autorelease];
	dsd.depthCompareFunction = MTLCompareFunctionLess;
	dsd.depthWriteEnabled = YES;
	id<MTLDepthStencilState> ds = [[d newDepthStencilStateWithDescriptor:dsd] autorelease];
	MTLRenderPassDescriptor *rpd = [[rp copy] autorelease];
	rpd.depthAttachment.texture = [[d newTextureWithDescriptor:dd] autorelease];
	rpd.depthAttachment.loadAction = MTLLoadActionClear;
	rpd.depthAttachment.clearDepth = 1.0;
	rpd.depthAttachment.storeAction = MTLStoreActionDontCare;
	t = now_ms();
	cb = [self.queue commandBuffer];
	e = [cb renderCommandEncoderWithDescriptor:rpd];
	[e setRenderPipelineState:dp];
	[e setDepthStencilState:ds];
	[e setVertexBuffer:qb offset:0 atIndex:0];
	[e drawPrimitives:MTLPrimitiveTypeTriangle vertexStart:0 vertexCount:6];
	[e endEncoding];
	ran = dp && [self run:cb name:"depth"];
	result(ran && near(px(rt, 10, 10), 0xff33cc00, 3), "test de profondeur", now_ms() - t, "pixel %#x (attendu ~0xff33cc00)", px(rt, 10, 10));

	/* a texture sampled: nearest and linear */
	id<MTLRenderPipelineState> tp = [self pipe:@"vs_quad" fs:@"fs_texture" format:MTLPixelFormatBGRA8Unorm blend:NO depth:NO samples:1 vd:nil];
	id<MTLTexture> chk = [[d newTextureWithDescriptor:
	    [MTLTextureDescriptor texture2DDescriptorWithPixelFormat:MTLPixelFormatBGRA8Unorm width:2 height:2 mipmapped:NO]] autorelease];
	uint32_t c4[4] = { 0xffff0000, 0xff00ff00, 0xff0000ff, 0xffffffff };
	[chk replaceRegion:MTLRegionMake2D(0, 0, 2, 2) mipmapLevel:0 withBytes:c4 bytesPerRow:8];
	for (int lin = 0; lin < 2; lin++) {
		MTLSamplerDescriptor *sd = [[MTLSamplerDescriptor new] autorelease];
		sd.minFilter = sd.magFilter = lin ? MTLSamplerMinMagFilterLinear : MTLSamplerMinMagFilterNearest;
		id<MTLSamplerState> ss = [[d newSamplerStateWithDescriptor:sd] autorelease];
		t = now_ms();
		cb = [self.queue commandBuffer];
		e = [cb renderCommandEncoderWithDescriptor:rp];
		[e setRenderPipelineState:tp];
		[e setFragmentTexture:chk atIndex:0];
		[e setFragmentSamplerState:ss atIndex:0];
		[e drawPrimitives:MTLPrimitiveTypeTriangleStrip vertexStart:0 vertexCount:4];
		[e endEncoding];
		ran = tp && [self run:cb name:"sample"];
		if (!lin)
			result(ran && px(rt, 5, 5) == 0xffff0000 && px(rt, 58, 5) == 0xff00ff00 && px(rt, 5, 58) == 0xff0000ff,
			    "texture échantillonnée (nearest)", now_ms() - t, "coins %#x %#x %#x", px(rt, 5, 5), px(rt, 58, 5), px(rt, 5, 58));
		else
			result(ran && near(px(rt, 32, 32), 0xff7f7f7f, 24), "texture échantillonnée (linear)", now_ms() - t,
			    "centre %#x (mélange attendu ~0xff7f7f7f)", px(rt, 32, 32));
	}

	/* MSAA 4x resolved */
	id<MTLRenderPipelineState> ms = [self pipe:@"vs_color" fs:@"fs_color" format:MTLPixelFormatBGRA8Unorm blend:NO depth:NO samples:4 vd:vd];
	MTLRenderPassDescriptor *mr = [MTLRenderPassDescriptor renderPassDescriptor];
	mr.colorAttachments[0].texture = [self target:MTLPixelFormatBGRA8Unorm w:64 h:64 samples:4];
	mr.colorAttachments[0].resolveTexture = rt;
	mr.colorAttachments[0].loadAction = MTLLoadActionClear;
	mr.colorAttachments[0].storeAction = MTLStoreActionMultisampleResolve;
	mr.colorAttachments[0].clearColor = MTLClearColorMake(0, 0, 1, 1);
	t = now_ms();
	cb = [self.queue commandBuffer];
	e = ms && mr.colorAttachments[0].texture ? [cb renderCommandEncoderWithDescriptor:mr] : nil;
	[e setRenderPipelineState:ms];
	[e setVertexBuffer:vb offset:0 atIndex:0];
	[e setVertexBuffer:off offset:0 atIndex:2];
	[e drawPrimitives:MTLPrimitiveTypeTriangle vertexStart:0 vertexCount:3];
	[e endEncoding];
	ran = e && [self run:cb name:"msaa"];
	result(ran && px(rt, 32, 34) == 0xffff0000, "MSAA 4x + résolution", now_ms() - t, "%s centre %#x",
	    e ? "" : "pas de cible 4x ;", px(rt, 32, 34));

	/* a uniform from setFragmentBytes, into an RGBA16F target */
	id<MTLRenderPipelineState> up = [self pipe:@"vs_quad" fs:@"fs_uniform" format:MTLPixelFormatRGBA16Float blend:NO depth:NO samples:1 vd:nil];
	id<MTLTexture> hf = [self target:MTLPixelFormatRGBA16Float w:16 h:16 samples:1];
	MTLRenderPassDescriptor *hr = [MTLRenderPassDescriptor renderPassDescriptor];
	hr.colorAttachments[0].texture = hf;
	hr.colorAttachments[0].loadAction = MTLLoadActionDontCare;
	hr.colorAttachments[0].storeAction = MTLStoreActionStore;
	float col[4] = { 0.25f, 0.5f, 2.0f, 1.0f };
	t = now_ms();
	cb = [self.queue commandBuffer];
	e = [cb renderCommandEncoderWithDescriptor:hr];
	[e setRenderPipelineState:up];
	[e setFragmentBytes:col length:16 atIndex:0];
	[e drawPrimitives:MTLPrimitiveTypeTriangleStrip vertexStart:0 vertexCount:4];
	[e endEncoding];
	ran = up && [self run:cb name:"half"];
	uint16_t h[4] = { 0 };
	[hf getBytes:h bytesPerRow:16 * 8 fromRegion:MTLRegionMake2D(8, 8, 1, 1) mipmapLevel:0];
	result(ran && h[0] == 0x3400 && h[1] == 0x3800 && h[2] == 0x4000, "setFragmentBytes → RGBA16F", now_ms() - t,
	    "%#x %#x %#x %#x (attendu 0x3400 0x3800 0x4000 0x3c00)", h[0], h[1], h[2], h[3]);

	/* compute writing a texture */
	id<MTLComputePipelineState> wt = [self kernel:@"write_texture"];
	MTLTextureDescriptor *wd = [MTLTextureDescriptor texture2DDescriptorWithPixelFormat:MTLPixelFormatRGBA8Unorm width:64 height:64 mipmapped:NO];
	wd.usage = MTLTextureUsageShaderWrite | MTLTextureUsageShaderRead;
	wd.storageMode = MTLStorageModeShared;
	id<MTLTexture> wtx = [[d newTextureWithDescriptor:wd] autorelease];
	t = now_ms();
	cb = [self.queue commandBuffer];
	id<MTLComputeCommandEncoder> ce = [cb computeCommandEncoder];
	[ce setComputePipelineState:wt];
	[ce setTexture:wtx atIndex:0];
	[ce dispatchThreadgroups:MTLSizeMake(8, 8, 1) threadsPerThreadgroup:MTLSizeMake(8, 8, 1)];
	[ce endEncoding];
	ran = wt && [self run:cb name:"write"];
	uint32_t w = 0;
	[wtx getBytes:&w bytesPerRow:4 fromRegion:MTLRegionMake2D(51, 17, 1, 1) mipmapLevel:0];
	result(ran && near(w, 0xff801133, 2), "compute écrit une texture", now_ms() - t, "texel %#x (attendu ~0xff801133)", w);

	/* a render target over an IOSurface, read back by the CPU through the surface */
	NSDictionary *props = @{ (id)kIOSurfaceWidth: @64, (id)kIOSurfaceHeight: @64, (id)kIOSurfaceBytesPerElement: @4,
				 (id)kIOSurfacePixelFormat: @((unsigned)'BGRA'), (id)kIOSurfaceBytesPerRow: @256 };
	IOSurfaceRef io = IOSurfaceCreate((CFDictionaryRef)props);
	MTLTextureDescriptor *sd = [MTLTextureDescriptor texture2DDescriptorWithPixelFormat:MTLPixelFormatBGRA8Unorm width:64 height:64 mipmapped:NO];
	sd.usage = MTLTextureUsageRenderTarget | MTLTextureUsageShaderRead;
	sd.storageMode = MTLStorageModeShared;
	id<MTLTexture> st = io ? [[d newTextureWithDescriptor:sd iosurface:io plane:0] autorelease] : nil;
	t = now_ms();
	if (st) {
		MTLRenderPassDescriptor *sp = [[rp copy] autorelease];
		sp.colorAttachments[0].texture = st;
		cb = [self.queue commandBuffer];
		e = [cb renderCommandEncoderWithDescriptor:sp];
		[e setRenderPipelineState:color];
		[e setVertexBuffer:vb offset:0 atIndex:0];
		[e setVertexBuffer:off offset:0 atIndex:2];
		[e drawPrimitives:MTLPrimitiveTypeTriangle vertexStart:0 vertexCount:3];
		[e endEncoding];
		ran = [self run:cb name:"iosurface"];
		IOSurfaceLock(io, kIOSurfaceLockReadOnly, NULL);
		uint32_t *p = IOSurfaceGetBaseAddress(io);
		size_t r = IOSurfaceGetBytesPerRow(io) / 4;
		uint32_t mid = p[34 * r + 32], corner = p[2 * r + 2];
		IOSurfaceUnlock(io, kIOSurfaceLockReadOnly, NULL);
		result(ran && mid == 0xffff0000 && corner == 0xff0000ff, "dessin dans une IOSurface", now_ms() - t,
		    "le CPU lit centre %#x coin %#x", mid, corner);
	} else {
		result(0, "dessin dans une IOSurface", now_ms() - t, "%s", io ? "texture refusée" : "IOSurfaceCreate a échoué");
	}
	if (io) CFRelease(io);

	/* a texture over the app's own memory (no copy), drawn into, read by the CPU: RenderBox's icons */
	vm_address_t page = 0;
	vm_allocate(mach_task_self(), &page, 65536, VM_FLAGS_ANYWHERE);
	memset((void *)page, 0, 65536);
	id<MTLBuffer> nb = [[d newBufferWithBytesNoCopy:(void *)page length:65536 options:MTLResourceStorageModeShared deallocator:nil] autorelease];
	MTLTextureDescriptor *bd = [MTLTextureDescriptor texture2DDescriptorWithPixelFormat:MTLPixelFormatBGRA8Unorm width:64 height:64 mipmapped:NO];
	bd.usage = MTLTextureUsageRenderTarget | MTLTextureUsageShaderRead;
	bd.storageMode = MTLStorageModeShared;
	id<MTLTexture> bt = [[nb newTextureWithDescriptor:bd offset:0 bytesPerRow:256] autorelease];
	t = now_ms();
	if (bt) {
		MTLRenderPassDescriptor *bp = [[rp copy] autorelease];
		bp.colorAttachments[0].texture = bt;
		cb = [self.queue commandBuffer];
		e = [cb renderCommandEncoderWithDescriptor:bp];
		[e setRenderPipelineState:color];
		[e setVertexBuffer:vb offset:0 atIndex:0];
		[e setVertexBuffer:off offset:0 atIndex:2];
		[e drawPrimitives:MTLPrimitiveTypeTriangle vertexStart:0 vertexCount:3];
		[e endEncoding];
		ran = [self run:cb name:"buftex"];
		uint32_t *p = (uint32_t *)page;
		result(ran && p[34 * 64 + 32] == 0xffff0000 && p[2 * 64 + 2] == 0xff0000ff, "dessin dans la mémoire de l'app",
		    now_ms() - t, "centre %#x coin %#x (comme les icônes de RenderBox)", p[34 * 64 + 32], p[2 * 64 + 2]);
	} else {
		result(0, "dessin dans la mémoire de l'app", 0, "%s", nb ? "texture sur tampon refusée" : "tampon sans copie refusé");
	}
}

- (void)testSync
{
	__block BOOL scheduled = NO, completed = NO;
	dispatch_semaphore_t s = dispatch_semaphore_create(0);
	double t = now_ms();
	id<MTLCommandBuffer> cb = [self.queue commandBuffer];
	[cb addScheduledHandler:^(id<MTLCommandBuffer> b) { scheduled = YES; }];
	[cb addCompletedHandler:^(id<MTLCommandBuffer> b) { completed = YES; dispatch_semaphore_signal(s); }];
	[[cb blitCommandEncoder] endEncoding];
	[cb commit];
	long late = dispatch_semaphore_wait(s, dispatch_time(DISPATCH_TIME_NOW, 5 * NSEC_PER_SEC));
	result(!late && scheduled && completed, "handlers scheduled/completed", now_ms() - t, "%s%s%s",
	    scheduled ? "" : "scheduled jamais appelé ; ", completed ? "" : "completed jamais appelé ; ", late ? "pas de réponse en 5 s" : "");

	id<MTLSharedEvent> ev = [[self.dev newSharedEvent] autorelease];
	t = now_ms();
	cb = [self.queue commandBuffer];
	[cb encodeSignalEvent:ev value:7];
	[cb commit];
	[cb waitUntilCompleted];
	result(ev && ev.signaledValue == 7, "MTLSharedEvent", now_ms() - t, "%s valeur %llu (attendu 7)",
	    ev ? "" : "nil ;", (unsigned long long)ev.signaledValue);

	/* 200 small command buffers in a row: the cost of one round trip */
	t = now_ms();
	id<MTLCommandBuffer> last = nil;
	for (int i = 0; i < 200; i++) {
		last = [self.queue commandBuffer];
		[[last blitCommandEncoder] endEncoding];
		[last commit];
	}
	[last waitUntilCompleted];
	double ms = now_ms() - t;
	result(last.status == MTLCommandBufferStatusCompleted, "200 command buffers", ms, "%.2f ms chacun", ms / 200);
}

- (void)testAll
{
	passed = failed = skipped = 0;
	double t = now_ms();
	say("=== Test Metal (B1L-0241), %s ===", getprogname());
	if (![self testDevice]) { say("pas d'appareil Metal : arrêt"); return; }
	[self testShaders];
	[self testBuffers];
	[self testCompute];
	[self testTextures];
	[self testRender];
	[self testIconShaders];   /* B1L-0264 */
	[self testTargets];       /* B1L-0265 */
	[self testSync];
	say("=== %u OK, %u ÉCHEC, %u SAUTÉ en %.1f s ===", passed, failed, skipped, (now_ms() - t) / 1000);
}
@end

/* ----------------------------------------------------- screen ---------------------------------------------------- */

/* a CAMetalLayer an app would draw into, at the screen's refresh: the frame rate is the test */
@interface B2LMetalPresent : NSObject
@property (nonatomic, retain) CAMetalLayer *layer;
@property (nonatomic, retain) id<MTLCommandQueue> queue;
@property (nonatomic, retain) CADisplayLink *link;
@end
@implementation B2LMetalPresent {
	unsigned _frames, _nil;
	double _start;
}
- (void)tick:(CADisplayLink *)l
{
	id<CAMetalDrawable> d = [self.layer nextDrawable];
	if (d == nil) { _nil++; return; }
	if (_frames == 0) _start = now_ms();
	double ph = _frames / 30.0;
	MTLRenderPassDescriptor *rp = [MTLRenderPassDescriptor renderPassDescriptor];
	rp.colorAttachments[0].texture = d.texture;
	rp.colorAttachments[0].loadAction = MTLLoadActionClear;
	rp.colorAttachments[0].storeAction = MTLStoreActionStore;
	rp.colorAttachments[0].clearColor = MTLClearColorMake(0.5 + 0.5 * sin(ph), 0.5 + 0.5 * sin(ph + 2.1), 0.5 + 0.5 * sin(ph + 4.2), 1);
	id<MTLCommandBuffer> cb = [self.queue commandBuffer];
	[[cb renderCommandEncoderWithDescriptor:rp] endEncoding];
	[cb presentDrawable:d];
	[cb commit];
	if (++_frames == 180) {
		double ms = now_ms() - _start;
		result(1, "CAMetalLayer à l'écran", ms, "%.1f images/s sur 180 (%u nextDrawable nil)", 179 * 1000.0 / ms, _nil);
		[self.link invalidate];
		[self.layer removeFromSuperlayer];
	}
}
@end

static UIView *panel;
static UITextView *text;
static B2LMetalSuite *suite;
static B2LMetalPresent *present;

static void
run_all(void)
{
	[report setString:@""];
	if (shown) shown();
	dispatch_async(dispatch_get_global_queue(QOS_CLASS_USER_INITIATED, 0), ^{
		@autoreleasepool {
			[suite release];
			suite = [[B2LMetalSuite alloc] init];
			[suite testAll];
			id<MTLDevice> dev = [suite.dev retain];
			dispatch_async(dispatch_get_main_queue(), ^{
				if (dev == nil) return;
				/* last: frames on the screen, a small square at the top of the panel */
				[present release];
				present = [[B2LMetalPresent alloc] init];
				CAMetalLayer *l = [CAMetalLayer layer];
				l.device = dev;
				l.pixelFormat = MTLPixelFormatBGRA8Unorm;
				l.frame = CGRectMake(panel.bounds.size.width - 110, 50, 90, 90);
				l.contentsScale = UIScreen.mainScreen.scale;
				l.drawableSize = CGSizeMake(90 * l.contentsScale, 90 * l.contentsScale);
				[panel.layer addSublayer:l];
				present.layer = l;
				present.queue = [[dev newCommandQueue] autorelease];
				present.link = [CADisplayLink displayLinkWithTarget:present selector:@selector(tick:)];
				[present.link addToRunLoop:[NSRunLoop mainRunLoop] forMode:NSRunLoopCommonModes];
				[dev release];
			});
		}
	});
}

@interface B2LMetalButtons : NSObject
@end
@implementation B2LMetalButtons
- (void)close:(id)s { panel.hidden = YES; }
- (void)again:(id)s { run_all(); }
@end
static B2LMetalButtons *buttons;

static UIWindow *
find_window(void)
{
	for (UIScene *scene in [UIApplication sharedApplication].connectedScenes) {
		if (![scene isKindOfClass:[UIWindowScene class]]) continue;
		for (UIWindow *w in ((UIWindowScene *)scene).windows)
			if (w.isKeyWindow) return w;
		if (((UIWindowScene *)scene).windows.count) return ((UIWindowScene *)scene).windows.firstObject;
	}
	return nil;
}

static UIButton *
button(NSString *title, CGRect frame, SEL action)
{
	UIButton *b = [UIButton buttonWithType:UIButtonTypeSystem];
	[b setTitle:title forState:UIControlStateNormal];
	b.titleLabel.font = [UIFont boldSystemFontOfSize:17];
	b.frame = frame;
	[b addTarget:buttons action:action forControlEvents:UIControlEventTouchUpInside];
	return b;
}

static void
try_start(int attempt)
{
	UIWindow *w = find_window();
	if (w == nil || w.bounds.size.width < 1) {
		if (attempt < 30)
			dispatch_after(dispatch_time(DISPATCH_TIME_NOW, NSEC_PER_SEC), dispatch_get_main_queue(), ^{ try_start(attempt + 1); });
		else
			say("pas de fenêtre après 30 s");
		return;
	}
	CGRect r = w.bounds;
	buttons = [[B2LMetalButtons alloc] init];
	panel = [[UIView alloc] initWithFrame:r];
	panel.backgroundColor = UIColor.systemBackgroundColor;
	panel.autoresizingMask = UIViewAutoresizingFlexibleWidth | UIViewAutoresizingFlexibleHeight;
	UILabel *title = [[[UILabel alloc] initWithFrame:CGRectMake(16, 50, r.size.width - 140, 30)] autorelease];
	title.text = @"Test Metal";
	title.font = [UIFont boldSystemFontOfSize:28];
	[panel addSubview:title];
	[panel addSubview:button(@"Relancer", CGRectMake(16, 88, 100, 36), @selector(again:))];
	[panel addSubview:button(@"Fermer", CGRectMake(126, 88, 100, 36), @selector(close:))];
	text = [[UITextView alloc] initWithFrame:CGRectMake(8, 150, r.size.width - 16, r.size.height - 170)];
	text.editable = NO;
	text.font = [UIFont monospacedSystemFontOfSize:11 weight:UIFontWeightRegular];
	text.autoresizingMask = UIViewAutoresizingFlexibleWidth | UIViewAutoresizingFlexibleHeight;
	[panel addSubview:text];
	[w addSubview:panel];
	shown = [^{ text.text = report; } copy];
	run_all();
}

__attribute__((constructor)) static void
b2l_metal_suite_init(void)
{
	report = [[NSMutableString alloc] init];
	say("dans %s (pid %d)", getprogname(), getpid());
	dispatch_after(dispatch_time(DISPATCH_TIME_NOW, 2 * NSEC_PER_SEC), dispatch_get_main_queue(), ^{ try_start(0); });
}
