/* SPDX-License-Identifier: BSD-2-Clause
 * B1L-0339: one app (an unpacked .ipa) installed by the kernel, as the 50 system apps were (B1L-0024/0025): a plain
 * app in /Applications, registered by lsd from a fresh LaunchServices store. iOS's installer route never worked here.
 *
 * scripts/stage-app.py puts the bundle's files on SYSTEM as B2L_APP_SRC/fNNNNN with B2L_APP_SRC/manifest.txt
 * ("D path" / "F NNNNN path" lines). Once per root, before launchd:
 *   1. every directory and file is made under B2L_APP_STAGE/B2L_APP_NAME (a file is written to a temporary name and
 *      renamed into place, so an interrupted boot resumes where it stopped: files already there are kept);
 *   2. the finished bundle is renamed to /Applications/B2L_APP_NAME (a target already there is never replaced);
 *   3. lsd's two LaunchServices stores are renamed *.b2l-before-0339, so lsd rebuilds them from /Applications;
 *   4. B2L_APP_MARKER is written.
 * Kill switch: boot-arg b2l_app_gd=0. Included by b2l_userspace_seed.h after b2l_apps_layout.h.
 */
#ifndef B2L_APP_INSTALL_H
#define B2L_APP_INSTALL_H

#define B2L_APP_SRC      "b2l-app-gd1"
#define B2L_APP_NAME     "GeometryJump.app"
#define B2L_APP_STAGE    "/private/var/b2l-app-gd1"
#define B2L_APP_MARKER   "/private/var/db/b2l-app-gd1"
#define B2L_APP_MANIFEST (1u << 20)

/* src (on SYSTEM) to final, through tmp in the same directory. 0 written or already there. */
static int
b2l_app_copy_one(const char *src, const char *final, const char *tmp, vfs_context_t ctx)
{
	vnode_t vp = NULL;
	uint64_t size = 0;
	int ex = b2l_layout_exists(final, ctx), error, ce;

	if (ex != 0) {
		return ex < 0 ? -ex : 0;
	}
	error = vnode_open(tmp, FREAD | FWRITE | O_CREAT | O_TRUNC, 0755, VNODE_LOOKUP_NOFOLLOW_ANY, &vp, ctx);
	if (error) {
		return error;
	}
	error = b2l_system_file_copy(src, b2l_gl_sink, vp, &size);
	if (!error) {
		error = VNOP_FSYNC(vp, MNT_WAIT, ctx);
	}
	ce = vnode_close(vp, FREAD | FWRITE, ctx);
	if (!error) {
		error = ce;
	}
	if (!error) {
		error = b2l_kernel_rename_excl(tmp, final, ctx);
	}
	return error;
}

/* A manifest path: relative, no "..", no empty part. */
static int
b2l_app_path_ok(const char *p)
{
	if (p[0] == '\0' || p[0] == '/') {
		return 0;
	}
	for (; *p; p++) {   /* the kernel has no strstr */
		if ((p[0] == '.' && p[1] == '.') || (p[0] == '/' && p[1] == '/')) {
			return 0;
		}
	}
	return 1;
}

