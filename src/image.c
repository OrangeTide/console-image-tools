/*
 *
 */
#include <assert.h>
#include <errno.h>
/* #include <setjmp.h> - included in png.h */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include <png.h>

#include "image.h"
#include "log.h"
#include "util.h"

static inline size_t calc_rowbytes(unsigned width, unsigned bpp) {
	return (width*bpp+7)/8; /* round up to nearest byte */
}

static unsigned get_pixel(const struct image *img, unsigned x, unsigned y) {
	unsigned pixels_per_byte, pixel_index, ret;

	assert(img != NULL);
	assert(x < img->xres);
	assert(y < img->yres);

	if(x>=img->xres || y>=img->yres) {
		return 0;
	}

#if 0 /* diagnostic junk */
	fprintf(stderr, "x:%d y:%d ", x, y);
#endif

	pixels_per_byte=8/img->bpp;
	pixel_index=(~x)%pixels_per_byte; /* which pixel - MSB is the low order pixel */
	x/=pixels_per_byte;

	/* get the pixel and shuffle it */
	ret=(img->image_data[y*img->rowbytes+x]>>(img->bpp*pixel_index))&((1<<img->bpp)-1);;
#if 0 /* diagnostic junk */
	fprintf(stderr, "ppb=%u pi=%u x'=%d c=%#x\n", pixels_per_byte, pixel_index, x, ret);
#endif
	return ret;
}

/* get pixel from a planar buffer
 * len is the length of the whole interlaced region in bytes
 * set endian to 1 for NES PPU's endian */
static unsigned get_pixel_planar(const unsigned char *ptr, unsigned bpp, size_t len, unsigned w, unsigned x, unsigned y, unsigned endian) {
	unsigned i, g, pixel_index;

#if 0 /* diagnostic junk */
	TRACE("x:%u y:%u bpp:%u w:%u len:%lu\n", x, y, bpp, w, len);
#endif
	assert(bpp > 0 && bpp <= 32);
	assert(y < len*w/bpp);

	/* 1 bit per plane */
	pixel_index=x%8;
	x/=8;

	/* this works out to swap bytes because of the planar nature */
	if((w*bpp)/8>1 && endian)
		pixel_index^=1<<bpp;

	ptr=ptr+x+y*((w+7)/8); /* treat as 1bpp for rowbytes */
	g=0;
	for(i=0;i<bpp;i++,ptr+=(len/bpp)) {
		g<<=1;
		g|=(*ptr>>pixel_index)&1;
	}
#if 0 /* diagnostic junk */
	TRACE("(%u,%u)=0x%x pi=%d ptr=%p w=%d\n", x*8+pixel_index, y, g, pixel_index, ptr, w);
#endif
	return g;
}

static void put_pixel(struct image *img, unsigned x, unsigned y, unsigned c) {
	unsigned pixels_per_byte, pixel_index;
	unsigned char *p, mask;

	// TRACE("x:%u y:%u xres:%u yres:%u bpp:%u c:%#x\n", x, y, img->xres, img->yres, img->bpp, c);
	assert(img != NULL);
	assert(x < img->xres);
	assert(y < img->yres);
	assert((c&((1<<img->bpp)-1)) == c);

	if(x>=img->xres || y>=img->yres) {
		return; /* ignore */
	}

	pixels_per_byte=8/img->bpp;
	pixel_index=x%pixels_per_byte; /* which pixel */
	x/=pixels_per_byte;

	c&=(1<<img->bpp)-1; /* mask off unnecessary bits */

	p=&img->image_data[y*img->rowbytes+x]; /* find the appropriate byte */

	mask=~(((1<<img->bpp)-1)<<(img->bpp*pixel_index)); /* clear the bits from the original */
	*p&=mask;
	*p|=c<<(img->bpp*pixel_index); /* set the bits */

	// TRACE("ofs:%u bpp:%u pi:%u c=0x%x c2=0x%x mask=0x%x *p=0x%x\n", p-img->image_data, img->bpp, pixel_index, c, c<<(img->bpp*pixel_index), mask, *p);
}

int image_create_from_data(struct image *img, unsigned width, unsigned height, unsigned bpp, unsigned rowbytes, unsigned char *data) {

	assert(img != NULL);
	assert(width > 0);
	assert(height > 0);
	assert(bpp > 0 && bpp <= 32);

	img->xres=width;
	img->yres=height;
	img->bpp=bpp;
	/* use a default if rowbytes is 0 */
	img->rowbytes=rowbytes?rowbytes:calc_rowbytes(img->xres, img->bpp); /* pad to nearest byte */
	img->image_data=data;

	assert(img->rowbytes > 0);
	return 1;
}

