#!/bin/bash
# SPDX-License-Identifier: BSD-2-Clause
# B1L-0342: libb2l-gl-app.dylib, OpenGL ES for apps: the EAGL bridge as build-gl.sh prepared it ($O/src: eagl.c,
# gl_osmesa.c, eagl_gen.h) + userspace/b2l_eagl_app.m (CAEAGLLayer, setCurrentContext), linked to the Mesa already
# published by build-gl.sh's v9 (/usr/local/lib/b2l-gl-v9/libOSMesa.8.dylib). Without b2l_accel_gles.c: that one
# patches QuartzCore for the window server. Output $O/$V/libb2l-gl-app.dylib.
#   V=b2l-gl-v14 bash scripts/build-gl-app.sh
set -euo pipefail
V=${V:-b2l-gl-v14}
R=$(cd "$(dirname "$0")/.." && pwd)
O=${GL_BUILD:-$HOME/Developer/s10-port/build-gl}
SDK=$(xcrun --sdk iphoneos --show-sdk-path)
mkdir -p "$O/$V"
# eagl.c first: its constructor (the bridge) must run before b2l_eagl_app.m's, which wraps what the bridge put in
xcrun --sdk iphoneos clang -target arm64e-apple-ios26.0 -isysroot "$SDK" -O2 -dynamiclib \
	-install_name /usr/local/lib/$V/libb2l-gl-app.dylib -I"$O/src" \
	"$O/b2l-gl-v9/libOSMesa.8.dylib" \
	-framework CoreFoundation -framework Foundation -framework QuartzCore -framework UIKit -framework IOKit -framework IOSurface \
	-framework OpenGLES -lobjc \
	-o "$O/$V/libb2l-gl-app.dylib" \
	"$O/src/eagl.c" "$O/src/gl_osmesa.c" "$R/userspace/b2l_eagl_app.m"
/usr/bin/codesign --force --sign - --identifier dev.b2l.gl.app "$O/$V/libb2l-gl-app.dylib"
/usr/bin/codesign -v "$O/$V/libb2l-gl-app.dylib"
if xcrun dyld_info -imports "$O/$V/libb2l-gl-app.dylib" | grep -E '_gl[A-Z][A-Za-z0-9]*  \(from OpenGLES\)' >/dev/null; then
	echo "build-gl-app: gl* still imported from OpenGLES" >&2; exit 1
fi
xcrun dyld_info -imports "$O/$V/libb2l-gl-app.dylib" | grep -c "from libOSMesa" || true
otool -L "$O/$V/libb2l-gl-app.dylib" | grep -i mesa
shasum -a 256 "$O/$V/libb2l-gl-app.dylib"
