#include <dirent.h>
#include <dlfcn.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <alloca.h>

#include "plugin.h"
#include "thumbnail.h"

#include <glib.h>
#include <magic.h>

#define DEFAULT_PLUGIN_DIR "/usr/lib/meego-ux-mediafs"

/* #define TRY_ALL_PLUGINS */



struct indexer_plugin {
	char *name;
	void *lib;
	const char **mime;
	const char **suffix;

	struct plugin plugin;
};


struct indexer {
	char *plugin_dir;

	struct indexer_plugin **plugins;
	int count;
	int size;

	struct thumbnailer *thumbconf;

	magic_t magic;
};



static int
init_plugin_real(struct indexer_plugin *indexer_plugin, const char *self)
{
	struct plugin *plugin = &indexer_plugin->plugin;
	const char **(*get_mimetypes)(struct plugin_context *ctx);
	const char **(*get_suffixes)(struct plugin_context *ctx);

	plugin->init = dlsym(indexer_plugin->lib, "init");
	if (! plugin->init) {
		fprintf(stderr, "plugin has no init()!\n");
		return 1;
	}

	plugin->uninit = dlsym(indexer_plugin->lib, "uninit");
	if (! plugin->uninit) {
		fprintf(stderr, "plugin has no uninit()\n");
		return 1;
	}

	plugin->ctx = plugin->init(self);
	if (! plugin->ctx) {
		fprintf(stderr, "plugin init() failed\n");
		return 1;
	}

	plugin->get_image = dlsym(indexer_plugin->lib, "get_image");
	if (! plugin->get_image) {
		fprintf(stderr, "pluing has no get_image()!\n");
		plugin->uninit(plugin->ctx);
		return 1;
	}

	get_mimetypes = dlsym(indexer_plugin->lib, "get_mimetypes");
	if (get_mimetypes) {
		indexer_plugin->mime = get_mimetypes(plugin->ctx);
	} else {
		indexer_plugin->mime = NULL;
	}
	get_suffixes = dlsym(indexer_plugin->lib, "get_suffixes");
	if (get_suffixes) {
		indexer_plugin->suffix = get_suffixes(plugin->ctx);
	} else {
		indexer_plugin->suffix = NULL;
	}

	return 0;
}



static struct indexer_plugin *
init_plugin(const char *fn, const char *self)
{
	struct indexer_plugin plugin;

	plugin.lib = dlopen(fn, RTLD_NOW);
	if (! plugin.lib) {
		fprintf(stderr, "dlopening %s failed: %s\n", fn, dlerror());
		return NULL;
	}

	if (! init_plugin_real(&plugin, self)) {
		struct indexer_plugin *new;
		new = malloc(sizeof(struct indexer_plugin));
		if (new != NULL) {
			char *libname = strrchr(fn, (int) '/');
			if (libname[1] == '\0') {
				plugin.name = strdup(libname);
			} else {
				plugin.name = strdup(libname + 1);
			}
			if (plugin.name != NULL) {
				memcpy(new, &plugin,
						sizeof(struct indexer_plugin));
				return new;
			}
			free(new);
		} else {
			fprintf(stderr, "out of memory!\n");
		}
	}

	fprintf(stderr, "failed to initialise %s\n", fn);
	dlclose(plugin.lib);
	return NULL;
}



static void
free_plugin(struct indexer_plugin *indexer_plugin)
{
	indexer_plugin->plugin.uninit(indexer_plugin->plugin.ctx);
	dlclose(indexer_plugin->lib);
	free(indexer_plugin->name);
	free(indexer_plugin);
}



static int
open_plugins(struct indexer *indexer, const char *self)
{
	char dirname[MAXNAMLEN];
	char fn[MAXNAMLEN + 3];
	DIR *dir;
	struct dirent *ent;

	snprintf(dirname, MAXNAMLEN, "%s/readers", indexer->plugin_dir);

	dir = opendir(dirname);
	if (! dir) {
		fprintf(stderr, "%s: cannot read\n", dirname);
		return 1;
	}

	while (1) {
		ent = readdir(dir);
		if (! ent) {
			break;
		}

		if (ent->d_type != DT_REG) {
			continue;
		}
		{
			size_t len = strlen(ent->d_name);
			if (len < 3 || strcmp(ent->d_name + len - 3, ".so")) {
				continue;
			}
		}

		if (indexer->count == indexer->size) {
			void *newptr;

			indexer->size += 4;
			newptr = realloc(indexer->plugins, indexer->size *
					sizeof(struct indexer_plugin *));
			if (! newptr) {
				fprintf(stderr, "realloc failed!\n");
				continue;
			}
			indexer->plugins = newptr;
		}

		snprintf(fn, MAXNAMLEN + 3, "%s/%s", dirname, ent->d_name);
		indexer->plugins[indexer->count] = init_plugin(fn, self);
		if (indexer->plugins[indexer->count]) {
			fprintf(stdout, "initialised %s\n", fn);
			indexer->count++;
		} else {
			fprintf(stderr, "failed to init %s\n", ent->d_name);
		}
	}
	closedir(dir);

	return indexer->count;
}



