#include "mfuse.h"
#include "indexer.h"

#include <stdio.h>
#include <unistd.h>
#include <fcntl.h>
#include <string.h>
#include <stdlib.h>
#include <getopt.h>
#include <string.h>
#include <assert.h>

static struct indexer *indexer;

char *help_text =	"Usage: %s -s <DIR> -m <DIR> -t <DIR> [OPTIONS]\n"
					"\n"
					"   -f, --foreground         run in foreground\n"
					"   -s, --source <DIR>       source directory path\n"
					"   -m, --monitor <DIR>      target directory path that will be monitored\n"
					"                              for changes and mirror content of source directory\n"
					"   -t, --thumb <DIR>        path to directory where thumbnails will be stored\n"
					"   -p, --plugin-dir <DIR>   path to plugin directory\n"
					"   -c, --config <FILE>      path to configuration file\n"
					"   -h, --help               print this message\n"
					"\n"
					"Examples:\n"
					"   %s -s /tmp/fuse-test/.photos-hidden -m /tmp/fuse-test/home/Photos\n"
					"                 -t /tmp/fuse-test/home/.thumbnails\n";

static struct option long_options[] = {
	{"foreground",	no_argument,		NULL, 'f'},
	{"source",		required_argument,	NULL, 's'},
	{"monitor",		required_argument,	NULL, 'm'},
	{"thumb",		required_argument,	NULL, 't'},
	{"config",		required_argument,	NULL, 'c'},
	{"plugin-dir",	required_argument,	NULL, 'p'},
	{"help",		no_argument,		NULL, 'h'},
	{NULL,			0,					NULL, 0}
};

static int index_file(const char *src, const char *dest, void *user_data)
{
	if (indexer_process(indexer, src, dest))
		fprintf(stderr, "indexing %s (%s) failed\n", dest, src);
	return 0;
}

static int on_renamed(const char *old_dest, const char *new_src,
		const char *new_dest, void *user_data)
{
	indexer_rename(indexer, old_dest, new_dest);
	return 0;
}

static int remove_thumbnail(const char *path, void *user_data)
{
	indexer_remove(indexer, path);
	return 0;
}

static struct mfuse_callbacks cb = {
	.write_closed = index_file,
	.renamed = on_renamed,
	.removed = remove_thumbnail
};

int main(int argc, char *argv[])
{
#define FUSE_ARGV_SIZE 16
	int fuse_argc = 0;
	char *fuse_argv[FUSE_ARGV_SIZE];
	int i;
	for (i = 0; i < FUSE_ARGV_SIZE; ++i)
		fuse_argv[i] = (char *)calloc(256, sizeof(char));
	strcpy(fuse_argv[++fuse_argc - 1], argv[0]);

	char *source_dir = NULL;
	char *monitor_dir = NULL;
	char *thumb_dir = NULL;
	char *plugin_dir = NULL;
	char *conf_file = NULL;

	int arg;
	while ((arg = getopt_long(argc, argv, "fs:m:t:p:c:h", long_options, NULL)) != -1) {
		switch (arg) {
		case 'f':
			strcpy(fuse_argv[++fuse_argc - 1], "-d");
			break;
		case 's':
			source_dir = strdup(optarg);
			assert(source_dir);
			break;
		case 'm':
			monitor_dir = strdup(optarg);
			assert(monitor_dir);
			break;
		case 't':
			thumb_dir = strdup(optarg);
			assert(thumb_dir);
			break;
		case 'p':
			plugin_dir = strdup(optarg);
			assert(plugin_dir);
			break;
		case 'c':
			conf_file = strdup(optarg);
			assert(conf_file);
			break;
		case 'h':
			printf(help_text, argv[0], argv[0]);
			return 0;
			break;
		default:
			break;
			return 1;
		}
	}

	if (!source_dir) {
		fprintf(stderr, "%s: -indexer: -s option is mandatory\n", argv[0]);
		return 1;
	}
	if (!monitor_dir) {
		fprintf(stderr, "%s: -m option is mandatory\n", argv[0]);
		return 1;
	}
	if (!thumb_dir) {
		fprintf(stderr, "%s: -t option is mandatory\n", argv[0]);
		return 1;
	}

	strcpy(fuse_argv[++fuse_argc - 1], monitor_dir);
	strcpy(fuse_argv[++fuse_argc - 1], "-o");
	strcpy(fuse_argv[++fuse_argc - 1], "modules=subdir");
	strcpy(fuse_argv[++fuse_argc - 1], "-o");
	strcpy(fuse_argv[++fuse_argc - 1], "subdir=");
	strcat(fuse_argv[fuse_argc - 1], source_dir);
	strcpy(fuse_argv[++fuse_argc - 1], "-o");
	strcpy(fuse_argv[++fuse_argc - 1], "allow_other");

	indexer = indexer_init(argv[0], plugin_dir, thumb_dir, conf_file);
	assert(indexer);

	int ret = mfuse_main(fuse_argc, fuse_argv, source_dir, monitor_dir,
			&cb, NULL);
	indexer_free(indexer);

	for (i = 0; i < FUSE_ARGV_SIZE; ++i)
		free(fuse_argv[i]);

	free(conf_file);
	free(plugin_dir);
	free(source_dir);
	free(monitor_dir);
	free(thumb_dir);

	return ret;
}

/* vim: set ts=4 sw=4: */
