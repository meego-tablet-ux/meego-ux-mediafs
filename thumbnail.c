#include "thumbnail.h"

#include <assert.h>
#include <errno.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <X11/Xlib.h>

#include <sys/stat.h>
#include <sys/types.h>

#include <glib.h>

#include <magick/api.h>


#define DIRNAME_NORMAL_PORTRAIT		"normal-portrait"
#define DIRNAME_NORMAL_LANDSCAPE	"normal-landscape"
#define DIRNAME_FULLSCREEN_PORTRAIT	"fullscreen-portrait"
#define DIRNAME_FULLSCREEN_LANDSCAPE	"fullscreen-landscape"

#define DEFAULT_CONFFILE "/etc/meego-ux-mediafs.conf"


enum {
	THUMB_MAIN = 0,
	THUMB_NORMAL_PORTRAIT,
	THUMB_NORMAL_LANDSCAPE,
	THUMB_FULLSCREEN_PORTRAIT,
	THUMB_FULLSCREEN_LANDSCAPE,
	THUMB_TYPE_N
};


enum {
	RESIZE_NONE,
	RESIZE_CROP_CENTRE,
};


struct config {
	char *name;
	int max_width_px;
	int max_height_px;
	double ratio;
	int resize;
};


struct thumbnailer {
	char *thumb_dir;
	struct config **config;
	int n;

	double hdpmm, vdpmm;
};



static inline int
min(int a, int b)
{
	return a < b ? a : b;
}



static int
read_config(struct thumbnailer *ctx, const char *conffile)
{
	int err = 0;

#define BUFSIZE 1024
	FILE *fptr;
	char buf[BUFSIZE + 1];
	size_t len;

	char *ptr;
	char *ptr_r;
	char *lf_ptr;
	char *lf_ptr_r;

	fptr = fopen(conffile, "r");
	if (! fptr) {
		fprintf(stderr, "%s: cannot read\n", conffile);
		return 1;
	}
	len = fread(buf, 1, BUFSIZE, fptr);
	if (len == BUFSIZE) {
		fprintf(stderr, "%s: file too long!\n", conffile);
		fclose(fptr);
		return 1;
	}
	buf[len] = '\0';

	for (lf_ptr = strtok_r(buf, "\n", &lf_ptr_r); lf_ptr;
			lf_ptr = strtok_r(NULL, "\n", &lf_ptr_r)) {
		struct config tn;
		void *new;

		ptr = strchr(lf_ptr, '#');
		if (ptr) {
			*ptr = '\0';
		}
#ifdef DEBUG
		printf("line: %s\n", lf_ptr);
#endif

		ptr = strtok_r(lf_ptr, " \t", &ptr_r);
		if (! ptr) {
			continue;
		}

		tn.name = ptr;
		tn.resize = RESIZE_NONE;
		tn.max_width_px = tn.max_height_px = 0;
		tn.ratio = -1.0;

		for (ptr = strtok_r(NULL, " \t", &ptr_r); ptr;
				ptr = strtok_r(NULL, " \t", &ptr_r)) {
			char *key, *val;

			key = ptr;
			val = strchr(ptr, '=');
			if (val) {
				*val = '\0';
				val++;
			} else {
				val = NULL;
			}
#ifdef DEBUG
			printf("key: %s (val: %s)\n", key, val ? : "none");
#endif
			if (! strcasecmp(key, "maxwidth_px") && val) {
				tn.max_width_px = atoi(val);
			} else if (! strcasecmp(key, "maxheight_px") && val) {
				tn.max_height_px = atoi(val);
			} else if (! strcasecmp(key, "crop")) {
				tn.resize = RESIZE_CROP_CENTRE;
			} else if (! strcasecmp(key, "ratio") && val) {
				tn.ratio = atof(val);
			} else {
				fprintf(stderr, "unrecognised config key: %s\n",
						key);
			}
		}

		if (! (tn.max_width_px || tn.max_height_px)) {
			continue;
		}
		new = realloc(ctx->config,
				(ctx->n + 1) * sizeof(struct config *));
		if (new) {
			ctx->config = new;
			ctx->config[ctx->n] = malloc(sizeof(struct config));
			/* TODO handle properly */
			assert(ctx->config[ctx->n]);
			tn.name = strdup(tn.name);
			assert(tn.name);
			memcpy(ctx->config[ctx->n], &tn, sizeof(struct config));
			ctx->n++;
#ifdef DEBUG
			printf("-- name=%s", tn.name);
			printf(" w=%d h=%d", tn.max_width_px, tn.max_height_px);
			if (tn.ratio >= 0.0) {
				printf(" r=%.2f", tn.ratio);
			}
			if (tn.resize != RESIZE_NONE) {
				printf(" rs=%d", tn.resize);
			}
			printf("\n");
#endif

		} else {
			fprintf(stderr, "out of memory!\n");
			err = 1;
		}
	}
	fclose(fptr);

	return err;
}