static void
b2l_app_install(vfs_context_t ctx)
{
	static char root[MAXPATHLEN], final[MAXPATHLEN], tmp[MAXPATHLEN], src[64];
	struct b2l_wifi_buf m = { NULL, B2L_APP_MANIFEST - 1 };   /* one byte left: the manifest ends in a NUL */
	unsigned files = 0, dirs = 0;
	uint64_t len = 0;
	int on = 1, error = 0, r;

	PE_parse_boot_argn("b2l_app_gd", &on, sizeof(on));
	if (!on) {
		printf("B2L APP: disabled by boot-arg\n");
		return;
	}
	if (b2l_layout_exists(B2L_APP_MARKER, ctx) == 1) {
		printf("B2L APP: %s already installed (%s)\n", B2L_APP_NAME, B2L_APP_MARKER);
		return;
	}
	m.p = kalloc_data(B2L_APP_MANIFEST, Z_WAITOK | Z_ZERO);
	if (!m.p) {
		error = ENOMEM;
	}
	if (!error) {
		error = b2l_system_file_copy(B2L_APP_SRC "/manifest.txt", b2l_wifi_sink, &m, &len);
	}
	if (!error && (b2l_layout_join(root, sizeof(root), B2L_APP_STAGE "/", B2L_APP_NAME, NULL) ||
	    b2l_layout_join(tmp, sizeof(tmp), B2L_APP_STAGE "/", ".b2l-tmp", NULL))) {
		error = ENAMETOOLONG;
	}
	if (!error) {
		error = b2l_apps_directory(B2L_APP_STAGE, ctx);
	}
	if (!error) {
		error = b2l_apps_directory(root, ctx);
	}
	for (char *line = (char *)m.p, *end = line + len; !error && line < end;) {
		char *nl = memchr(line, '\n', (size_t)(end - line));

		if (!nl) {
			error = EINVAL;
			break;
		}
		*nl = '\0';
		if (line[0] == 'D' && line[1] == ' ' && b2l_app_path_ok(line + 2)) {
			error = b2l_layout_join(final, sizeof(final), root, "/", line + 2);
			if (!error) {
				error = b2l_apps_directory(final, ctx);
			}
			dirs++;
		} else if (line[0] == 'F' && line[1] == ' ' && nl - line > 8 && line[7] == ' ' && b2l_app_path_ok(line + 8)) {
			error = (size_t)snprintf(src, sizeof(src), "%s/f%.5s", B2L_APP_SRC, line + 2) < sizeof(src) ? 0 : ENAMETOOLONG;
			if (!error) {
				error = b2l_layout_join(final, sizeof(final), root, "/", line + 8);
			}
			if (!error) {
				error = b2l_app_copy_one(src, final, tmp, ctx);
			}
			files++;
		} else {
			error = EINVAL;
		}
		if (error) {
			printf("B2L APP: manifest line '%s' error=%d\n", line, error);
		}
		line = nl + 1;
	}
	if (!error) {
		r = b2l_layout_move(root, "/Applications/" B2L_APP_NAME, ctx);
		error = r < 0 ? -r : 0;
		printf("B2L APP: %s -> /Applications %s\n", B2L_APP_NAME, r == 0 ? "moved" : r == 1 ? "already there" : "error");
	}
	if (!error) {
		static const char *const stores[] = {
			"com.apple.LaunchServices-20971545-v2.csstore",
			"SystemDataOnly-com.apple.LaunchServices-20971545-v2.csstore",
		};
		for (unsigned i = 0; i < 2; i++) {
			if (!b2l_layout_join(root, sizeof(root), B2L_LAYOUT_LS_DIR, stores[i], NULL) &&
			    !b2l_layout_join(final, sizeof(final), B2L_LAYOUT_LS_DIR, stores[i], ".b2l-before-0339")) {
				r = b2l_layout_move(root, final, ctx);
				printf("B2L APP: %s %s\n", stores[i], r == 0 ? "renamed" : r == 1 ? "absent/kept" : "error");
			}
		}
		error = b2l_layout_marker(B2L_APP_MARKER, ctx);
	}
	if (!error) {
		error = VFS_SYNC(vnode_mount(rootvnode), MNT_WAIT, ctx);
	}
	if (m.p) {
		kfree_data(m.p, B2L_APP_MANIFEST);
	}
	printf("B2L APP: %s: %u files, %u directories, manifest %llu bytes, error=%d\n",
	    B2L_APP_NAME, files, dirs, (unsigned long long)len, error);
}

/*
 * B1L-0340: 0339 put the app in /Applications, but lsd's stores were not under the names B1L-0025 used
 * ("absent/kept"), so lsd kept its old store and the app never showed. Its container's UUID is not fixed (a clean
 * USERDATA makes a new one), so the stores are looked for: every entry of every
 * /private/var/mobile/Containers/Data/InternalDaemon/X/Library/Caches/ whose name has "LaunchServices" and ends in
 * ".csstore" is logged and renamed NAME.b2l-before-0340. Once (B2L_APP_LS_MARKER); boot-arg b2l_app_gd=0 skips it.
 */
#include <sys/dirent.h>
#define B2L_APP_LS_MARKER "/private/var/db/b2l-app-ls-reset-v1"
#define B2L_APP_LS_ROOT   "/private/var/mobile/Containers/Data/InternalDaemon/"

