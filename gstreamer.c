#include "plugin.h"
#include "thumbnail.h"

#include <string.h>

#include <gst/gst.h>

#define SELF	"plugin-gstreamer.so"
#define WATCHDOG_TIME	10000

/* #define USE_APPSINK */

/*
 * We could pick only streams we're interested in (and thus possibly save
 * some CPU time), but it seems that not all video stream identifiers contain
 * string "video". If only we could pick video streams if they exist and if
 * no video streams are found, accept everything... For now we'll simply
 * accept everything. Sigh.
 */
static gboolean select_streams = FALSE;

/* #define BUS_MESSAGES */



struct plugin_context {
	const char *thumb_dir;

	GMainLoop *loop;
	GstElement *pipeline;
	GstElement *video;
	GstElement *grabsink;

	guint watchdog_id;

	gboolean seek_done;
	int grab_done;
	int data_size;

	struct plugin_reply *reply;
};


enum {
	GRAB_FRAME_NONE,
	GRAB_FRAME_FIRST,
	GRAB_FRAME_GOOD
};


struct reply_internal {
	GstBuffer *buffer;
};



static char *mimetypes[] = {
	"video/*",
	NULL
};

/* random pick of file suffixes that libmagic may not identify correctly */
static char *suffixes[] = {
	"mpeg", "mpg",	/* libmagic fails to identify some mpeg streams */
	"ogv", "ogg",	/* mime type doesn't reflect actual contents */
	"avi", "divx", "mov", "mp4",
	"qt", "3gp", "3g2", "flv",
	"rm","swf", "vob", "wmv",
	NULL
};



const char **
get_mimetypes(struct plugin_context *ctx)
{
	return (const char **) mimetypes;
}



const char **
get_suffixes(struct plugin_context *ctx)
{
	return (const char **) suffixes;
}



struct plugin_context *
init(const char *self, const char *thumb_dir)
{
	struct plugin_context *ctx;
	GError *error;

	error = NULL;
	if (! gst_init_check(NULL, NULL, &error)) {
		fprintf(stderr, "%s: failed to initialise gstreamer: %s\n",
				SELF, error->message);
		g_error_free(error);
		return NULL;
	}

	ctx = malloc(sizeof(struct plugin_context));
	if (! ctx) {
		gst_deinit();
		fprintf(stderr, "%s: cannot create context\n", SELF);
		return NULL;
	}
	ctx->thumb_dir = thumb_dir;

	ctx->loop = g_main_loop_new(NULL, FALSE);
	if (! ctx->loop) {
		gst_deinit();
		fprintf(stderr, "%s: cannot create mainloop for gst!\n", SELF);
		free(ctx);
		return NULL;
	}
	g_main_loop_ref(ctx->loop);

	return ctx;
}



void
uninit(struct plugin_context *ctx)
{
	g_main_loop_unref(ctx->loop);

	gst_deinit();

	free(ctx);
}



static void
free_reply(struct plugin_reply *reply)
{
#ifdef USE_APPSINK
	struct reply_internal *internal =
		(struct reply_internal *) reply->internal;

	if (internal->buffer) {
		gst_buffer_unref(internal->buffer);
		internal->buffer = NULL;
	}
#else
	free(reply->data);
#endif

	free(reply->internal);
}



#ifdef USE_APPSINK
static gboolean
grab_frame(struct plugin_context *ctx)
{
	struct plugin_reply *reply = ctx->reply;
	struct reply_internal *internal = ctx->reply->internal;

	gboolean r;
	gboolean got_it = FALSE;

	GstElement *sink;

	sink = ctx->grabsink;

	/* pull-preroll may hang */
	fprintf(stdout, "%s: getting buffer\n", SELF);
	g_signal_emit_by_name(sink, "pull-preroll", &internal->buffer, NULL);

	if (internal->buffer) {
		GstCaps *caps;
		caps = GST_BUFFER_CAPS(internal->buffer);
		if (caps) {
			GstStructure *s;
			gint width, height;
			width = height = -1;

			s = gst_caps_get_structure(caps, 0);
			/* TODO check return values */
			gst_structure_get_int(s, "width", &width);
			gst_structure_get_int(s, "height", &height);

			reply->type = PLUGIN_REPLY_TYPE_RAW;
			reply->data = GST_BUFFER_DATA(internal->buffer);
			reply->free = free_reply;
			reply->width = width;
			reply->height = height;
			sprintf(reply->format, "RGB");
			reply->pixeltype = CharPixel;
			got_it = TRUE;
		}
	}

	return got_it;
}



