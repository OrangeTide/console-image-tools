/*
 *
 */
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <setjmp.h>

#include "image.h"
#include "log.h"

#if defined(WIN32) || defined(__WIN32__)
#error Supply an implementation of getopt()
#else
#include <unistd.h>
#endif

/*
 * Defaults
 */
#define DEFAULT_OUTFILE "out.png"
#define DEFAULT_W 8
#define DEFAULT_H 8
#define DEFAULT_BPP 2
#define DEFAULT_COLUMNS 16

/* macro to turn a macro into a string */
#define _TOSTR(x) #x
#define TOSTR(x) _TOSTR(x)

/*
 * Globals
 */
struct prog_opts
{
	int verbose_fl;
	int in_bpp;
	int tile_w, tile_h;
	int tiles_per_row;
	const char *out_filename;
};

/*
 *
 */
static void
usage(void)
{
	fprintf(stderr, 
		"usage: chrtopng [-hv] [-b <bbp>] [-o <f>] [-t <NxM>] [-w <width>] [file ...]\n"
	);

	fprintf(stderr,
		"-b <bbp>    bits per pixel for input file (default " TOSTR(DEFAULT_BPP) ").\n"
		"-o <f>      output file (default '" DEFAULT_OUTFILE "').\n"
		"-t <NxM>    size of tile (default " TOSTR(DEFAULT_W) "x" TOSTR(DEFAULT_H) ").\n"
		"-w <width>  tiles per row (default " TOSTR(DEFAULT_COLUMNS) ").\n"
	);
}

/*
 *
 */
static int
parse_args(struct prog_opts *po, int argc, char **argv)
{
	int c;
	const char *tmp;
	char *endptr;

	while ((c=getopt(argc, argv, "hvb:o:t:w:"))>0)
	{
		switch (c)
		{
			case 'h':
				usage();
				return 0; /* treat as a failure */
			case 'v':
				po->verbose_fl++;
				break;
			case 'b':
				po->in_bpp=strtoul(optarg, &endptr, 10);
				if (*endptr)
				{
					fprintf(stderr, "Error: -b takes a number.\n");
					usage();
					return 0;
				}
				break;
			case 'o':
				po->out_filename=optarg;
				break;
			case 't':
				po->tile_w=strtoul(optarg, &endptr, 10);
				if (*endptr=='x' || *endptr=='X' || *endptr==',')
				{
					tmp=endptr+1;
					po->tile_h=strtoul(tmp, &endptr, 10);
					if (!*endptr)
					{
						break; /* success */
					}
				}
				/* it's a failure to get here */
				fprintf(stderr, "Error: -t takes a width and height.\n");
				usage();
				break;
			case 'w':
				po->tiles_per_row=strtoul(optarg, &endptr, 10);
				if (*endptr)
				{
					fprintf(stderr, "Error: -w takes a number.\n");
					usage();
					return 0;
				}
				break;
			default:
				usage();
				return 0; /* failure */
		}
	}
	return 1; /* success */
}

/*
 * main
 */
int
main(int argc, char **argv)
{
	struct prog_opts prog_opts;
	int i;
	struct image curr_img;

	/* configure defaults */
	prog_opts.verbose_fl=0;
	prog_opts.tile_w=DEFAULT_W;
	prog_opts.tile_h=DEFAULT_H;
	prog_opts.in_bpp=DEFAULT_BPP;
	prog_opts.tiles_per_row=DEFAULT_COLUMNS;
	prog_opts.out_filename=DEFAULT_OUTFILE;

	/* load command-line configuration */
	if (!parse_args(&prog_opts, argc, argv))
	{
		return EXIT_FAILURE;
	}

	TRACE("opts: %ux%u@%u '%s'\n", prog_opts.tile_w, prog_opts.tile_h, prog_opts.in_bpp, prog_opts.out_filename);

	if (optind==argc)
	{
		usage();	
		return EXIT_FAILURE;
	}
	else 
	{
		for (i=optind; i<argc; i++)
		{
			if (!load_chr(argv[i], &curr_img, prog_opts.tile_w, prog_opts.tile_h, prog_opts.in_bpp, prog_opts.tiles_per_row))
			{
				fprintf(stderr, "Could not load image '%s'\n", argv[i]);
				return EXIT_FAILURE;
			}
			if (!save_png(prog_opts.out_filename, &curr_img))
			{
				fprintf(stderr, "Could not write image '%s'\n", prog_opts.out_filename);
				return EXIT_FAILURE;
			}
			image_destroy(&curr_img);
		}
	}

	return EXIT_SUCCESS;
}
