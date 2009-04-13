/*
 *
 */
#ifndef IMAGE_H
#define IMAGE_H
struct image {
	unsigned xres, yres, bpp, rowbytes;
	unsigned char *image_data;
};

int image_create(struct image *img, unsigned width, unsigned height, unsigned bpp, unsigned rowbytes);
int image_create_from_data(struct image *img, unsigned width, unsigned height, unsigned bpp, unsigned rowbytes, unsigned char *data);
void image_destroy(struct image *img);
int load_png(const char *filename, struct image *img);
int load_chr(const char *filename, struct image *img, unsigned width, unsigned tile_height, unsigned bpp, unsigned tiles_per_row);
int save_png(const char *filename, struct image *img);
int save_chr(const char *filename, struct image *img, unsigned tile_w, unsigned tile_h);
#endif
