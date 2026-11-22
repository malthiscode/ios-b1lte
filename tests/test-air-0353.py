#!/usr/bin/env python3
# B1L-0353: flat 16-bit varyings through the G76 compiler, with its assertions on ("sz == 32", bi_emit_load_vary).
# Needs a Mac with Metal, scripts/build-mesa.sh's tree and scripts/build-bicomp.sh.
import os, subprocess, tempfile

BICOMP = os.environ.get('MESA_BUILD', os.path.expanduser('~/Developer/s10-port/build-mesa')) + '/build-host-pan/b2l-bicomp'
TOOL = r'''#import <Foundation/Foundation.h>
#import <Metal/Metal.h>
#import <objc/message.h>
int main(int c, char **v) { @autoreleasepool {
	NSString *src = @"using namespace metal;"
	    "struct V { float4 p [[position]]; half a [[flat]]; half2 b [[flat]]; half4 c [[flat]]; short i [[flat]]; ushort u [[flat]]; };"
	    "fragment float4 f(V in [[stage_in]]) { return float4(in.a + in.c.x, in.b.y * in.c.w, float(in.i), float(in.u)); }";
	NSError *e = nil;
	id<MTLLibrary> l = [MTLCreateSystemDefaultDevice() newLibraryWithSource:src options:nil error:&e];
	if (!l) { NSLog(@"%@", e); return 1; }
	NSData *bc = ((NSData *(*)(id, SEL))objc_msgSend)([l newFunctionWithName:@"f"], sel_getUid("bitcodeData"));
	return [bc writeToFile:@(v[1]) atomically:YES] ? 0 : 1;
} }
'''
with tempfile.TemporaryDirectory() as d:
    open(d + '/t.m', 'w').write(TOOL)
    subprocess.run(['xcrun', '--sdk', 'macosx', 'clang', '-fobjc-arc', '-framework', 'Foundation', '-framework', 'Metal',
                    d + '/t.m', '-o', d + '/t'], check=True)
    subprocess.run([d + '/t', d + '/f.air'], check=True)
    r = subprocess.run([BICOMP, d + '/f.air', '--disasm'], capture_output=True, text=True, errors='replace')
    assert r.returncode == 0, r.stderr[-400:]
    out = r.stdout + r.stderr
    loads = [x.strip() for x in out.splitlines() if 'LD_VAR_FLAT' in x]
    assert loads and all('.u32' in x for x in loads), loads
print('test-air-0353: flat half, short and ushort varyings compile for the G76, loaded in 32 bits')