#else



static void
handoff_cb(GstElement *bin, GstBuffer *buffer, GstPad *pad, gpointer data)
{
	struct plugin_context *ctx = data;
	GstCaps *caps;

	fprintf(stdout, "%s: %s\n", SELF, __FUNCTION__);

	if (ctx->grab_done == GRAB_FRAME_GOOD) {
		fprintf(stdout, "%s: grabbed two frames already\n", SELF);
		return;
	}
	if (! ctx->seek_done) {
		fprintf(stdout, "%s: warning: video not seeked!\n", SELF);
		if (ctx->grab_done == GRAB_FRAME_FIRST) {
			return;
		}
	}

	/* TODO check is frame is ok */

	caps = GST_BUFFER_CAPS(buffer);
	if (caps) {
		struct plugin_reply *reply = ctx->reply;
		int data_size;
		GstStructure *s;
		gint width, height;
		width = height = -1;

		s = gst_caps_get_structure(caps, 0);
		/* TODO check return values */
		gst_structure_get_int(s, "width", &width);
		gst_structure_get_int(s, "height", &height);

		((struct reply_internal *) reply->internal)->buffer = buffer;

		reply->type = PLUGIN_REPLY_TYPE_RAW_PIXELS;
		data_size = GST_BUFFER_SIZE(buffer);

		if (ctx->grab_done == GRAB_FRAME_NONE) {
			reply->data = malloc(data_size);
			ctx->data_size = data_size;
		} else if (data_size > ctx->data_size) {
			void *newdata = realloc(reply->data, data_size);
			if (newdata) {
				reply->data = newdata;
				ctx->data_size = data_size;
			} else {
				fprintf(stderr, "%s: no memory for second "
						"grab, using the first one!\n",
						SELF);
			}
		}

		if (ctx->data_size >= data_size) {
			reply->data_len = data_size;
			reply->free = free_reply;
			reply->width = width;
			reply->height = height;
			sprintf(reply->pixel_format, "RGB");
			reply->pixel_type = PLUGIN_REPLY_CHAR_PIXEL;

			/* gstreamer seems to pad row width to word size */
			/* GraphicsMagick/ImageMagick won't do this for us!? */
			if (width % 4 != 0) {
				char *src = (char *) GST_BUFFER_DATA(buffer);
				char *dst = reply->data;
				int row;
				int pad = width % 4;
				for (row = 0; row < height; row++) {
					memcpy(dst, src, width * 3);
					src += width * 3 + pad;
					dst += width * 3;
				}
			} else {
				memcpy(reply->data, GST_BUFFER_DATA(buffer),
						data_size);
			}

			if (ctx->seek_done) {
				g_main_loop_quit(ctx->loop);
				ctx->grab_done = GRAB_FRAME_GOOD;
			} else {
				ctx->grab_done = GRAB_FRAME_FIRST;
			}
		}
	}
}



static void
preroll_handoff_cb(GstElement *bin, GstBuffer *buffer, GstPad *pad, gpointer data)
{
	handoff_cb(bin, buffer, pad, data);
}
#endif



