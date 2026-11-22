#!/usr/bin/env python3
# SPDX-License-Identifier: BSD-2-Clause
# B1L-0350: b2l_eagl_app.m's own blit and choice of surface, run on the Mac's OpenGL: top-down BGRA, scissor ignored
# and put back, and each of the three surfaces used in turn.
import pathlib, subprocess, tempfile

src = (pathlib.Path(__file__).resolve().parent.parent / 'userspace/b2l_eagl_app.m').read_text()
blit = src[src.index('static void blit_turned_over'):src.index('static BOOL my_renderbufferStorage')]
pick = src[src.index('\tint first = d->shown % SURFACES'):src.index('\tIOSurfaceRef surface = d->surfaces[k];')]

test = '''#define GL_SILENCE_DEPRECATION
#include <OpenGL/OpenGL.h>
#include <OpenGL/gl3.h>
#include <assert.h>
#include <stdint.h>
#include <string.h>
''' + blit + '''
#define SURFACES 3
static int busy[3];
#define IOSurfaceIsInUse(s) busy[s]
static int pick(int shown) {
	struct { int shown, surfaces[3]; } v = { shown, { 0, 1, 2 } }, *d = &v;
''' + pick + '''	return k;
}
int main(void) {
	assert(pick(0) == 0 && pick(1) == 1 && pick(2) == 2 && pick(3) == 0);
	busy[1] = 1; assert(pick(1) == 2);
	memset(busy, 1, sizeof(busy)); assert(pick(1) == 1);

	CGLPixelFormatAttribute a[] = { kCGLPFAOpenGLProfile, (CGLPixelFormatAttribute)kCGLOGLPVersion_3_2_Core, 0 };
	CGLPixelFormatObj f; GLint n; CGLContextObj c;
	assert(!CGLChoosePixelFormat(a, &f, &n) && f && !CGLCreateContext(f, NULL, &c));
	CGLSetCurrentContext(c);
	enum { W = 8, H = 6 };
	GLuint rb, t, fb[3], seen[W * H];
	glGenRenderbuffers(1, &rb); glBindRenderbuffer(GL_RENDERBUFFER, rb);
	glRenderbufferStorage(GL_RENDERBUFFER, GL_RGBA8, W, H);
	glGenFramebuffers(2, fb); glBindFramebuffer(GL_FRAMEBUFFER, fb[0]);
	glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_RENDERBUFFER, rb);
	glEnable(GL_SCISSOR_TEST);
	for (int y = 0; y < H; y++) for (int x = 0; x < W; x++) {
		glScissor(x, y, 1, 1);
		glClearColor((x * 30 + 7) / 255.f, (y * 40 + 3) / 255.f, 200 / 255.f, 128 / 255.f);
		glClear(GL_COLOR_BUFFER_BIT);
	}
	glGenTextures(1, &t); glBindTexture(GL_TEXTURE_2D, t);
	glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, W, H, 0, GL_BGRA, GL_UNSIGNED_INT_8_8_8_8_REV, NULL);
	glBindFramebuffer(GL_FRAMEBUFFER, fb[1]);
	glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, t, 0);
	glBindFramebuffer(GL_FRAMEBUFFER, fb[0]);
	glScissor(2, 2, 1, 1);
	blit_turned_over(fb[1], W, H);
	GLint draw; glGetIntegerv(GL_DRAW_FRAMEBUFFER_BINDING, &draw);
	assert(glIsEnabled(GL_SCISSOR_TEST) && (GLuint)draw == fb[0]);
	glGetTexImage(GL_TEXTURE_2D, 0, GL_BGRA, GL_UNSIGNED_INT_8_8_8_8_REV, seen);
	for (int y = 0; y < H; y++) for (int x = 0; x < W; x++)
		assert(seen[y * W + x] == (128u << 24 | (uint32_t)(x * 30 + 7) << 16 | (uint32_t)((H - 1 - y) * 40 + 3) << 8 | 200u));
	assert(glGetError() == 0);
}
'''
with tempfile.TemporaryDirectory() as d:
    c = pathlib.Path(d, 't.c'); c.write_text(test)
    subprocess.run(['clang', '-framework', 'OpenGL', str(c), '-o', f'{d}/t'], check=True)
    subprocess.run([f'{d}/t'], check=True)
print('test-eagl-present: ok')
