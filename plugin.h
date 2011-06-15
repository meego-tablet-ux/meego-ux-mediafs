#ifndef PLUGIN_H
#define PLUGIN_H

#include <stdlib.h>

/*
 * Introduction to media-preprocessor reader plugins
 *
 * Reader plugins are dynamically loaded objects that can extract a bitmap
 * image from a file. The most basic kind of plugin simply opens input image
 * and makes raw pixel data available for the pre-processor itself. When
 * media-processor is done with the data (i.e. it has created the actual
 * thumbnails), it asks plugin to free the image data.
 *
 * All plugins must implement three functions: init(), uninit() and
 * get_image(). See #struct plugin for more. Plugins should also implement
 * functions get_mimetypes() and get_suffixes(), but this is not required. Note
 * that if neither get_mimetypes() or get_suffixes() have been implemented,
 * depending on the configuration of media-preprocessor the plugin may be
 * completely useless.
 *
 * Mime types are the primary mean to find suitable plugin for processing a
 * file. If none of the plugins support the type, file suffix match is also
 * attempted. Depending on the configuration, media-preprocessor may try all
 * remaining plugins if file suffix match fails. Note that it is also possible
 * that get_image() fails.
 *
 *
 * In pseudo-code:
 *
 *	image = null
 *	mimetype = get_mimetype(file)
 *	if mimetype
 *	   for plugin in reader_plugins
 *	       if plugin.supports(mimetype)
 *		   image = plugin.process(file)
 *		   plugin.tried = true
 *		   if image
 *		       break
 *	if not image
 *	   suffix = file.suffix()
 *	   for plugin in reader_plugins
 *	       if not plugin.tried and plugin.supports(suffix)
 *		   image = plugin.process(file)
 *		   plugin.tried = true
 *		   if image
 *		       break
 *	if image
 *	   (invoke post processors -- not implemented)
 *	   create_thumbnails(file, image)
 *	else
 *	   delete_thumbnails(file)
 *
 *
 * Reader plugin reply
 *
 * Because not all reader plugins are alike, plugins have the following options
 * to return image data. Reply type must be set accordingly.
 *
 * PLUGIN_REPLY_TYPE_IMAGE
 *	Fully populated ImageMagick Image structure.
 *	Required fields: data
 *
 * PLUGIN_REPLY_TYPE_DATA
 *	Image data as it would appear on disk (e.g. PNG file)
 *	Required fields: data, data_len
 *
 * PLUGIN_REPLY_TYPE_RAW
 *	Raw image data, as it might appear decoded in application memory.
 *	Required fields: data, width, height, format, pixeltype
 *
 * Most replies have function pointer free() set. This function is called when
 * media-preprocessor is done with the data. Reply may also contain
 * plugin-specific internal data. See #struct plugin_reply for more.
 */


/* defined in each plugin, never accessed outside plugin itself */
struct plugin_context;



enum plugin_reply_type {
	PLUGIN_REPLY_TYPE_IMAGE,		/* ImageMagick Image * */
	PLUGIN_REPLY_TYPE_IMAGE_FILE_DATA,	/* image file data */
	PLUGIN_REPLY_TYPE_RAW_PIXELS,		/* raw pixel data */
};

enum plugin_reply_pixel_type {
	PLUGIN_REPLY_UNDEF_PIXEL,	/* undefined colour component size */
	PLUGIN_REPLY_CHAR_PIXEL,	/* char per colour component */
	PLUGIN_REPLY_DOUBLE_PIXEL,	/* double per colour component */
	PLUGIN_REPLY_FLOAT_PIXEL,	/* float per colour component */
	PLUGIN_REPLY_OTHER_PIXEL,	/* use pixel_type_other */
};

struct plugin_reply {
	enum plugin_reply_type type;