static void
stream_ready(struct plugin_context *ctx)
{
	if (! ctx->seek_done) {
		GstFormat fmt = GST_FORMAT_TIME;
		gint64 len, pos;
		gboolean r;

		if (gst_element_query_duration(ctx->pipeline, &fmt, &len)) {
			pos = len * 1 / 10;
		} else {
			fprintf(stdout, "%s: media length query failed\n",
					SELF);
			len = 0;
			pos = 5 * GST_SECOND;
		}

		fprintf(stdout, "%s: trying to seek to %.1f/%.1f sec\n", SELF,
				(double) pos / GST_SECOND,
				(double) len / GST_SECOND);
		r = gst_element_seek(ctx->pipeline, 1.0, GST_FORMAT_TIME,
				GST_SEEK_FLAG_KEY_UNIT | GST_SEEK_FLAG_FLUSH
				| GST_SEEK_FLAG_SKIP,
				GST_SEEK_TYPE_SET, pos,
				GST_SEEK_TYPE_NONE, GST_CLOCK_TIME_NONE);
		if (! r ) {
			fprintf(stdout, "%s: seek failed\n", SELF);
		} else {
			ctx->seek_done = TRUE;
		}

		/* this helps with some funky videos */
		gst_element_set_state(ctx->pipeline, GST_STATE_PLAYING);
	}

#ifdef USE_APPSINK
	if (! ctx->grab_done) {
		if (grab_frame(ctx)) {
			ctx->grab_done = TRUE;
			g_main_loop_quit(ctx->loop);
		}
	}
#endif
}



static gboolean
autoplug_continue_cb(GstElement *bin, GstPad *pad, GstCaps *caps,
		gpointer user_data)
{
	int i;
	for (i = 0; i < gst_caps_get_size(caps); i++) {
		GstStructure *str;
		str = gst_caps_get_structure(caps, i);
		if (select_streams == FALSE
				|| g_strrstr(gst_structure_get_name(str),
					"video")) {
			fprintf(stdout, "%s: accepted stream[%d]: %s\n", SELF,
					i, gst_structure_get_name(str));

			return TRUE;
		} else {
			fprintf(stdout, "%s: ignored stream[%d]: %s\n", SELF,
					i, gst_structure_get_name(str));
		}
	}
	return FALSE;
}



static void
pad_added_cb(GstElement *decodebin, GstPad *pad, gpointer data)
{
	struct plugin_context *ctx = data;
	GstCaps *caps;
	int i;

	/* check media type */
	caps = gst_pad_get_caps(pad);
	if (! caps) {
		fprintf(stdout, "%s: pad added with no caps in!\n", SELF);
		return;
	}

	for (i = 0; i < gst_caps_get_size(caps); i++) {
		GstStructure *str;
		GstPad *videopad;
		str = gst_caps_get_structure(caps, i);

		if (! g_strrstr(gst_structure_get_name(str), "video")) {
			continue;
		}

		videopad = gst_element_get_static_pad(ctx->video, "videosink");
		/* only link once */
		if (GST_PAD_IS_LINKED(videopad)) {
			continue;
		}
		if (GST_PAD_LINK_FAILED(gst_pad_link(pad, videopad))) {
			fprintf(stderr, "%s: failed to link "
					"new pad to video\n", SELF);
		} else {
			g_object_unref(videopad);
		}
	}
	gst_caps_unref(caps);
}



static gboolean
watchdog_timeout(gpointer data)
{
	struct plugin_context *ctx = data;
	fprintf(stderr, "%s: getting thumbnail timed out\n", SELF);
	g_main_loop_quit(ctx->loop);
	ctx->watchdog_id = 0;
	return FALSE;
}



static void
bus_state_changed_cb(GstBus *bus, GstMessage *message, gpointer data)
{
	struct plugin_context *ctx = data;
	GstState state, pending;
	GstStateChangeReturn ret;

	fprintf(stdout, "%s: bus state changed: %s\n", SELF,
			GST_MESSAGE_SRC_NAME(message));
	/* TODO
	 * gst_element_get_state() here causes a small delay. If we don't
	 * slow down a bit (e.g. call gst_message_parse_state_changed()
	 * instead), seeking fails and we're screwed.
	 */

	ret = gst_element_get_state(ctx->pipeline,
			&state, &pending, GST_SECOND / 10);
	if (state == GST_STATE_PAUSED) {
		fprintf(stdout, "%s: stream ready\n", SELF);
		stream_ready(ctx);
	}
}