static void
free_config(struct thumbnailer *ctx)
{
	int i;
	free(ctx->thumb_dir);
	for (i = 0; i < ctx->n; i++) {
		free(ctx->config[i]->name);
		free(ctx->config[i]);
	}
	free(ctx->config);
	free(ctx);
}



static struct thumbnailer *
make_config(const char *thumb_dir, const char *conffile)
{
	struct thumbnailer *config;

	config = malloc(sizeof(struct thumbnailer));
	if (! config) {
		return NULL;
	}
	config->thumb_dir = strdup(thumb_dir);
	if (! config->thumb_dir) {
		free_config(config);
		return NULL;
	}

	config->n = 0;
	config->config = NULL;

	if (read_config(config, conffile ? : DEFAULT_CONFFILE)) {
		free_config(config);
		return NULL;
	}

	return config;
}



static void
make_hash(char *hash, const char *fn)
{
	GChecksum *checksum;
	const char *digest;

	checksum = g_checksum_new(G_CHECKSUM_MD5);
	g_checksum_update(checksum, (const guchar *) "file://", 7);
	g_checksum_update(checksum, (const guchar *) fn, strlen(fn));
	digest = g_checksum_get_string(checksum);
	memcpy(hash, digest, 32);
	hash[32] = '\0';
	g_checksum_free(checksum);
}



static int
get_thumbnail_dir(char *dir, size_t dir_len, const char *base_dir,
		const char *type)
{
	int w;
	int err = 0;

	w = snprintf(dir, dir_len, "%s/%s%s",
			base_dir,
			type ? "/" : "",
			type ? : "");

	return (w < dir_len && !err) ? 0 : 1;
}



static int
build_filename(char *fn, size_t len, const char *thumb_dir, const char *hash,
		const char *type)
{
	char dir[len];
	int w;
	if (get_thumbnail_dir(dir, len - 16 - 4 - 1, thumb_dir, type)) {
		return 1;
	}
	/*
	 * get_thumbnail_dir should fail is len is not enough. Still, there's
	 * no harm in additional checks.
	 */
	w = snprintf(fn, len, "%s/%s.png", dir, hash);
	return (w < len) ? 0 : 1;
}



static int
get_dpmm(const char *display_name, int screen_num, double *hdpmm, double *vdpmm)
{
	Display *dsp;
	int width, height, widthmm, heightmm;

	dsp = XOpenDisplay(display_name);
	if (dsp == NULL) {
		return 1;
	}

	width = DisplayWidth(dsp, screen_num);
	height = DisplayHeight(dsp, screen_num);
	widthmm = DisplayWidthMM(dsp, screen_num);
	heightmm = DisplayHeightMM(dsp, screen_num);

	XCloseDisplay(dsp);

	*hdpmm = (double) width / widthmm;
	*vdpmm = (double) height / heightmm;
#ifdef DEBUG
	printf("screen %dx%d px, %dx%d DPI = %.2fx%.2f DPmm\n",
			width, height, widthmm, heightmm,
			*hdpmm, *vdpmm);
#endif

	return 0;
}



void
thumbnail_calc_dimensions_mm(const struct thumbnailer *ctx,
		int image_width, int image_height,
		int width_mm, int height_mm,
		int *tn_width, int *tn_height)
{
	double width_px, height_px;
	double hdpmm, vdpmm;

	if (ctx) {
		hdpmm = ctx->hdpmm;
		vdpmm = ctx->vdpmm;
	} else {
		if (get_dpmm(getenv("DISPLAY"), 0, &hdpmm, &vdpmm)) {
			hdpmm = vdpmm = 96.0 / 25.4;
		}
	}
	width_px = (double) width_mm * hdpmm;
	height_px = (double) height_mm * vdpmm;

	/*
	 * don't round up dimensions; we want to have the thumbnail to fit
	 * in given area
	 */
	if ((double) image_width / width_px >
			(double) image_height / height_px) {
		*tn_width = (int) width_px;
		*tn_height = (int) (width_px * image_height / image_width);
	} else {
		*tn_height = (int) height_px;
		*tn_width = (int) (height_px * image_width / image_height);
	}

#ifdef DEBUG
	printf("img=%dx%d scr=%.2fx%.2f dpmm - want max %dx%d mm"
			" = %.0fx%.0f px -> %dx%d px\n",
			image_width, image_height,
			hdpmm, vdpmm,
			width_mm, height_mm,
			width_px, height_px,
			*tn_width, *tn_height);
#endif
}