static void
free_plugins(struct indexer *indexer)
{
	int i;

	for (i = 0; i < indexer->count; i++) {
		free_plugin(indexer->plugins[i]);
	}
	free(indexer->plugins);
	indexer->plugins = NULL;
	indexer->count = indexer->size = 0;
}



struct indexer *
indexer_init(const char *self, const char *plugin_dir,
		const char *thumb_dir, const char *conffile)
{
	struct indexer *indexer;
	struct thumbnailer *thumbconf;

	/* init threads as plugins may need that */
	if (! g_thread_supported()) {
		g_thread_init(NULL);
	}

	thumbconf = thumbnail_init(self, thumb_dir,
			conffile ? : getenv("INDEXER_RC"));
	if (! thumbconf) {
		return NULL;
	}

	indexer = malloc(sizeof(struct indexer));
	if (indexer == NULL) {
		return NULL;
	}
	indexer->thumbconf = thumbconf;

	/* save plugin_dir to allow rereading upon SIGHUP */
	if (plugin_dir) {
		indexer->plugin_dir = strdup(plugin_dir);
	} else {
		indexer->plugin_dir = strdup(DEFAULT_PLUGIN_DIR);
	}
	if (! indexer->plugin_dir) {
		free(indexer);
		return NULL;
	}

	indexer->plugins = NULL;
	indexer->count = indexer->size = 0;

	open_plugins(indexer, self);

	indexer->magic = magic_open(MAGIC_SYMLINK | MAGIC_MIME |
			MAGIC_CONTINUE | MAGIC_PRESERVE_ATIME);
	if (indexer->magic == NULL) {
		fprintf(stderr, "failed to open libmagic\n");
	} else {
		if (magic_load(indexer->magic, NULL)) {
			fprintf(stderr, "failed to load magic: %s\n",
					magic_error(indexer->magic));
			magic_close(indexer->magic);
			indexer->magic = NULL;
		}
	}

	return indexer;
}



void
indexer_free(struct indexer *indexer)
{
	thumbnail_uninit(indexer->thumbconf);

	if (indexer == NULL) {
		return;
	}

	if (indexer->magic) {
		magic_close(indexer->magic);
	}
	free(indexer->plugin_dir);
	free_plugins(indexer);
	free(indexer);
}



static int
match_mime(const char *pattern, const char *mime)
{
	int patlen = strlen(pattern);

#if 0
	fprintf(stderr, "match_mime: pattern=\"%s\" mime=\"%s\"\n",
			pattern, mime);
#endif
	if (pattern[patlen - 1] == '*') {
		if (strlen(mime) >= patlen - 1 &&
				strncasecmp(mime, pattern, patlen - 1) == 0) {
			return 1;
		}
	} else if (pattern[0] == '*') {
		int mimelen = strlen(mime);
		if (mimelen >= patlen - 1) {
			if (strcasecmp(mime + mimelen - patlen + 1,
						pattern + 1)) {
				return 1;
			}
		}
	} else {
		if (strcasecmp(mime, pattern) == 0) {
			return 1;
		}
	}
	return 0;
}



static const StorageType
get_pixel_storage_type(enum plugin_reply_pixel_type type, int other)
{
	switch (type) {
		case PLUGIN_REPLY_CHAR_PIXEL:
			return CharPixel;
		case PLUGIN_REPLY_DOUBLE_PIXEL:
			return DoublePixel;
		case PLUGIN_REPLY_FLOAT_PIXEL:
			return FloatPixel;
		case PLUGIN_REPLY_OTHER_PIXEL:
			return other;

		default:
			fprintf(stderr, "thumbnail.c: "
					"unsupported pixel type: %d\n",
					(int) type);
			/* fall through */
		case PLUGIN_REPLY_UNDEF_PIXEL:
			return UndefinedPixel;
	}
}



static int
create_thumbnails(const struct indexer *indexer, struct plugin_reply *reply,
		const char *fn)
{
	switch (reply->type) {
		case PLUGIN_REPLY_TYPE_IMAGE:
			return thumbnail_make_all_from_image(indexer->thumbconf,
					(Image *) reply->data, fn);
			break;
		case PLUGIN_REPLY_TYPE_IMAGE_FILE_DATA:
			return thumbnail_make_all_from_data(indexer->thumbconf,
					reply->data, reply->data_len, fn);
			break;
		case PLUGIN_REPLY_TYPE_RAW_PIXELS:
			return thumbnail_make_all_from_raw(indexer->thumbconf,
					reply->data,
					reply->width, reply->height,
					reply->pixel_format,
					get_pixel_storage_type(
						reply->pixel_type,
						reply->pixel_type_other),
					fn);
			break;

		default:
			fprintf(stderr, "cannot create thumbnails from "
					"unrecognised reply: %d\n",
					reply->type);
			return 1;
	}
}