static void
bus_error_cb(GstBus *bus, GstMessage *message, gpointer data)
{
	struct plugin_context *ctx = data;
	GError *err;
	gchar *debug;

	gst_message_parse_error(message, &err, &debug);
	fprintf(stderr, "%s: error: %s\n", SELF, err->message);
	g_error_free(err);
	g_free(debug);

	g_main_loop_quit(ctx->loop);
}



static void
bus_eos_cb(GstBus *bus, GstMessage *message, gpointer data)
{
	struct plugin_context *ctx = data;
	fprintf(stdout, "%s: end of stream\n", SELF);

	g_main_loop_quit(ctx->loop);
}



#ifdef BUS_MESSAGES
static void
bus_message_cb(GstBus *bus, GstMessage *message, gpointer data)
{
	fprintf(stdout, "%s: bus message: %s\n", SELF,
			GST_MESSAGE_TYPE_NAME(message));
}
#endif



int
get_image(struct plugin_context *ctx, const char *fn,
		int width, int height,
		struct plugin_reply *reply)
{
	/* TODO check clean up branches */

	GstElement *filesrc, *decoder, *colorspace;
	GstBus *bus;
	GstPad *videopad, *ghostpad;
	GstCaps *caps;
	GstStateChangeReturn ret;

	reply->data = NULL;

	ctx->reply = reply;	/* grab_frame needs ctx and reply */
	ctx->seek_done = FALSE;
	ctx->grab_done = GRAB_FRAME_NONE;

	/* create pipeline */
	ctx->pipeline = gst_pipeline_new("pipeline");
	if (! ctx->pipeline) {
		fprintf(stderr, "%s: failed to create pipeline\n", SELF);
		return 1;
	}

	bus = gst_pipeline_get_bus(GST_PIPELINE(ctx->pipeline));
	if (! bus) {
		fprintf(stderr, "%s: failed to get pipeline bus\n", SELF);
		gst_object_unref(ctx->pipeline);
		return 1;
	}
	gst_bus_add_signal_watch(bus);
	g_signal_connect(bus, "message::state-changed",
			G_CALLBACK(bus_state_changed_cb), ctx);
	g_signal_connect(bus, "message::error", G_CALLBACK(bus_error_cb), ctx);
	g_signal_connect(bus, "message::eos", G_CALLBACK(bus_eos_cb), ctx);
#ifdef BUS_MESSAGES
	g_signal_connect(bus, "message", G_CALLBACK(bus_message_cb), ctx);
#endif
	gst_object_unref(bus);


	/* source and decoder */
	filesrc = gst_element_factory_make("filesrc", "source");
	if (! filesrc) {
		fprintf(stderr, "%s: failed to create filesrc\n", SELF);
		gst_object_unref(ctx->pipeline);
		return 1;
	}
	g_object_set(G_OBJECT(filesrc), "location", fn, NULL);
	gst_bin_add(GST_BIN(ctx->pipeline), filesrc);

	decoder = gst_element_factory_make("decodebin2", "decoder");
	if (! decoder) {
		fprintf(stderr, "%s: failed to create decoder\n", SELF);
		gst_object_unref(ctx->pipeline);
		return 1;
	}
	g_signal_connect(decoder, "autoplug-continue",
			G_CALLBACK(autoplug_continue_cb), ctx);
	g_signal_connect(decoder, "pad-added", G_CALLBACK(pad_added_cb), ctx);
	gst_bin_add(GST_BIN(ctx->pipeline), decoder);

	if (! gst_element_link(filesrc, decoder)) {
		fprintf(stderr, "%s: failed to link source and decoder\n",
				SELF);
		gst_object_unref(ctx->pipeline);
		return 1;
	}