#if 0
static int
make_thumbnail_mm(const char *target_dir,
		Image *image, ImageInfo *info, ExceptionInfo *exception,
		const char *hash, int width_mm, int height_mm, int type)
{
/*	char buf[FILENAME_MAX]; */
	int width, height;
	Image *thumb;
	int err = 1;

	thumbnail_calc_dimensions_mm(image->columns, image->rows,
			width_mm, height_mm, &width, &height);
	thumb = ThumbnailImage(image, width, height, exception);

	if (thumb) {
		int r;

		if (build_filename(thumb->filename, MaxTextExtent,
					target_dir, hash, type)) {
			return 1;
		}
		/* file magick is guessed from file suffix */

		r = WriteImage(info, thumb);
		if (r) {
			err = 0;
		} else if (thumb->exception.severity != UndefinedException) {
			CatchException(&thumb->exception);
		}
		DestroyImage(thumb);
	}

	return err;
}
#endif



static int
make_thumbnail(const struct config *conf,
		Image *image, ImageInfo *info, ExceptionInfo *exc,
		const char *thumb_dir, const char *hash)
{
	Image *edit = NULL;
	Image *use, *thumb;
	int width, height;
	int err;

	use = image;
	if (conf->ratio >= 0.0 &&
			fabs((image->rows * conf->ratio - image->columns) /
				image->columns) >= 0.01) {
		RectangleInfo crop;

		if ((double) image->columns / image->rows >= conf->ratio) {
			crop.width = (int) (image->rows * conf->ratio);
			crop.height = (int) image->rows;
		} else {
			crop.width = (int) image->columns;
			crop.height = (int) (image->columns / conf->ratio);
		}

		switch (conf->resize) {
			case RESIZE_CROP_CENTRE:
				crop.x = (image->columns - crop.width) / 2;
				crop.y = (image->rows - crop.height) / 2;
				break;
			case RESIZE_NONE:
				fprintf(stderr, "image ratio (%dx%d=%.2f) "
						"doesn't match requested "
						"thumbnail ratio (%.2f) "
						"and cropping isn't allowed!\n",
						(int) image->columns,
						(int) image->rows,
						(double) image->columns /
							image->rows,
							conf->ratio);
				crop.x = crop.y = 0;
				crop.width = image->columns;
				crop.height = image->rows;
				break;
		}

		if (crop.x || crop.y || crop.width != image->columns ||
				crop.height != image->rows) {
#ifdef DEBUG
			fprintf(stdout, "cropping %dx%d to %dx%d+%d+%d\n",
					(int) image->columns, (int) image->rows,
					(int) crop.width, (int) crop.height,
					(int) crop.x, (int) crop.y);
#endif
			edit = CropImage(image, &crop, exc);
			if (edit) {
				use = edit;
			} else {
				fprintf(stderr, "failed to crop image!\n");
			}
		}
	}

	if (conf->ratio <= 0.0) {
		/* preserve ratio */
		if (use->columns > use->rows) {
			width = conf->max_width_px;
			height = width * use->rows / use->columns;
		} else {
			height = conf->max_height_px;
			width = height * use->columns / use->rows;
		}
	} else {
		if (conf->ratio >= 1.0) {
			width = min(use->columns, conf->max_width_px);
			height = (int) (width / conf->ratio);
		} else {
			height = min(use->rows, conf->max_height_px);
			width = (int) (height * conf->ratio);
		}
	}
#ifdef DEBUG
	fprintf(stderr, "thumbnail size: %dx%d\n", width, height);
#endif

	thumb = ThumbnailImage(use, width, height, exc);
	if (edit) {
		DestroyImage(edit);
	}

	if (thumb) {
		int r;

		if (build_filename(thumb->filename, MaxTextExtent,
					thumb_dir, hash, conf->name)) {
			fprintf(stderr, "building filename failed!\n");
			return 1;
		}
		fprintf(stdout, "wrote %s\n", thumb->filename);
		/* magick (= output format) is guessed from file suffix */

		r = WriteImage(info, thumb);
		if (r) {
			err = 0;
		} else if (thumb->exception.severity != UndefinedException) {
			CatchException(&thumb->exception);
		}
		DestroyImage(thumb);
	}


	return 0;
}



int
thumbnail_make_all_from_image(const struct thumbnailer *ctx,
		Image *image, const char *fn)
{
	ImageInfo *info;
	ExceptionInfo exception;
	char hexhash[16 * 2 + 1];
	int i;
	int err = 0;

	make_hash(hexhash, fn);

	GetExceptionInfo(&exception);
	info = CloneImageInfo((ImageInfo *) NULL);

	for (i = 0; i < ctx->n; i++) {
		err |= make_thumbnail(ctx->config[i],
				image, info, &exception,
				ctx->thumb_dir, hexhash);
	}

	DestroyImageInfo(info);
	DestroyExceptionInfo(&exception);

	return err;
}



