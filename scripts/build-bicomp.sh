#!/bin/bash
# B1L-0224: rebuild b2l-bicomp (userspace/mesa/b2l-bicomp.c) against build-mesa/build-host-pan (debug Mesa, assertions on)
# with whatever src/compiler/air currently holds. Usage: bash scripts/build-bicomp.sh
set -euo pipefail
B=${MESA_BUILD:-$HOME/Developer/s10-port/build-mesa}
R=$(cd "$(dirname "$0")/.." && pwd)
export SDKROOT=$(xcrun --sdk macosx --show-sdk-path) PATH=$B/venv/bin:$HOME/Developer/tools/mamba/envs/s10/bin:$PATH
cd "$B/build-host-pan"
ninja all src/compiler/air/libair.a >/dev/null   # libair has no consumer in this build, so name it
CMD=$(python3 - <<'PY'
import json, shlex
e = [e for e in json.load(open('compile_commands.json')) if e['file'].endswith('bifrost_compile.c')][0]['command']
out, skip = [], False
for x in shlex.split(e):
    if skip: skip = False; continue
    if x in ('-o', '-c', '-MF', '-MQ'): skip = True; continue
    if x == '-MD' or x.endswith('bifrost_compile.c'): continue
    out.append(x)
print(shlex.join(out))
PY
)
eval "$CMD -Wno-missing-prototypes -I../mesa-25.0.7/src/panfrost -Isrc/panfrost -I../mesa-25.0.7/src/compiler/air -Isrc/compiler/air -c '$R/userspace/mesa/b2l-bicomp.c' -o b2l-bicomp.o"
L="src/compiler/air/libair.a src/panfrost/compiler/libpanfrost_bifrost.a src/panfrost/compiler/libpanfrost_bifrost_disasm.a src/panfrost/compiler/valhall/libpanfrost_valhall_disasm.a src/panfrost/util/libpanfrost_util.a src/panfrost/shared/libpanfrost_shared.a src/compiler/nir/libnir.a src/compiler/libcompiler.a src/util/libmesa_util.a src/util/libmesa_util_sse41.a src/util/libxmlconfig.a src/c11/impl/libmesa_util_c11.a src/util/blake3/libblake3.a"
clang -g b2l-bicomp.o $L $L -lz -lc++ -o b2l-bicomp 2>&1 | grep -v 'warning' || true
echo "$B/build-host-pan/b2l-bicomp"
