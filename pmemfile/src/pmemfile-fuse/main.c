/*
 * Copyright 2017, Intel Corporation
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 *
 *     * Redistributions of source code must retain the above copyright
 *       notice, this list of conditions and the following disclaimer.
 *
 *     * Redistributions in binary form must reproduce the above copyright
 *       notice, this list of conditions and the following disclaimer in
 *       the documentation and/or other materials provided with the
 *       distribution.
 *
 *     * Neither the name of the copyright holder nor the names of its
 *       contributors may be used to endorse or promote products derived
 *       from this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 * "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 * LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
 * A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
 * OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
 * SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
 * LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 * DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 * THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
 * OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#define _GNU_SOURCE

#include "libpmemfile-posix.h"
#include <assert.h>
#include <err.h>
#include <errno.h>
#include <fuse.h>
#include <getopt.h>
#include <limits.h>
#include <stdbool.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>

#define PFP ((PMEMfilepool *)(fuse_get_context()->private_data))

#ifdef DEBUG
#define log(fmt, args...) fprintf(stderr, "%s " fmt, __func__, ## args)
#else
#define log(fmt, args...) do {} while (0)
#endif

static int
update_ctx(PMEMfilepool *pfp)
{
	struct fuse_context *ctx = fuse_get_context();

	if (pmemfile_setreuid(pfp, ctx->uid, ctx->uid) < 0) {
		log("pmemfile_setreuid failed: %d\n", errno);
		return -errno;
	}
	if (pmemfile_setregid(pfp, ctx->gid, ctx->gid) < 0) {
		log("pmemfile_setregid failed: %d\n", errno);
		return -errno;
	}
	pmemfile_umask(pfp, ctx->umask);

	return 0;
}

static int
pmemfile_fuse_getattr(const char *path, struct stat *statbuf)
{
	log("%s\n", path);

	int ret;
	PMEMfilepool *pfp = PFP;

	if ((ret = update_ctx(pfp)) < 0)
		return ret;

	if (pmemfile_lstat(pfp, path, statbuf) < 0) {
		log("pmemfile_lstat %s failed: %d\n", path, errno);
		return -errno;
	}

	return 0;
}

static int
pmemfile_fuse_opendir(const char *path, struct fuse_file_info *fi)
{
	log("%s\n", path);

	int ret;
	PMEMfilepool *pfp = PFP;

	if ((ret = update_ctx(pfp)) < 0)
		return ret;

	PMEMfile *f = pmemfile_open(pfp, path, O_DIRECTORY);
	if (f == NULL) {
		log("pmemfile_open %s failed: %d\n", path, errno);
		return -errno;
	}
	fi->fh = (uintptr_t)f;

	return 0;
}

static int
pmemfile_fuse_releasedir(const char *path, struct fuse_file_info *fi)
{
	log("%s\n", path);

	if (!fi->fh)
		return -EBADF;

	pmemfile_close(PFP, (PMEMfile *)fi->fh);

	fi->fh = 0;

	return 0;
}

static int
pmemfile_fuse_readdir(const char *path, void *buff, fuse_fill_dir_t fill,
		off_t off, struct fuse_file_info *fi)
{
	log("%s\n", path);

	PMEMfilepool *pfp = PFP;
	PMEMfile *dir = (PMEMfile *)fi->fh;

	if (!dir)
		return -EBADF;

	if (pmemfile_lseek(pfp, dir, off, PMEMFILE_SEEK_SET) != off) {
		log("pmemfile_lseek failed: %d\n", errno);
		return -errno;
	}

	char dirp[32758];
	struct stat statbuf;
	while (1) {
		int r = pmemfile_getdents64(pfp, dir,
					    (struct linux_dirent64 *)dirp,
					    sizeof(dirp));
		if (r < 0)
			return -errno;
		if (r == 0)
			break;

		for (unsigned i = 0; i < (unsigned)r; ) {
			i += 8;

			unsigned long nextoff = *(unsigned long *)&dirp[i];
			i += 8;

			unsigned short reclen = *(unsigned short *)&dirp[i];
			i += 2;

			i += 1;

			int ret = pmemfile_fstatat(pfp, dir, dirp + i, &statbuf,
					PMEMFILE_AT_SYMLINK_NOFOLLOW);
			if (ret)
				return -errno;

			fill(buff, dirp + i, &statbuf, (off_t)nextoff);

			i += reclen;
			i -= 8 + 8 + 2 + 1;
		}
	}


	return 0;
}

static int
pmemfile_fuse_mkdir(const char *path, mode_t mode)
{
	log("%s\n", path);

	int ret;
	PMEMfilepool *pfp = PFP;

	if ((ret = update_ctx(pfp)) < 0)
		return ret;

	if (pmemfile_mkdir(pfp, path, mode) < 0) {
		log("pmemfile_mkdir %s failed: %d\n", path, errno);
		return -errno;
	}

	return 0;
}

static int
pmemfile_fuse_rmdir(const char *path)
{
	log("%s\n", path);

	int ret;
	PMEMfilepool *pfp = PFP;

	if ((ret = update_ctx(pfp)) < 0)
		return ret;

	if (pmemfile_rmdir(pfp, path) < 0) {
		log("pmemfile_rmdir %s failed: %d\n", path, errno);
		return -errno;
	}

	return 0;
}

static int
pmemfile_fuse_chmod(const char *path, mode_t mode)
{
	log("%s\n", path);
	int ret;
	PMEMfilepool *pfp = PFP;

	if ((ret = update_ctx(pfp)) < 0)
		return ret;

	if (pmemfile_chmod(pfp, path, mode) < 0) {
		log("pmemfile_chmod %s failed: %d\n", path, errno);
		return -errno;
	}

	return 0;
}

static int
pmemfile_fuse_chown(const char *path, uid_t uid, gid_t gid)
{
	log("%s\n", path);

	int ret;
	PMEMfilepool *pfp = PFP;

	if ((ret = update_ctx(pfp)) < 0)
		return ret;

	if (pmemfile_chown(pfp, path, uid, gid) < 0) {
		log("pmemfile_chown %s failed: %d\n", path, errno);
		return -errno;
	}

	return 0;
}

static PMEMfile *
pmemfile_open_wrapper(PMEMfilepool *pfp, const char *path, int flags,
		pmemfile_mode_t mode)
{
	/*
	 * &~0x8000, because fuse passes flag pmemfile doesn't understand
	 * (O_LARGEFILE, which userspace on x86_64 defines as 0)
	 */
	return pmemfile_open(pfp, path, flags & ~0x8000, mode);
}

