#ifndef M_FUSE_H
#define M_FUSE_H

struct mfuse_callbacks {
	int (*write_closed) (const char *src, const char *dest, void *user_data);
	int (*renamed) (const char *old_dest, const char *new_src,
			const char *new_dest, void *user_data);
	int (*removed) (const char *path, void *user_data);
};

int mfuse_main(int argc, char *argv[], const char *source_path,
		const char *monitor_path, const struct mfuse_callbacks *mc,
		void *user_data);

#endif

/* vim: set ts=4 sw=4: */