int image_create(struct image *img, unsigned width, unsigned height, unsigned bpp, unsigned rowbytes) {
	void *buf;

	assert(img != NULL);
	assert(width > 0);
	assert(height > 0);
	assert(bpp > 0 && bpp <= 32);

	/* use a default if rowbytes is 0 */
	if(!rowbytes)
		rowbytes=calc_rowbytes(width, bpp); /* pad to nearest byte */

	buf=calloc(height, rowbytes);
	if(!buf) {
		PERROR("calloc()");
		return 0;
	}

	if(image_create_from_data(img, width, height, bpp, rowbytes, buf)) {
		return 1; /* success */
	}

	free(buf);
	return 0; /* failure */
}

void image_destroy(struct image *img) {
	if(!img) return;
	free(img->image_data);
	img->image_data=NULL;
}

/* loads a PNG as an 8bpp image.
 * img - pointer to an uninitialized structure (will be overwritten) */
int load_png(const char *filename, struct image *img) {
	FILE *f;
	png_structp png_ptr=NULL;
	png_infop info_ptr=NULL;
	png_bytep *row_pointers=NULL, image_data=NULL;
	int ret=0; /* default to failure */
	unsigned i;

	/* use this member to know if we should free the struct */
	img->image_data=0;

	/** Load the PNG **/
	f=fopen(filename, "rb");
	if(!f) {
		PERROR(filename);
		return 0; /* failure */
	}

	png_ptr=png_create_read_struct(PNG_LIBPNG_VER_STRING, 0, 0, 0);
	if(!png_ptr) {
		TRACE_MSG("png_create_read_struct failed");
		goto failure;
	}

	/* register error handler */
	if(setjmp(png_ptr->jmpbuf)) {
		/* oops .. there was an error */
		ERROR_MSG("caught error");
		goto failure;
	}

	png_init_io(png_ptr, f);

	info_ptr=png_create_info_struct(png_ptr);
	if(!info_ptr) {
		TRACE_MSG("png_create_info_struct failed");
		goto failure;
	}

	png_read_info(png_ptr, info_ptr);

	png_set_strip_alpha(png_ptr);

	/* strip 16-bit depths down to 8-bit */
	if(info_ptr->bit_depth>8) {
		png_set_strip_16(png_ptr);
	}

	/* update info with requested transformations */
	png_read_update_info(png_ptr, info_ptr);

	DEBUG("%s:%ux%u,%u\n",
		filename,
		(unsigned)info_ptr->width, (unsigned)info_ptr->height,
		(unsigned)info_ptr->bit_depth
	);

	image_create(img, info_ptr->width, info_ptr->height, info_ptr->bit_depth, info_ptr->rowbytes);

	/* allocate row_pointers and point to a big buffer */
	row_pointers=png_malloc(png_ptr, info_ptr->height * sizeof *row_pointers);

	for(i=0;i<info_ptr->height;i++) {
		row_pointers[i]=&img->image_data[i*img->rowbytes];
	}

	png_start_read_image(png_ptr);

	png_read_image(png_ptr, row_pointers);

	/* done with the image, read the rest of the PNG junk */
	png_read_end(png_ptr, info_ptr);

	ret=1; /* success */
failure:
	if(!ret)
		TRACE_MSG("Something bad happened");
	png_free(png_ptr, row_pointers);
	png_free(png_ptr, image_data);
	png_destroy_read_struct(&png_ptr, &info_ptr, 0);
	if(f) fclose(f);
	if(!ret && img->image_data) image_destroy(img);
	return ret;
}