static int
pmemfile_fuse_create(const char *path, mode_t mode, struct fuse_file_info *fi)
{
	log("%s\n", path);

	PMEMfilepool *pfp = PFP;
	int ret;

	if ((ret = update_ctx(pfp)) < 0)
		return ret;

	PMEMfile *f = pmemfile_open_wrapper(pfp, path, fi->flags, mode);
	if (!f) {
		log("pmemfile_open %s failed: %d\n", path, errno);
		return -errno;
	}
	fi->fh = (uintptr_t)f;

	return 0;
}

static int
pmemfile_fuse_utimens(const char *path, const struct timespec tv[2])
{
	log("%s\n", path);

	int ret;
	PMEMfilepool *pfp = PFP;

	if ((ret = update_ctx(pfp)) < 0)
		return ret;

	if (pmemfile_utimensat(pfp, NULL, path, tv, 0) < 0) {
		log("pmemfile_utimens %s failed: %d\n", path, errno);
		return -errno;
	}

	return 0;
}

static int
pmemfile_fuse_open(const char *path, struct fuse_file_info *fi)
{
	log("%s 0%o\n", path, fi->flags);

	int ret;
	PMEMfilepool *pfp = PFP;

	if ((ret = update_ctx(pfp)) < 0)
		return ret;

	PMEMfile *f = pmemfile_open_wrapper(pfp, path, fi->flags, 0);
	if (f == NULL) {
		log("pmemfile_open %s failed: %d\n", path, errno);
		return -errno;
	}
	fi->fh = (uintptr_t)f;

	return 0;
}

static int
pmemfile_fuse_release(const char *path, struct fuse_file_info *fi)
{
	log("%s\n", path);

	if (!fi->fh)
		return -EBADF;

	pmemfile_close(PFP, (PMEMfile *)fi->fh);

	fi->fh = 0;

	return 0;
}

static int
pmemfile_fuse_write(const char *path, const char *buff, size_t size,
		off_t off, struct fuse_file_info *fi)
{
	log("%s\n", path);
	PMEMfile *f = (PMEMfile *)fi->fh;
	if (!f)
		return -EBADF;

	if (size > INT32_MAX)
		size = INT32_MAX;

	int ret = (int)pmemfile_pwrite(PFP, f, buff, size, off);
	if (ret < 0) {
		log("pmemfile_pwrite %zd failed: %d\n", size, errno);
		return -errno;
	}

	return ret;
}

