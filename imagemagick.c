#include "plugin.h"

#include <string.h>
// #include <unistd.h>
// #include <libgen.h>

#include <stdio.h>
#include <stdlib.h>

#include <magick/api.h>

/* #define RETURN_BLOB */


/* TODO
 *  - Buffer overflow check for ImageInfo filenames
 *  - ExceptionInfo checking for ImageMagick
 */


#define SELF	"libplugin-imagemagick.so"


struct plugin_context {
	/* nothing in ImageMagick's context */
	int unused;
};

struct reply_internal {
	struct plugin_context *ctx;
	Image *image;
	ImageInfo *info;
};



const char **
get_mimetypes(struct plugin_context *ctx)
{
	static char *mimetypes[] = {
		"image/*",
		NULL
	};
	return (const char **) mimetypes;
}



struct plugin_context *
init(const char *self)
{
	struct plugin_context *ctx;

	ctx = malloc(sizeof(struct plugin_context));
	if (! ctx) {
		fprintf(stderr, "%s: cannot create context\n", SELF);
		return NULL;
	}

	return ctx;
}



void
uninit(struct plugin_context *ctx)
{
	free(ctx);
}



static Image *
open_image(const char *fn, ImageInfo *info, ExceptionInfo *exception)
{
	Image *image;

	if (strlen(fn) < MaxTextExtent) {
		strcpy(info->filename, fn);
		image = ReadImage(info, exception);
	} else {
		return NULL;
		/* TODO split fn, chdir, ReadImage, chdir */
	}

	return image;
}



static void
free_reply(struct plugin_reply *reply)
{
	struct reply_internal *internal =
		(struct reply_internal *) reply->internal;

#ifdef RETURN_BLOB
	if (reply->data) {
		MagickFree(reply->data);
	}
#endif
	if (internal->info) {
		DestroyImageInfo(internal->info);
	}
	if (internal->image) {
		DestroyImage(internal->image);
	}

	free(reply->internal);
}



static Image *
orient_image(Image *image, ImageInfo *info, ExceptionInfo *exception)
{
	Image *oriented = NULL;
	SyncImageSettings(info, image);

	switch (image->orientation) {
		case TopLeftOrientation:
			return image;
		case TopRightOrientation:
			oriented = FlopImage(image, exception);
			break;
		case BottomRightOrientation:
			oriented = RotateImage(image, 180.0, exception);
			break;
		case BottomLeftOrientation:
			oriented = FlipImage(image, exception);
			break;
		case LeftTopOrientation:
			oriented = TransposeImage(image, exception);
			break;
		case RightTopOrientation:
			oriented = RotateImage(image, 90.0, exception);
			break;
		case RightBottomOrientation:
			oriented = TransverseImage(image, exception);
			break;
		case LeftBottomOrientation:
			oriented = RotateImage(image, 270.0, exception);
			break;
		default:
			/*
			 * UnknownOrientation is not an error or even
			 * "condition". For example, PNG files have unknown
			 * orientation. We'll simply return the original.
			 */
			return image;
	}
	if (oriented) {
		DestroyImage(image);
		oriented->orientation = TopLeftOrientation;
		return oriented;
	} else {
		fprintf(stderr, "%s: failed to rotate image\n", SELF);
		return image;
	}
}



int
get_image(struct plugin_context *ctx, const char *fn,
		int width, int height, struct plugin_reply *reply)
{
	struct reply_internal *internal;
	ExceptionInfo exception;
	int err = 1;

	internal = malloc(sizeof(struct reply_internal));
	if (! internal) {
		return 1;
	}

	GetExceptionInfo(&exception);
	internal->info = CloneImageInfo((ImageInfo *) NULL);
	if (! internal->info) {
		free(internal);
		return 1;
	}

	reply->internal = internal;
	internal->ctx = ctx;

	internal->image = open_image(fn, internal->info, &exception);
	if (internal->image) {
		/* returns input image untouched if re-orientation
		 * is not needed */
		internal->image = orient_image(internal->image,
				internal->info, &exception);
#ifndef RETURN_BLOB
		reply->data = internal->image;
		reply->type = PLUGIN_REPLY_TYPE_IMAGE;
		reply->free = free_reply;
		err = 0;
#else
		reply->data = ImageToBlob(internal->info, internal->image,
				&reply->data_len,
				&exception);
		if (reply->data) {
			reply->type = PLUGIN_REPLY_TYPE_DATA;
			reply->free = free_reply;
			err = 0;
		}
#endif
	}

	DestroyExceptionInfo(&exception);
	if (err) {
		free_reply(reply);
	}

	return err;
}