	/* create videosink */
	ctx->video = gst_bin_new("videobin");
	if (! ctx->video) {
		fprintf(stderr, "%s: failed to create videobin", SELF);
		gst_object_unref(ctx->pipeline);
		return 1;
	}
	gst_bin_add(GST_BIN(ctx->pipeline), ctx->video);

	colorspace = gst_element_factory_make("ffmpegcolorspace", "colorspace");
	if (! colorspace) {
		fprintf(stderr, "%s: failed to create colorspace converter\n",
				SELF);
		gst_object_unref(ctx->pipeline);
		return 1;
	}
	gst_bin_add(GST_BIN(ctx->video), colorspace);

#ifdef USE_APPSINK
	ctx->grabsink = gst_element_factory_make("appsink", "vsink");
#else
	ctx->grabsink = gst_element_factory_make("fakesink", "vsink");
#endif
	if (! ctx->grabsink) {
		fprintf(stderr, "%s: failed to create grabsink\n", SELF);
		gst_object_unref(ctx->pipeline);
		return 1;
	}
#ifndef USE_APPSINK
	g_object_set(ctx->grabsink, "signal-handoffs", TRUE, NULL);
	g_object_set(ctx->grabsink, "num-buffers", 1, NULL);
	g_signal_connect(ctx->grabsink, "preroll-handoff",
			G_CALLBACK(preroll_handoff_cb), ctx);
	g_signal_connect(ctx->grabsink, "handoff",
			G_CALLBACK(handoff_cb), ctx);
#endif

	gst_bin_add(GST_BIN(ctx->video), ctx->grabsink);

	caps = gst_caps_new_simple("video/x-raw-rgb",
			"bpp", G_TYPE_INT, 24,
			"depth", G_TYPE_INT, 24,
			NULL);
	if (! caps) {
		fprintf(stderr, "%s: failed to create caps for rgb\n", SELF);
		gst_object_unref(ctx->pipeline);
		return 1;
	}
	if (! gst_element_link_filtered(colorspace, ctx->grabsink, caps)) {
		fprintf(stderr, "%s: failed to link gst elements\n", SELF);
		gst_caps_unref(caps);
		gst_object_unref(ctx->pipeline);
		return 1;
	}
	gst_caps_unref(caps);

	/* XXX can these fail? */
	videopad = gst_element_get_static_pad(colorspace, "sink");
	ghostpad = gst_ghost_pad_new("videosink", videopad);
	gst_element_add_pad(ctx->video, ghostpad);
	gst_object_unref(videopad);


	reply->internal = malloc(sizeof(struct reply_internal));
	if (! reply->internal) {
		gst_object_unref(ctx->pipeline);
		return 1;
	}
	((struct reply_internal *) reply->internal)->buffer = NULL;


	/* add failsafe */
	ctx->watchdog_id = g_timeout_add(WATCHDOG_TIME, watchdog_timeout, ctx);


	/* run */
	ret = gst_element_set_state(ctx->pipeline, GST_STATE_PAUSED);
	if (ret == GST_STATE_CHANGE_FAILURE) {
		fprintf(stderr, "%s: gstreamer failed\n", SELF);
		return 1;
	}

	g_main_loop_run(ctx->loop);


	/* cleanup */
	gst_bus_remove_signal_watch(bus);
	gst_element_set_state(ctx->pipeline, GST_STATE_NULL);

	if (ctx->watchdog_id) {
		g_source_remove(ctx->watchdog_id);
	}

	if (ctx->grab_done == GRAB_FRAME_NONE) {
		free_reply(ctx->reply);
	}

	if (videopad && ! GST_PAD_IS_LINKED(videopad)) {
		gst_object_unref(videopad);
	}
	gst_object_unref(GST_OBJECT(ctx->pipeline));


	return (ctx->grab_done == GRAB_FRAME_NONE) ? 1 : 0;
}