/* load interlaced CHR data */
int load_chr(const char *filename, struct image *img, unsigned tile_width, unsigned tile_height, unsigned bpp, unsigned tiles_per_row) {
	FILE *f=NULL;
	unsigned height, width, i, total_tiles;
	long len;
	unsigned char *inbuf=NULL, *currtile;
	const size_t tilebytes=calc_rowbytes(tile_width, bpp)*tile_height;
	size_t res;

	assert(img != NULL);

	/* at least 1 tile per row */
	if(tiles_per_row<1) tiles_per_row=1;

	f=fopen(filename, "rb");
	if(!f) {
		PERROR(filename);
		return 0; /* failure */
	}

	len=filesize(filename, f);
	if(len<0) {
		fclose(f);
		return 0; /* failure */
	}

	/* check that there are an even number of tiles in the input file */
	total_tiles=len/tilebytes;
	if((len%tilebytes) != 0) {
		fprintf(stderr, "%s:file size %lu does contain an even number of %ux%u,%ubpp tiles\n", filename, len, tile_width, tile_height, bpp);
		goto failure;
	}

	/* calculate the number of tiles and how many we can fit on a sheet */
	width=tiles_per_row*tile_width;
	height=tile_height*((total_tiles+tiles_per_row-1)/tiles_per_row); /* round up */

	DEBUG("%s:tile_width = %d, tile_height = %d, tiles_per_row = %d, total_tiles = %d, bpp = %d, len = %ld, width = %d, height = %d\n", filename, tile_width, tile_height, tiles_per_row, total_tiles, bpp, len, width, height);

	/* allocate a buffer for CHR data */
	inbuf=calloc(1, len);
	if(!inbuf) {
		PERROR("malloc()");
		goto failure;
	}

	/* read the data */
	res=fread(inbuf, 1, len, f);
	if(ferror(f)) {
		PERROR(filename);
		goto failure;
	}
	if(res!=(size_t)len) {
		fprintf(stderr, "%s:short read\n", filename);
		goto failure;
	}

	/* output image */
	if(!image_create(img, width, height, bpp, 0)) {
		fprintf(stderr, "%s:Could not create image (%ux%u,%u).\n", filename, width, height, bpp);
		goto failure;
	}
	DEBUG("Loading image %ux%u,%ubpp\n", img->xres, img->yres, img->bpp);

	/* convert the planar input data into regular data */
	TRACE("tiles = %d\n", len/tilebytes);
	for(currtile=inbuf,i=0;i<len/tilebytes;i++,currtile+=tilebytes) {
		size_t ofs;
		unsigned g, x, y;

		// TRACE("inbuf=%p ofs=%zd i=%d tilebytes=%zd\n", inbuf, ofs, i, tilebytes);
		for(y=0;y<tile_height;y++) {
			for(x=0;x<tile_width;x++) {
				unsigned ix, iy; /* destination image x, y */
				g=get_pixel_planar(currtile, bpp, tilebytes, tile_width, x, y, 1);

				/* find offset in destination image */
				ix=(i%tiles_per_row)*tile_width;
				iy=(i/tiles_per_row)*tile_height;

				put_pixel(img, x+ix, y+iy, g);
			}
		}
	}

#if 0 /* diagnostic junk */
	TRACE("i0: %#x i1: %#x i2: %#x i3: %#x\n", inbuf[0], inbuf[1], inbuf[2], inbuf[3]);
	inbuf[0]=0x33;
	inbuf[1]=0x33;
	inbuf[2]=0x33;
	inbuf[3]=0x33;
	TRACE("g0: %#x g1: %#x g2: %#x g3: %#x\n",
		get_pixel(img, 0, 0),
		get_pixel(img, 1, 0),
		get_pixel(img, 2, 0),
		get_pixel(img, 3, 0)
	);
#endif

	free(inbuf);
	fclose(f);

	return 1; /* success */

failure:
	free(inbuf);
	fclose(f);

	return 0;
}

/* copy a tile area from img to dest */
static int copy_chr_tile(struct image *img, unsigned img_x, unsigned img_y, unsigned char *dest, unsigned tile_w, unsigned tile_h, unsigned bpp) {
	unsigned g, x, y, j;
	unsigned char *tmp;
	const size_t planar_rowbytes=calc_rowbytes(tile_w, 1);

	assert(img != NULL);
	assert(dest != NULL);

	if(!dest || !tile_w || !tile_h || !bpp ) {
		fprintf(stderr, "error: bad args\n");
		return 0; /* failure */
	}

	/* we must start as 0 for the bitmath to work */
	memset(dest, 0, tile_h*tile_w*bpp/8);

	for(y=0;y<tile_h;y++) {
		for(x=0;x<tile_w;x++) {
			g=get_pixel(img, img_x+x, img_y+y);
			for(j=0;j<bpp;j++) { /* loop through each bit plane */
				/* OR in the bit plane data for each bit of the pixel
				 * most-significant bit is the low order pixel (example 0th px starts on bit 7) */
				dest[x/8+y*planar_rowbytes+planar_rowbytes*tile_h*j]|=((g>>j)&1)<<((~x)%8);
			}
		}
	}

	return 1; /* success */
}

