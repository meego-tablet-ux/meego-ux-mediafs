#define FUSE_USE_VERSION 26
#define _XOPEN_SOURCE 500
#define FILENAME_MAX 512

#include "mfuse.h"

#include <fuse.h>
#include <string.h>
#include <errno.h>
#include <dirent.h>
#include <unistd.h>
#include <stdio.h>
#include <stdlib.h>

static struct mfuse_callbacks mfuse_cb;
static uint64_t write_fh;
static char monitor_dir[FILENAME_MAX];
static char source_dir[FILENAME_MAX];
static int source_dir_len;

char * monitored_path(const char *sourced_path)
{
	char *monitored = (char *)calloc(FILENAME_MAX, sizeof(char));
	if (strncmp(sourced_path, source_dir, source_dir_len) != 0) {
		fprintf(stderr, "%s: path not matching prefix %s!\n",
				sourced_path, source_dir);
	}
	snprintf(monitored, FILENAME_MAX, "%s/%s", monitor_dir, sourced_path + source_dir_len);
	return monitored;
}

static int mfuse_getattr(const char *path, struct stat *stat_buf)
{
	int res = lstat(path, stat_buf);
	if (res < 0)
		return -errno;

	return 0;
}

static int mfuse_readdir(const char *path, void *buf,
						fuse_fill_dir_t filler, off_t offset,
						struct fuse_file_info *fi)
{
	(void) offset;
	(void) fi;

	DIR *dp = opendir(path);
	if (dp == NULL)
		return -errno;

	struct dirent *de;
	while ((de = readdir(dp)) != NULL) {
		struct stat st;
		memset(&st, 0, sizeof(st));
		st.st_ino = de->d_ino;
		st.st_mode = de->d_type << 12;
		if (filler(buf, de->d_name, &st, 0))
			break;
	}

	closedir(dp);
	return 0;
}

static int mfuse_open(const char *path, struct fuse_file_info *fi)
{
	int fd = open(path, fi->flags);
	if (fd < 0)
		return -errno;
	fi->fh = fd;

	return 0;
}

static int mfuse_read(const char *path, char *buf, size_t size,
					off_t offset, struct fuse_file_info *fi)
{
	int res = pread(fi->fh, buf, size, offset);
	if (res < 0)
		res = -errno;

	return res;
}

static int mfuse_create(const char *path, mode_t mode,
					struct fuse_file_info *fi)
{
	int fd = creat(path, mode);
	if (fd < 0)
		return -errno;
	fi->fh = fd;
	printf("create: %s\n", path);

	return 0;
}

static int mfuse_mkdir(const char *path, mode_t mode)
{
	int res = mkdir(path, mode);
	if (res < 0)
		return -errno;

	return 0;
}

static int mfuse_rm(const char *path)
{
	int res = unlink(path);

	if (res == 0 && mfuse_cb.removed) {
		char *mon = monitored_path(path);
		mfuse_cb.removed(mon, NULL);
		free(mon);
	}

	if (res < 0)
		return -errno;

	return 0;
}

static int mfuse_rmdir(const char *path)
{
	int res = rmdir(path);
	if (res < 0)
		return -errno;

	return 0;
}

static int mfuse_utime(const char *path, struct utimbuf *ubuf)
{
	int res = utime(path, ubuf);
	if (res < 0)
		res = -errno;

	return res;
}

static int mfuse_write(const char *path, const char *buf,
					size_t size, off_t offset,
					struct fuse_file_info *fi)
{
	int res = pwrite(fi->fh, buf, size, offset);
	if (res < 0)
		res = -errno;

	if (write_fh == -1)
		write_fh = fi->fh;
	else if (write_fh != fi->fh)
		fprintf(stderr, "mfuse_write() : re-assigning write_fh\n");

	return res;
}

static int mfuse_rename(const char *old_path, const char *new_path)
{
	int res = rename(old_path, new_path);

	if (res == 0 && mfuse_cb.renamed) {
			char *mon_old = monitored_path(old_path);
			char *mon_new = monitored_path(new_path);
			mfuse_cb.renamed(mon_old, new_path, mon_new, NULL);
			free(mon_old);
			free(mon_new);
	}

	if (res < 0)
		return -errno;

	return 0;
}