int
thumbnail_make_all_from_data(const struct thumbnailer *ctx,
		void *data, size_t data_len, const char *fn)
{
	Image *image;
	ImageInfo *info;
	ExceptionInfo exception;
	int r;

	info = CloneImageInfo((ImageInfo *) NULL);
	if (! info) {
		return 1;
	}
	GetExceptionInfo(&exception);

	image = BlobToImage(info, data, data_len, &exception);
	if (image) {
		r = thumbnail_make_all_from_image(ctx, image, fn);
		DestroyImage(image);
	} else {
		fprintf(stderr, "failed to build image from blob!\n");
		r = 1;
	}

	DestroyImageInfo(info);
	DestroyExceptionInfo(&exception);

	return r;
}



int
thumbnail_make_all_from_raw(const struct thumbnailer *ctx,
		void *data, int width, int height, const char *format,
		const StorageType type, const char *fn)
{
	Image *image;
	ImageInfo *info;
	ExceptionInfo exception;
	int r;

	info = CloneImageInfo((ImageInfo *) NULL);
	if (! info) {
		return 1;
	}
	GetExceptionInfo(&exception);

	image = ConstituteImage(width, height, format, type, data, &exception);
	if (image) {
		r = thumbnail_make_all_from_image(ctx, image, fn);
		DestroyImage(image);
	} else {
		fprintf(stderr, "failed build image from raw data!\n");
		r = 1;
	}

	DestroyImageInfo(info);
	DestroyExceptionInfo(&exception);

	return r;
}



static int
rename_thumbnail(const char *target_dir, const char *old_hash,
		const char *new_hash, const char *type)
{
	char old_fn[FILENAME_MAX];
	char new_fn[FILENAME_MAX];
	int r;

	if (build_filename(old_fn, FILENAME_MAX, target_dir, old_hash, type)) {
		return 1;
	}
	if (build_filename(new_fn, FILENAME_MAX, target_dir, new_hash, type)) {
		return 1;
	}

	r = rename(old_fn, new_fn);
	if (r == 0) {
		fprintf(stdout, "renamed %s to %s\n", old_fn, new_fn);
		return 0;
	} else {
		fprintf(stderr, "%s: cannot rename to %s: %s\n",
				old_fn, new_fn, strerror(errno));
		return 1;
	}
}



int
thumbnail_rename_all(const struct thumbnailer *ctx,
		const char *old_fn, const char *new_fn)
{
	char old_hash[16 * 2 + 1];
	char new_hash[16 * 2 + 1];
	int i;
	int r = 0;

	make_hash(old_hash, old_fn);
	make_hash(new_hash, new_fn);

	for (i = 0; i < ctx->n; i++) {
		r |= rename_thumbnail(ctx->thumb_dir, old_hash, new_hash,
				ctx->config[i]->name);
	}

	return r;
}



static int
delete_thumbnail(const char *target_dir, const char *hash, const char *type)
{
	char fn[FILENAME_MAX];
	int r;

	if (build_filename(fn, FILENAME_MAX, target_dir, hash, type)) {
		return 1;
	}

	r = unlink(fn);
	if (r == 0 || errno == ENOENT) {
		fprintf(stdout, "deleted %s\n", fn);
		return 0;
	} else {
		fprintf(stderr, "%s: cannot delete: %s\n", fn, strerror(errno));
		return 1;
	}
}



int
thumbnail_delete_all(const struct thumbnailer *ctx, const char *fn)
{
	char hexhash[16 * 2 + 1];
	int r = 0;
	int i;

	make_hash(hexhash, fn);
	for (i = 0; i < ctx->n; i++) {
		r |= delete_thumbnail(ctx->thumb_dir, hexhash,
				ctx->config[i]->name);
	}

	return r;
}



struct thumbnailer *
thumbnail_init(const char *self, const char *thumb_dir, const char *conffile)
{
	struct thumbnailer *config;
	char dir[FILENAME_MAX];
	int i;

	config = make_config(thumb_dir, conffile);
	if (! config) {
		return NULL;
	}

	for (i = 0; i < config->n; i++) {
		if (! get_thumbnail_dir(dir, FILENAME_MAX, thumb_dir,
					config->config[i]->name)) {
			int ret;
			ret = mkdir(dir, 0700);
			if (! (ret == 0 || errno == EEXIST)) {
				fprintf(stderr, "%s: cannot create: %s\n",
						dir, strerror(errno));
				return NULL;
			}
		}
	}

	if (!getenv("DISPLAY") || get_dpmm(getenv("DISPLAY"), 0,
				&config->hdpmm, &config->vdpmm)) {
		config->hdpmm = config->hdpmm = 0.0;
	}

	return config;
}



void
thumbnail_uninit(struct thumbnailer *ctx)
{
	free_config(ctx);
}