/* Calls fn(name, arg) for each entry of path (not "." / ".."). Names are copied out before fn runs. */
static int
b2l_app_each_entry(const char *path, void (*fn)(const char *, void *), void *arg, vfs_context_t ctx)
{
	typedef char b2l_name_t[MAXNAMLEN + 1];
	b2l_name_t *names = kalloc_data(256 * sizeof(b2l_name_t), Z_WAITOK | Z_ZERO);   /* per call: the walk nests; one container per daemon */
	vnode_t dvp = NULL;
	int error = vnode_lookup(path, VNODE_LOOKUP_NOFOLLOW_ANY, &dvp, ctx), eof = 0, count = 0;
	unsigned n = 0;
	char *buf;
	uio_t uio;

	if (error || !names) {
		if (names) {
			kfree_data(names, 256 * sizeof(b2l_name_t));
		}
		if (!error) {
			vnode_put(dvp);
		}
		return error ? error : ENOMEM;
	}
	if (vnode_vtype(dvp) != VDIR) {
		vnode_put(dvp);
		kfree_data(names, 256 * sizeof(b2l_name_t));
		return ENOTDIR;
	}
	buf = kalloc_data(8192, Z_WAITOK | Z_ZERO);
	uio = uio_create(1, 0, UIO_SYSSPACE, UIO_READ);
	if (!buf || !uio) {
		error = ENOMEM;
	}
	while (!error && !eof && n < 256) {
		uio_reset(uio, uio_offset(uio), UIO_SYSSPACE, UIO_READ);
		uio_addiov(uio, CAST_USER_ADDR_T(buf), 8192);
		error = VNOP_READDIR(dvp, uio, 0, &eof, &count, ctx);
		size_t got = 8192 - (size_t)uio_resid(uio);
		if (error || got == 0) {
			break;
		}
		for (size_t off = 0; off + offsetof(struct dirent, d_name) < got && n < 256;) {
			struct dirent *d = (struct dirent *)(buf + off);
			if (d->d_reclen == 0) {
				break;
			}
			if (d->d_ino != 0 && d->d_namlen > 0 && d->d_namlen <= MAXNAMLEN &&
			    !(d->d_namlen == 1 && d->d_name[0] == '.') &&
			    !(d->d_namlen == 2 && d->d_name[0] == '.' && d->d_name[1] == '.')) {
				memcpy(names[n], d->d_name, d->d_namlen);
				names[n][d->d_namlen] = '\0';
				n++;
			}
			off += d->d_reclen;
		}
	}
	if (uio) {
		uio_free(uio);
	}
	if (buf) {
		kfree_data(buf, 8192);
	}
	vnode_put(dvp);
	for (unsigned i = 0; !error && i < n; i++) {
		fn(names[i], arg);
	}
	kfree_data(names, 256 * sizeof(b2l_name_t));
	return error;
}

struct b2l_app_ls_state {
	vfs_context_t ctx;
	char dir[MAXPATHLEN];
	unsigned found, renamed;
};

static int
b2l_app_has(const char *s, const char *part)
{
	size_t lp = strlen(part);
	for (; *s; s++) {
		if (strncmp(s, part, lp) == 0) {
			return 1;
		}
	}
	return 0;
}

static void
b2l_app_ls_store(const char *name, void *arg)
{
	struct b2l_app_ls_state *st = arg;
	static char from[MAXPATHLEN], to[MAXPATHLEN];
	size_t l = strlen(name);

	if (!b2l_app_has(name, "LaunchServices") || l < 8 || strcmp(name + l - 8, ".csstore") != 0) {
		return;
	}
	st->found++;
	if (b2l_layout_join(from, sizeof(from), st->dir, name, NULL) ||
	    b2l_layout_join(to, sizeof(to), st->dir, name, ".b2l-before-0340")) {
		return;
	}
	int r = b2l_layout_move(from, to, st->ctx);
	if (r == 0) {
		st->renamed++;
	}
	printf("B2L APP LS: %s%s %s\n", st->dir, name, r == 0 ? "renamed" : r == 1 ? "kept" : "error");
}

static void
b2l_app_ls_container(const char *uuid, void *arg)
{
	struct b2l_app_ls_state *st = arg;

	if (b2l_layout_join(st->dir, sizeof(st->dir), B2L_APP_LS_ROOT, uuid, "/Library/Caches/")) {
		return;
	}
	(void)b2l_app_each_entry(st->dir, b2l_app_ls_store, st, st->ctx);
}

static void
b2l_app_ls_reset(vfs_context_t ctx)
{
	static struct b2l_app_ls_state st;
	int on = 1, error;

	PE_parse_boot_argn("b2l_app_gd", &on, sizeof(on));
	if (!on || b2l_layout_exists(B2L_APP_LS_MARKER, ctx) == 1 || b2l_layout_exists(B2L_APP_MARKER, ctx) != 1) {
		return;
	}
	st.ctx = ctx;
	st.found = st.renamed = 0;
	error = b2l_app_each_entry(B2L_APP_LS_ROOT, b2l_app_ls_container, &st, ctx);
	if (!error && st.renamed) {
		error = b2l_layout_marker(B2L_APP_LS_MARKER, ctx);
	}
	if (!error) {
		error = VFS_SYNC(vnode_mount(rootvnode), MNT_WAIT, ctx);
	}
	printf("B2L APP LS: %u stores found, %u renamed, error=%d\n", st.found, st.renamed, error);
}

#endif /* B2L_APP_INSTALL_H */