static int
try_index_mime(struct indexer *indexer, int *plugins, const char *fn,
		struct plugin_reply *reply)
{
	const char *mime_raw;
#define MIME_LEN 64
	char mime[MIME_LEN + 1];
	char *ptr;
	const char **s;
	int i;

	mime_raw = magic_file(indexer->magic, fn);
	if (! mime_raw) {
		return 0;
	}
	if (strlen(mime_raw) > MIME_LEN) {
		return 0;
	}
	strncpy(mime, mime_raw, MIME_LEN);
	mime[MIME_LEN] = '\0';

	/* strip extra attributes */
	ptr = strchr(mime, ' ');
	if (ptr) {
		*ptr = '\0';
	}
	ptr = strchr(mime, ';');
	if (ptr) {
		*ptr = '\0';
	}

	for (i = 0; i < indexer->count; i++) {
		struct plugin *plugin = &indexer->plugins[i]->plugin;
		if (plugins[i]) {
			continue;
		}

		for (s = indexer->plugins[i]->mime; *s != NULL; s++) {
			if (! match_mime(*s, mime)) {
				continue;
			}
			plugins[i] = 1;
			fprintf(stdout, "trying %s (mime type %s matches %s)\n",
					indexer->plugins[i]->name, mime, *s);

			if (! plugin->get_image(plugin->ctx, fn, 1024, 1024,
						reply)) {
				fprintf(stdout, "processed with %s\n",
						indexer->plugins[i]->name);
				return 1;
			}
		}
	}
	fprintf(stdout, "no parser found for mime type %s\n", mime);

	return 0;
}



static int
try_index_suffix(struct indexer *indexer, int *plugins, const char *fn,
		struct plugin_reply *reply)
{
	const char *t, *suffix;
	int i;

	t = strrchr(fn, (int) '/');
	if (t == fn || t[1] == '\0') {
		return 0;
	}
	suffix = strrchr(fn, (int) '.');
	if (suffix == NULL || suffix < t) {
		return 0;
	}
	suffix++;

	for (i = 0; i < indexer->count; i++) {
		struct indexer_plugin *plugin = indexer->plugins[i];
		const char **s;

		if (plugins[i] || plugin->suffix == NULL) {
			continue;
		}

		for (s = plugin->suffix; *s != NULL; s++) {
			if (strcasecmp(*s, suffix) != 0) {
				continue;
			}

			plugins[i] = 1;
			fprintf(stdout, "trying %s (suffix %s matches %s)\n",
					indexer->plugins[i]->name, suffix, *s);
			if (! plugin->plugin.get_image(plugin->plugin.ctx,
						fn, 1024, 1024, reply)) {
				fprintf(stdout, "processed with %s\n",
						indexer->plugins[i]->name);
				return 1;
			}
		}
	}
	fprintf(stdout, "no parser found for file suffix %s\n", suffix);

	return 0;
}



#ifdef TRY_ALL_PLUGINS
static int
try_index_all_plugins(struct indexer *indexer, int *plugins, const char *fn,
		struct plugin_reply *reply)
{
	int i;

	for (i = 0; i < indexer->count; i++) {
		struct plugin *plugin = &indexer->plugins[i]->plugin;
		if (plugins[i]) {
			continue;
		}
		plugins[i] = 1;
		fprintf(stdout, "trying %s\n", indexer->plugins[i]->name);
		if (! plugin->get_image(plugin->ctx, fn, 1024, 1024, reply)) {
			fprintf(stdout, "processed with %s\n",
					indexer->plugins[i]->name);
			return 1;
		}
	}

	return 0;
}
#endif



int
indexer_process(struct indexer *indexer, const char *src, const char *dest)
{
	struct plugin_reply reply;
	int *tried;
	int ok = 0;

	fprintf(stdout, "processing %s (%s)\n", dest, src);

	tried = alloca(indexer->count * sizeof(int));
	memset(tried, 0, indexer->count * sizeof(int));

	reply.free = NULL;

	if (indexer->magic) {
		ok = try_index_mime(indexer, tried, src, &reply);
	}
	if (! ok) {
		ok = try_index_suffix(indexer, tried, src, &reply);
	}
#ifdef TRY_ALL_PLUGINS
	if (! ok) {
		ok = try_index_all_plugins(indexer, tried, src, &reply);
	}
#endif

	if (ok) {
		int ret = create_thumbnails(indexer, &reply, dest);
		if (reply.free) {
			reply.free(&reply);
		}
		if (ret == 0) {
			return 0;
		}
	}

	thumbnail_delete_all(indexer->thumbconf, dest);
	return 1;
}



int
indexer_rename(struct indexer *indexer, const char *old_path,
		const char *new_path)
{
	return thumbnail_rename_all(indexer->thumbconf, old_path, new_path);
}



int
indexer_remove(struct indexer *indexer, const char *path)
{
	return thumbnail_delete_all(indexer->thumbconf, path);
}
