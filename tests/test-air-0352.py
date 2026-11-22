#!/usr/bin/env python3
# B1L-0352: the patched air_metal.c's own region check, row length, read loop and zero constant buffer, on this Mac with
# Mesa's real format tables. Needs scripts/build-mesa.sh (the tree) and scripts/build-air-host.sh (libmesa_util.a).
import os, re, subprocess, tempfile

B = os.environ.get('MESA_BUILD', os.path.expanduser('~/Developer/s10-port/build-mesa'))
M = B + '/mesa-25.0.7'
src = open(M + '/src/compiler/air/air_metal.c').read()


def function(name):
    return re.search(r'^static bool\n%s\(.*?^}\n' % name, src, re.S | re.M).group(0)


def between(a, b):
    i = src.index(a)
    return src[i:src.index(b, i) + len(b)]


loop = between('   size_t texel_row = util_format_get_stride(', 'texel_row);\n')
zero = between('         if (!b) {   /* B2L (B1L-0352): unbound: zeros */', '            break;\n         }\n')
astc = re.findall(r'\{ (\d+), "MTLPixelFormatASTC_(\w+?)_(sRGB|LDR)", (PIPE_FORMAT_ASTC_\w+) \}', src)
assert len(astc) == 28, len(astc)

program = '''#include <assert.h>
#include <stdarg.h>
#include <stdio.h>
#include <string.h>
#include "pipe/p_state.h"
#include "pipe/p_context.h"
#include "util/format/u_format.h"
#include "util/u_math.h"
static void say(char *w, size_t n, const char *f, ...) { va_list a; va_start(a, f); vsnprintf(w, n, f, a); va_end(a); }
struct rwa_texture { struct pipe_resource *res; const char *format_name; unsigned layers; };
%s
%s
static void read_back(struct rwa_texture *texture, struct pipe_box box, const uint8_t *map, unsigned pitch,
                      uint8_t *bytes, size_t row)
{
   struct pipe_transfer t = { .stride = pitch, .layer_stride = pitch * 64 }, *xfer = &t;
%s}
static struct pipe_constant_buffer got;
static void set(struct pipe_context *p, enum pipe_shader_type s, uint i, bool o, const struct pipe_constant_buffer *c)
{ got = *c; }
static void bind(unsigned read_end, unsigned size)
{
   struct { unsigned b2l_read_end, size, index; } res = { read_end, size, 5 }, *r = &res;
   struct { unsigned ubo_base; } dev = { 1 }, *device = &dev;
   struct pipe_context ctx = { .set_constant_buffer = set }, *pipe = &ctx;
   enum pipe_shader_type stage = PIPE_SHADER_FRAGMENT;
   const char *name = "test"; void *b = NULL;
   switch (0) { case 0: {
%s   } }
}
int main(void)
{
   char why[256]; size_t row;
   struct pipe_resource res = { .format = PIPE_FORMAT_ASTC_4x4, .target = PIPE_TEXTURE_2D, .width0 = 4092,
                                .height0 = 992, .depth0 = 1, .array_size = 1, .last_level = 5 };
   struct rwa_texture t = { &res, "ASTC", 1 };
   struct pipe_box box;
   unsigned o[3] = { 0 }, s[3] = { 4092, 992, 1 };
   assert(texture_box(&t, 0, 0, o, s, &box, why, sizeof why));
   assert(caller_stride(&t, &box, 0, &row, why, sizeof why) && row == 16368);
   assert(util_format_get_nblocksy(res.format, box.height) == 248);
   assert(caller_stride(&t, &box, 16368, &row, why, sizeof why));
   assert(!caller_stride(&t, &box, 16352, &row, why, sizeof why));
   o[0] = 2; s[0] = 4;
   assert(!texture_box(&t, 0, 0, o, s, &box, why, sizeof why));   /* starts inside a block */
   o[0] = 4; s[0] = 6;
   assert(!texture_box(&t, 0, 0, o, s, &box, why, sizeof why));   /* ends inside one */
   o[0] = 4092; s[0] = 4;
   assert(!texture_box(&t, 0, 0, o, s, &box, why, sizeof why));   /* past the level */
   o[0] = 2044; s[0] = 2; s[1] = 496;
   assert(texture_box(&t, 1, 0, o, s, &box, why, sizeof why));    /* 2046 wide: the last block is cut by the edge */

   static uint8_t map[32 * 64 * 2], out[64 + 16];
   memset(map, 7, sizeof map); memset(out, 0xee, sizeof out);
   read_back(&t, (struct pipe_box){ .width = 8, .height = 8, .depth = 1 }, map, 64, out, 32);
   for (int i = 0; i < 80; i++) assert(out[i] == (i < 64 ? 7 : 0xee));   /* two rows of two blocks, no more */

   res.format = PIPE_FORMAT_B8G8R8A8_UNORM;
   o[0] = 3; s[0] = 5; s[1] = 7;
   assert(texture_box(&t, 0, 0, o, s, &box, why, sizeof why));    /* other formats as before */
   assert(caller_stride(&t, &box, 0, &row, why, sizeof why) && row == 20);

   bind(32, 0);  assert(got.buffer_size == 32 && got.user_buffer && !got.buffer);
   bind(0, 12);  assert(got.buffer_size == 16);
   bind(0, 0);   assert(got.buffer_size == 4096);
   bind(1 << 20, 0); assert(got.buffer_size == 65536);
   for (unsigned i = 0; i < 16384; i++) assert(((const uint32_t *)got.user_buffer)[i] == 0);
%s   puts("air 0352: regions, rows, read loop, zero buffer, 28 formats: ok");
   return 0;
}
''' % (function('texture_box'), function('caller_stride'), loop, zero,
       ''.join('   assert(util_format_is_compressed(%s) && util_format_get_blocksize(%s) == 16 &&\n'
               '          util_format_get_blockwidth(%s) == %s && util_format_get_blockheight(%s) == %s);\n'
               % (p, p, p, size.split('x')[0], p, size.split('x')[1]) for _, size, _, p in astc))

with tempfile.TemporaryDirectory() as d:
    open(d + '/t.c', 'w').write(program)
    H = B + '/build-host/src/'
    inc = [M + '/include', M + '/src', M + '/src/gallium/include', M + '/src/gallium/auxiliary', B + '/build-host/include',
           B + '/build-host/src', H + 'util/format']
    subprocess.run(['xcrun', '--sdk', 'macosx', 'clang', '-std=gnu11', '-w', '-UNDEBUG', '-DHAVE_PTHREAD=1',
                    '-DUTIL_ARCH_LITTLE_ENDIAN=1', '-DUTIL_ARCH_BIG_ENDIAN=0', '-DHAVE_STRUCT_TIMESPEC=1',
                    *['-I' + i for i in inc], d + '/t.c', H + 'util/libmesa_util.a', H + 'util/libmesa_util_sse41.a',
                    H + 'c11/impl/libmesa_util_c11.a', H + 'util/blake3/libblake3.a', '-lz', '-lc++', '-o', d + '/t'],
                   check=True)
    subprocess.run([d + '/t'], check=True)
