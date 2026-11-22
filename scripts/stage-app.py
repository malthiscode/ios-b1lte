#!/usr/bin/env python3
# SPDX-License-Identifier: BSD-2-Clause
"""Stage an unpacked .app for the kernel installer (B1L-0339, xnu-overlay/bsd/kern/b2l_app_install.h).

system-add-fast.sh copies only the top-level files of a directory onto SYSTEM, and the kernel reads files of
1 byte to 64 MB. So every file of the bundle becomes OUT/fNNNNN, and OUT/manifest.txt lists, one per line:
  D <path>            a directory, relative to the bundle, parents first
  F <NNNNN> <path>    a file, copied from fNNNNN
Paths are checked against what the kernel accepts: no '..', no spaces, under 900 bytes.

  stage-app.py APP_DIR OUT_DIR
"""
import argparse, os, re, shutil, sys

OK = re.compile(r'^[A-Za-z0-9_./@~+-]+$')


def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument('app'); p.add_argument('out')
    a = p.parse_args()
    app = os.path.realpath(a.app)
    if not app.endswith('.app') or not os.path.isdir(app):
        sys.exit('not an .app directory: ' + app)
    if os.path.exists(a.out):
        shutil.rmtree(a.out)
    os.makedirs(a.out)
    lines, n = [], 0
    for top, dirs, files in os.walk(app):
        dirs.sort(); files.sort()
        rel = os.path.relpath(top, app)
        if rel != '.':
            lines.append('D ' + rel)
        for f in files:
            src = os.path.join(top, f)
            path = f if rel == '.' else rel + '/' + f
            if os.path.islink(src) or not os.path.isfile(src):
                sys.exit('not a regular file: ' + path)
            size = os.path.getsize(src)
            if not OK.match(path) or '..' in path.split('/') or len(path) > 900:
                sys.exit('path the kernel would refuse: ' + path)
            if size == 0 or size > 64 << 20:
                sys.exit('size the kernel would refuse (%d): %s' % (size, path))
            shutil.copyfile(src, os.path.join(a.out, 'f%05d' % n))
            lines.append('F %05d %s' % (n, path))
            n += 1
    for d in [l[2:] for l in lines if l[0] == 'D']:
        if not OK.match(d) or '..' in d.split('/'):
            sys.exit('directory name the kernel would refuse: ' + d)
    data = ('\n'.join(lines) + '\n').encode()
    if len(data) > 1 << 20:
        sys.exit('manifest over 1 MB')
    open(os.path.join(a.out, 'manifest.txt'), 'wb').write(data)
    print('staged %d files, %d directories, manifest %d bytes' % (n, sum(1 for l in lines if l[0] == 'D'), len(data)))


if __name__ == '__main__':
    main()