	/*
	 * Populated fields of this structure depend on #type.
	 *
	 * PLUGIN_REPLY_TYPE_IMAGE
	 * fields: data
	 *
	 * PLUGIN_REPLY_TYPE_IMAGE_FILE_DATA
	 * fields: data, data_len
	 *
	 * PLUGIN_REPLY_TYPE_RAW_PIXELS
	 * fields: data, width, height, pixel_format, pixel_type
	 *		(pixel_type_other)
	 */

	void *data;		/* pointer to reply image data */
	size_t data_len;	/* data length in octets */
	int width;
	int height;

	/*
	 * Pixel format of raw data reply, e.g. "RGB" or "aCMYK". This field
	 * is ultimately passes to ImageMagick as is.
	 */
	char pixel_format[16];

	/*
	 * Pixel type of raw data reply. If #enum plugin_reply_pixel_type
	 * doesn't support pixel type in question, use
	 * %PLUGIN_REPLY_OTHER_PIXEL and set #pixel_type_other. Typically
	 * pixel_type_other is not used.
	 */
	enum plugin_reply_pixel_type pixel_type;
	int pixel_type_other;

	/*
	 * Callback function to free the reply internals.
	 */
	void (*free)(struct plugin_reply *reply);

	void *internal;	/* pluguin internal, free to use */
};


struct plugin {
	/* required plugin functions */

	/**
	 * init:
	 * @self: path to main application
	 *
	 * Initialise plugin.
	 *
	 * Prepares plugin ready to accept #get_image() calls. If possible,
	 * plugin should make sanity checks to make sure it won't fail as soon
	 * as #get_image() is called. In other words, if the plugin needs to
	 * dynamically load libraries or allocate other resources, it should
	 * check for those resources here.
	 *
	 * Plugins can assume g_thread_init() has already been called.
	 *
	 * Returns: pointer to plugin structure if initialisation was success
	 * or NULL on failure.
	 */
	struct plugin_context *(*init)(const char *self);

	/**
	 * uninit:
	 * @ctx: plugin context (from #struct plugin)
	 *
	 * Unitialise plugin.
	 *
	 * Frees allocated resources. It is not possible to use the plugin
	 * after this function has been called. This function is called only
	 * once.
	 */
	void (*uninit)(struct plugin_context *ctx);

	/**
	 * get_image:
	 * @ctx: plugin context (from #struct plugin)
	 * @fn: path to file
	 * @width: requested image width
	 * @height: requested image height
	 * @reply: pre-allocated but unpopulated #struct plugin_reply
	 *
	 * Extracts image from #file.
	 *
	 * Extracts image from #file, suitable for post-processing into
	 * thumbnail or preview image. Image file reader may simply decode
	 * given file and video player may play the file until suitable frame
	 * is found.
	 *
	 * On failure, the content of @reply is undefined. On success, plugin
	 * populates @reply. If not %NULL, @reply->free is called to free the
	 * reply and associated resources.
	 *
	 * Note that the plugin is not required to return an image with
	 * requested dimensions; size request should only be used as reference.
	 * Unless very large, reader plugins should /not/ scale a bitmap image.
	 * If the source file does not have native pixel dimensions (e.g. the
	 * file is a SVG file), plugin should create an image at least as big
	 * as the requested size.
	 *
	 * Returns: 0 on success, non-0 on failure. Reply is populated on
	 * success.
	 */
	int (*get_image)(struct plugin_context *ctx, const char *fn,
			int width, int height,
			struct plugin_reply *reply);


	/* optional plugin functions */

	/**
	 * get_mimetypes:
	 * @ctx: plugin context (from #struct plugin)
	 *
	 * Get list of supported mime types.
	 *
	 * Returns: NULL-terminated list of supported mime types.
	 */
	const char **(*get_mimetypes)(struct plugin_context *ctx);

	/*
	 * get_suffixes:
	 * @ctx: plugin context (from #struct plugin)
	 *
	 * Get list of (probably) supported file suffixes.
	 *
	 * Returns: NULL-terminated list of (probably) supported file suffixes.
	 */
	const char **(*get_suffixes)(struct plugin_context *ctx);


	/* plugin-private pointer */
	struct plugin_context *ctx;
};



#endif
