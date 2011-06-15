#ifndef THUMBNAIL_H_
#define THUMBNAIL_H_

#include <stdint.h>
#include <magick/api.h>



struct thumbnailer;


struct thumbnailer *thumbnail_init(
		const char *self, const char *thumb_dir,
		const char *conffile);
void thumbnail_uninit(struct thumbnailer *ctx);

int thumbnail_make_all_from_image(const struct thumbnailer *ctx,
		Image *image, const char *fn);
int thumbnail_make_all_from_data(const struct thumbnailer *ctx,
		void *data, size_t data_len, const char *fn);
int thumbnail_make_all_from_raw(const struct thumbnailer *ctx,
		void *data, int width, int height, const char *pixel_format,
		const StorageType pixel_type, const char *fn);

int thumbnail_rename_all(const struct thumbnailer *ctx,
		const char *old_fn, const char *new_fn);

int thumbnail_delete_all(const struct thumbnailer *ctx,
		const char *fn);

void thumbnail_calc_dimensions_mm(const struct thumbnailer *ctx,
		int image_width, int image_height,
		int width_mm, int height_mm,
		int *tn_width, int *tn_height);



#endif