static int mfuse_link(const char *path, const char *new_path)
{
	int res = link(path, new_path);
	if (res < 0)
		return -errno;

	return 0;
}

static int mfuse_symlink(const char *path, const char *new_path)
{
	int res = symlink(path, new_path);
	if (res < 0)
		return -errno;

	return 0;
}

static int mfuse_readlink(const char *path, char *buf, size_t size)
{
	int res = readlink(path, buf, size - 1);
	if (res < 0)
		return -errno;

	buf[res] = '\0';
	return 0;
}

static int mfuse_chown(const char *path, uid_t uid, gid_t gid)
{
	int res = lchown(path, uid, gid);
	if (res < 0)
		return -errno;

	return 0;
}

static int mfuse_chmod(const char *path, mode_t mode)
{
	int res = chmod(path, mode);
	if (res < 0)
		return -errno;

	return 0;
}

static int mfuse_access(const char *path, int mask)
{
	int res = access(path, mask);
	if (res < 0)
		return -errno;

	return 0;
}

static int mfuse_truncate(const char *path, off_t size)
{
	int res = truncate(path, size);
	if (res < 0)
		return -errno;

	return 0;
}

static int mfuse_statfs(const char *path, struct statvfs *stbuf)
{
	int res = statvfs(path, stbuf);
	if (res < 0)
		return -errno;

	return 0;
}

static int mfuse_fsync(const char *path, int is_datasync,
					struct fuse_file_info *fi)
{
	int res;
	if (is_datasync)
		res = fdatasync(fi->fh);
	else
		res = fsync(fi->fh);

	if (res < 0)
		return -errno;

	return 0;
}

static int mfuse_flush(const char *path, struct fuse_file_info *fi)
{
	if (write_fh != -1) {
		if (write_fh != fi->fh) {
			fprintf(stderr, "mfuse_flush() : wrong write_fh\n");
			return -1;
		} else {
			if (mfuse_cb.write_closed) {
				char *mon = monitored_path(path);
				mfuse_cb.write_closed(path, mon, NULL);
				free(mon);
			}
			write_fh = -1;
		}
	}

	return 0;
}

static struct fuse_operations mfuse_oper = {
	.getattr	= mfuse_getattr,
	.readdir	= mfuse_readdir,
	.open		= mfuse_open,
	.read		= mfuse_read,
	.create		= mfuse_create,
	.mkdir		= mfuse_mkdir,
	.unlink		= mfuse_rm,
	.rmdir		= mfuse_rmdir,
	.utime		= mfuse_utime,
	.write		= mfuse_write,
	.rename		= mfuse_rename,
	.link		= mfuse_link,
	.symlink	= mfuse_symlink,
	.readlink	= mfuse_readlink,
	.chown		= mfuse_chown,
	.chmod		= mfuse_chmod,
	.access		= mfuse_access,
	.truncate	= mfuse_truncate,
	.statfs		= mfuse_statfs,
	.fsync		= mfuse_fsync,
	.flush		= mfuse_flush,
};

static void
trim_path(char *path)
{
	char *ptr;
	for (ptr = path; *ptr != '\0'; ptr++) {
		if (ptr[0] == '/' && ptr[1] == '/') {
			memmove(ptr, ptr + 1, strlen(ptr)); /* includes terminator */
			ptr--; /* check same start position again */
		}
	}
	while (1) {
		ptr = strrchr(path, '/');
		if (ptr == NULL) {
			break;
		}
		if (ptr[1] == '\0') {
			*ptr = '\0';
		} else {
			break;
		}
	}
}

int mfuse_main(int argc, char *argv[], const char *source_path,
		const char *monitor_path, const struct mfuse_callbacks *mc,
		void *user_data)
{
	/* copy and trim source and monitor paths */
	strcpy(monitor_dir, monitor_path);
	trim_path(monitor_dir);

	strcpy(source_dir, source_path);
	trim_path(source_dir);
	if (strlen(source_dir) + 1 >= FILENAME_MAX) {
		fprintf(stderr, "source_dir is too long!\n");
		return 1;
	} else {
		strcat(source_dir, "/");
	}
	source_dir_len = strlen(source_dir);

	mfuse_cb = *mc;
	write_fh = -1;
	return fuse_main(argc, argv, &mfuse_oper, user_data);
}

/* vim: set ts=4 sw=4: */