static int
pmemfile_fuse_read(const char *path, char *buff, size_t size, off_t off,
		struct fuse_file_info *fi)
{
	log("%s\n", path);

	PMEMfile *f = (PMEMfile *)fi->fh;
	if (!f)
		return -EBADF;

	if (size > INT32_MAX)
		size = INT32_MAX;

	int ret = (int)pmemfile_pread(PFP, f, buff, size, off);
	if (ret < 0) {
		log("pmemfile_pread %zd failed: %d\n", size, errno);
		return -errno;
	}

	return ret;
}

static int
pmemfile_fuse_truncate(const char *path, off_t off)
{
	log("%s\n", path);
	int ret;
	PMEMfilepool *pfp = PFP;

	if ((ret = update_ctx(pfp)) < 0)
		return ret;

	if (pmemfile_truncate(pfp, path, off) < 0) {
		log("pmemfile_truncate %s failed: %d\n", path, errno);
		return -errno;
	}

	return 0;
}

static int
pmemfile_fuse_ftruncate(const char *path, off_t off, struct fuse_file_info *fi)
{
	log("%s\n", path);

	if (!fi->fh)
		return -EBADF;

	int ret;
	PMEMfilepool *pfp = PFP;

	if ((ret = update_ctx(pfp)) < 0)
		return ret;

	if (pmemfile_ftruncate(pfp, (PMEMfile *)fi->fh, off) < 0) {
		log("pmemfile_ftruncate %s failed: %d\n", path, errno);
		return -errno;
	}

	return 0;
}

static int
pmemfile_fuse_unlink(const char *path)
{
	log("%s\n", path);

	int ret;
	PMEMfilepool *pfp = PFP;

	if ((ret = update_ctx(pfp)) < 0)
		return ret;

	if (pmemfile_unlink(pfp, path) < 0) {
		log("pmemfile_unlink %s failed: %d\n", path, errno);
		return -errno;
	}

	return 0;
}

static int
pmemfile_fuse_link(const char *path1, const char *path2)
{
	log("%s %s\n", path1, path2);

	int ret;
	PMEMfilepool *pfp = PFP;

	if ((ret = update_ctx(pfp)) < 0)
		return ret;

	if (pmemfile_link(pfp, path1, path2) < 0) {
		log("pmemfile_link %s %s failed: %d\n", path1, path2, errno);
		return -errno;
	}

	return 0;
}

static int
pmemfile_fuse_flush(const char *path, struct fuse_file_info *fi)
{
	log("%s\n", path);
	return 0;
}

static int
pmemfile_fuse_ioctl(const char *path, int cmd, void *arg,
		struct fuse_file_info *fi, unsigned flags, void *data)
{
	log("%s\n", path);
	return -ENOTSUP;
}

static int
pmemfile_fuse_rename(const char *path, const char *dest)
{
	log("%s %s\n", path, dest);

	int ret;
	PMEMfilepool *pfp = PFP;

	if ((ret = update_ctx(pfp)) < 0)
		return ret;

	if (pmemfile_rename(pfp, path, dest) < 0) {
		log("pmemfile_rename %s %s failed: %d\n", path, dest, errno);
		return -errno;
	}

	return 0;
}

static int
pmemfile_fuse_symlink(const char *path, const char *link)
{
	log("%s %s\n", path, link);

	int ret;
	PMEMfilepool *pfp = PFP;

	if ((ret = update_ctx(pfp)) < 0)
		return ret;

	if (pmemfile_symlink(pfp, path, link) < 0) {
		log("pmemfile_symlink %s %s failed: %d\n", path, link, errno);
		return -errno;
	}

	return 0;
}

static int
pmemfile_fuse_readlink(const char *path, char *buff, size_t size)
{
	log("%s\n", path);

	int ret;
	PMEMfilepool *pfp = PFP;

	if ((ret = update_ctx(pfp)) < 0)
		return ret;

	ret = (int)pmemfile_readlink(pfp, path, buff, size - 1);
	if (ret < 0) {
		log("pmemfile_readlink %s failed: %d\n", path, errno);
		return -errno;
	}
	buff[ret] = 0;

	return 0;
}

static int
pmemfile_fuse_mknod(const char *path, mode_t mode, dev_t dev)
{
	log("%s\n", path);
	int ret;
	PMEMfilepool *pfp = PFP;

	if ((ret = update_ctx(pfp)) < 0)
		return ret;

	if (pmemfile_mknodat(pfp, NULL, path, mode, dev) < 0) {
		log("pmemfile_mknod %s failed: %d\n", path, errno);
		return -errno;
	}

	return 0;
}