int save_chr(const char *filename, struct image *img, unsigned tile_w, unsigned tile_h) {
	const unsigned bpp=2; /* output bpp */
	const size_t tilebytes=tile_h*calc_rowbytes(tile_w, bpp);
	FILE *f=NULL;
	unsigned tx, ty; /* index into image for current tile */
	unsigned ix, iy; /* image x, y */
	unsigned rows, cols;
	unsigned res;
	unsigned char *outbuf; /* holds a single tile */

	assert(tile_w > 0 && tile_h > 0);

	/* figure out the area to iterate through for tiles */
	cols=img->xres/tile_w;
	rows=img->yres/tile_h;

	/* check that there was no remainder above, treat images that aren't exact as an error */
	if((img->xres%tile_w)!=0 && (img->yres%tile_h)!=0) {
		fprintf(stderr, "%s:image size %ux%u not a multiple of tiles size %ux%u\n", filename, img->xres, img->yres, tile_w, tile_h);
		return 0; /* failure */
	}

	f=fopen(filename, "wb");
	if(!f) {
		PERROR(filename);
		return 0; /* failure */
	}

	/* allocate a buffer for a single tile */
	outbuf=malloc(tilebytes);
	if(!outbuf) {
		PERROR("malloc()");
		goto failure;
	}

	for(ty=0;ty<rows;ty++) {
		for(tx=0;tx<cols;tx++) {
			/* copy part of the image to the tile */
			if(!copy_chr_tile(img, tx*tile_w, ty*tile_h, outbuf, tile_w, tile_h, bpp)) {
				goto failure;
			}
			/* output single tile to file */
			fwrite(outbuf, 1, tilebytes, f);
			if(ferror(f) || feof(f)) { /* check for errors */
				PERROR(filename);
				goto failure;
			}
		}
	}

	fclose(f);
	return 1; /* success */
failure:
	free(outbuf);
	fclose(f);
	return 0; /* failure */
}

static void user_error_fn(png_structp png_ptr __attribute__((unused)), png_const_charp error_msg) {
	fprintf(stderr, "ERROR:%s\n", error_msg);
	exit(EXIT_FAILURE); /* TODO: return back to save_png */
}

static void user_warning_fn(png_structp png_ptr __attribute__((unused)), png_const_charp warning_msg) {
	fprintf(stderr, "WARNING:%s\n", warning_msg);
}

/* apply the current time to the PNG output */
static void do_png_time(png_structp png_ptr, png_infop info_ptr) {
	time_t t;
	struct tm tm;
	png_time pt;

	/* for tIME */
	time(&t);
	tm=*gmtime(&t);
	pt.year=tm.tm_year+1900;
	pt.month=tm.tm_mon;
	pt.day=tm.tm_mday;
	pt.hour=tm.tm_hour;
	pt.minute=tm.tm_min;
	pt.second=tm.tm_sec;
	png_set_tIME(png_ptr, info_ptr, &pt);
}

int save_png(const char *filename, struct image *img) {
	FILE *f=NULL;
	unsigned y;
	unsigned char *p;
	png_structp png_ptr;
	png_infop info_ptr;
	png_bytep rowdata=NULL;

	f=fopen(filename, "wb");
	if(!f) {
		PERROR(filename);
		return 0; /* failure */
	}

	png_ptr=png_create_write_struct(PNG_LIBPNG_VER_STRING, NULL, user_error_fn, user_warning_fn);
	if(!png_ptr) goto failure1;

	info_ptr=png_create_info_struct(png_ptr);
	if(!info_ptr) {
		png_destroy_write_struct(&png_ptr, NULL);
		goto failure1;
	}

	if(setjmp(png_jmpbuf(png_ptr))) {
		fprintf(stderr, "%s:failure!\n", filename);
		goto failure2;
	}

	png_init_io(png_ptr, f);

	png_set_compression_level(png_ptr, Z_BEST_COMPRESSION);

	fprintf(stderr, "%s:writing %ux%u,%u\n", filename, img->xres, img->yres, img->bpp);

	png_set_IHDR(png_ptr, info_ptr, img->xres, img->yres, img->bpp, PNG_COLOR_TYPE_GRAY, PNG_INTERLACE_NONE, PNG_COMPRESSION_TYPE_DEFAULT, PNG_FILTER_TYPE_DEFAULT);

	/* for PNG_COLOR_TYPE_PALETTE:
	 * png_set_PLTE(png_ptr, info_ptr, pal, 2);
	 * png_set_tRNS(png_ptr, info_ptr, trans, 1, ??);
	 */

	/* warn editors not to muck with the values */
	png_set_sRGB_gAMA_and_cHRM(png_ptr, info_ptr, PNG_sRGB_INTENT_ABSOLUTE);

	do_png_time(png_ptr, info_ptr);

	png_write_info(png_ptr, info_ptr);

	/* convert data to a form png likes */
	rowdata=malloc(img->rowbytes);

	assert(img->rowbytes >= calc_rowbytes(img->xres, img->bpp)); /* verify the data structure makes sense */

	p=img->image_data;
	for(y=0;y<img->yres;y++) {
		png_write_row(png_ptr, img->image_data+img->rowbytes*y);
	}

	free(rowdata);

	png_write_end(png_ptr, info_ptr);

	png_destroy_write_struct(&png_ptr, &info_ptr);

	fclose(f);

	return 1; /* success */
failure2:
	png_destroy_write_struct(&png_ptr, &info_ptr);
failure1:
	free(rowdata);
	fprintf(stderr, "%s:could not create PNG\n", filename);
	fclose(f);
	return 0; /* failure */
}