static int
pmemfile_fuse_fallocate(const char *path, int mode, off_t offset, off_t size,
		struct fuse_file_info *fi)
{
	log("%s\n", path);

	PMEMfile *f = (PMEMfile *)fi->fh;
	if (!f)
		return -EBADF;

	int ret;
	PMEMfilepool *pfp = PFP;

	if ((ret = update_ctx(pfp)) < 0)
		return ret;

	if (pmemfile_fallocate(pfp, f, mode, offset, size) < 0) {
		log("pmemfile_fallocate %s failed: %d\n", path, errno);
		return -errno;
	}

	return 0;
}

static void *
pmemfile_fuse_init(struct fuse_conn_info *conn)
{
	log("\n");

	return PFP;
}

static int
pmemfile_fuse_statvfs(const char *path, struct statvfs *vfs)
{
	memset(vfs, 0, sizeof(*vfs));
	vfs->f_bsize = 4096;
	vfs->f_namemax = 255;

	return 0;
}

static struct fuse_operations pmemfile_ops = {
	.init		= pmemfile_fuse_init,
	.statfs		= pmemfile_fuse_statvfs,
	.getattr	= pmemfile_fuse_getattr,
	.chmod		= pmemfile_fuse_chmod,
	.chown		= pmemfile_fuse_chown,
	.utimens	= pmemfile_fuse_utimens,
	.ioctl		= pmemfile_fuse_ioctl,
	.opendir	= pmemfile_fuse_opendir,
	.releasedir	= pmemfile_fuse_releasedir,
	.readdir	= pmemfile_fuse_readdir,
	.mkdir		= pmemfile_fuse_mkdir,
	.rmdir		= pmemfile_fuse_rmdir,
	.rename		= pmemfile_fuse_rename,
	.mknod		= pmemfile_fuse_mknod,
	.symlink	= pmemfile_fuse_symlink,
	.create		= pmemfile_fuse_create,
	.unlink		= pmemfile_fuse_unlink,
	.link		= pmemfile_fuse_link,
	.open		= pmemfile_fuse_open,
	.release	= pmemfile_fuse_release,
	.write		= pmemfile_fuse_write,
	.read		= pmemfile_fuse_read,
	.flush		= pmemfile_fuse_flush,
	.truncate	= pmemfile_fuse_truncate,
	.ftruncate	= pmemfile_fuse_ftruncate,
	.fallocate	= pmemfile_fuse_fallocate,
	.readlink	= pmemfile_fuse_readlink,

	.flag_nopath = 1,
};

static void
print_usage(FILE *stream, const char *progname)
{
	fprintf(stream, "Usage: %s [-o|-b|-h] POOL DIR\n", progname);
}

int
main(int argc, char *argv[])
{
	int opt;
	bool allow_other = false;
	bool foreground = true;

	while ((opt = getopt(argc, argv, "obh")) >= 0) {
		switch (opt) {
		case 'o':
		case 'O':
			allow_other = true;
			break;
		case 'b':
		case 'B':
			foreground = false;
			break;
		case 'h':
		case 'H':
			print_usage(stdout, argv[0]);
			return 0;
		default:
			print_usage(stderr, argv[0]);
			return 2;
		}
	}

	if (optind + 2 != argc) {
		print_usage(stderr, argv[0]);
		return 2;
	}

	char *poolpath = argv[optind++];
	char *mountpoint = argv[optind++];

	PMEMfilepool *pool = pmemfile_pool_open(poolpath);
	if (!pool)
		err(2, "can't open pool '%s'", poolpath);

	char resolved_path[PATH_MAX];
	if (realpath(poolpath, resolved_path) == NULL)
		err(3, "realpath");

	char *fsname = NULL;
	if (asprintf(&fsname, "fsname=pmemfile:%s", resolved_path) < 0)
		err(4, "asprintf");

	char *fuse_args[9];

	int idx = 0;
	fuse_args[idx++] = argv[0];
	fuse_args[idx++] = "-o";
	fuse_args[idx++] = fsname;
	fuse_args[idx++] = "-o";
	fuse_args[idx++] = "subtype=pmemfile";

	if (allow_other) {
		fuse_args[idx++] = "-o";
		fuse_args[idx++] = "allow_other";
	}

	if (foreground)
		fuse_args[idx++] = "-f";

	fuse_args[idx++] = mountpoint;

	assert((unsigned)idx <= (sizeof(fuse_args) / sizeof(fuse_args[0])));

	return fuse_main(idx, fuse_args, &pmemfile_ops, pool);
}
