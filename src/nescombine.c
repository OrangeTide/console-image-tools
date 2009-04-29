/* nescombine.c
 * combines Pasofami style CHR/PRG files into .nes file.
 *
 * PUBLIC DOMAIN - April 29, 2009 - Jon Mayo
 *
 */
#include <stdlib.h>
#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include <unistd.h>

#include "util.h"

#define PROG_NAME "nescombine"

#define INES_MAGIC "NES\x1a"

/* macro to turn a macro into a string */
#define _TOSTR(x) #x
#define TOSTR(x) _TOSTR(x)

/*
 * Types
 */
struct prog_opts {
	int verbose_fl;
	const char *out_filename;
	unsigned mapper, extended_mapper, ram_size;
};

static int write_ines(FILE *out_f, const char *filename, unsigned prg_rom_size, const void *prg_base, unsigned chr_rom_size, const void *chr_base, unsigned mapper, unsigned extended_mapper, unsigned ram_size) {
	size_t res;
	unsigned char ines_hdr[16], zeropad[16384];

	memset(ines_hdr, 0, sizeof ines_hdr);
	memcpy(ines_hdr+0, INES_MAGIC, strlen(INES_MAGIC));
	ines_hdr[4]=(prg_rom_size+16384-1)/16384; /* PRG segments - round up to 16K bounrdy */
	ines_hdr[5]=(chr_rom_size+8192-1)/8192; /* CHR segments - round up to 8K boundry */
	ines_hdr[6]=mapper;
	ines_hdr[7]=extended_mapper;
	ines_hdr[8]=(ram_size+8192-1)/8192; /* RAM segments - round up to 8K boundry */

	if(fwrite(ines_hdr, 1l, sizeof ines_hdr, out_f)!=sizeof ines_hdr) {
		fprintf(stderr, "Short write.\n");
		return 0;
	}

	fprintf(stderr, "%s:\n", filename);
	fprintf(stderr, "  PRG-ROM %ldK\n", ines_hdr[4]*16l);
	fprintf(stderr, "  CHR-ROM %ldK\n", ines_hdr[5]*8l);

	memset(zeropad, 0, sizeof zeropad); /* holds empty data used for padding */

	/* write PRG */
	res=fwrite(prg_base, 1, prg_rom_size, out_f);
	if(ferror(out_f)) goto error_f; /* IO error */
	/* pad PRG to 16k boundry */
	fwrite(zeropad, 1, (prg_rom_size%16384), out_f);
	if(ferror(out_f)) goto error_f; /* IO error */

	/* write CHR */
	res=fwrite(chr_base, 1, chr_rom_size, out_f);
	if(ferror(out_f)) goto error_f; /* IO error */
	/* pad CHR to 8k boundry */
	fwrite(zeropad, 1, (chr_rom_size%8192), out_f);
	if(ferror(out_f)) goto error_f; /* IO error */

	return 1; /* success */
error_f:
	perror(filename);
	return 0; /* IO error */
}

/* update len and data with new data */
static int file_append(const char *filename, size_t *len, unsigned char **data) {
	FILE *f;
	long buflen;
	unsigned char *tmp;
	size_t res;

	if(!len || !data || !filename) {
		fprintf(stderr, "%s():garbage args\n", __func__);
		return 0;
	}

	/* open the file */
	f=fopen(filename, "rb");
	if(!f) {
		perror(filename);
		return 0; /* failure */
	}

	/* first time into the loop try and use the entire filesize for the buffer */
	buflen=filesize(filename, f);
	if(buflen<0) {
		fclose(f);
		return 0; /* failure */
	}
	buflen++; /* read more than the file size to cause EOF to be detected in the fread loop */

	do {
		/* resize the buffer */
		tmp=realloc(*data, *len+buflen);
		if(!tmp) {
			perror("realloc()");
			fclose(f);
			return 0; /* failure - leave the old pointer alone */
		}
		*data=tmp; /* success - use the new pointer */

		/* load the data */
		res=fread(*data+*len, 1, buflen, f);
		if(ferror(f)) { /* check for errors */
			perror(filename);
			fclose(f);
			return 0; /* failure */
		}

		/* successfuly read the data - update the length */
		*len+=res;

		/* fprintf(stderr, "DEBUG:buflen=%ld res=%zd\n", buflen, res); */

		if(res<buflen) {
			break; /* treat short reads as EOF */
		}

		buflen=32768; /* if we loop we should try to read 32K at a time */
	} while(!feof(f));


	fclose(f);
	return 1; /* success */
}

/*
 * Display the usage message
 */
static void usage(void) {
	fprintf(stderr,
		"usage: " PROG_NAME "[-o <f>] [-m <M>] [-x <X>] [-r <sz>] [file ...]\n"
	);

	fprintf(stderr,
		"-o <f>      output file (default is basename of first file).\n"
		"-m <M>      mapper number (default is 0).\n"
		"-x <X>      extended mapper number (default is 0).\n"
		"-r <R>      RAM size (default is 0, rounded up in 8K chunks).\n"
	);
}

/*
 * Parse command-line arguments
 */
static int parse_args(struct prog_opts *po, int argc, char **argv) {
	int c;
	const char *tmp;
	char *endptr;

	while ((c=getopt(argc, argv, "hvo:m:r:x:"))>0) {
		switch (c) {
			case 'h':
				usage();
				return 0; /* treat as a failure */
			case 'v':
				po->verbose_fl++;
				break;
			case 'o':
				po->out_filename=optarg;
				break;
			case 'm':
				po->mapper=strtoul(optarg, &endptr, 10);
				if (*endptr) {
					fprintf(stderr, "Error: -m takes a decimal number.\n");
					usage();
					return 0;
				}
				break;
			case 'x':
				po->extended_mapper=strtoul(optarg, &endptr, 10);
				if (*endptr) {
					fprintf(stderr, "Error: -x takes a decimal number.\n");
					usage();
					return 0;
				}
				break;
			case 'r':
				po->ram_size=strtoul(optarg, &endptr, 0);
				if (*endptr) {
					fprintf(stderr, "Error: -r takes a number.\n");
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

int main(int argc, char **argv) {
	int i;
	FILE *out_f;
	char out_filename_tmp[512]; /* temp space for a filename */
	struct prog_opts po={0, NULL};
	unsigned char *prg_base=NULL, *chr_base=NULL;
	size_t prg_rom_size=0, chr_rom_size=0;


	if(!parse_args(&po, argc, argv)) {
		return EXIT_FAILURE;
	}

	if (argc==optind) {
		usage();
		return EXIT_FAILURE;
	}

	/* no outfile specified, use name of first file as the base name */
	if(!po.out_filename) {
		make_file_name(out_filename_tmp, sizeof out_filename_tmp, argv[optind], ".nes");
		po.out_filename=out_filename_tmp;
	}

	/* load all the files */
	for(i=optind;i<argc;i++) {
		const char *ext;

		ext=file_extension(argv[i]);
		if(ext && !strcasecmp(ext, ".chr")) {
			/* TODO: load CHR */
			if(!file_append(argv[i], &chr_rom_size, &chr_base)) {
				usage();
				return EXIT_FAILURE;
			}
		} else if(ext && !strcasecmp(ext, ".prg")) {
			/* TODO: load PRG */
			if(!file_append(argv[i], &prg_rom_size, &prg_base)) {
				usage();
				return EXIT_FAILURE;
			}
		} else {
			fprintf(stderr, "%s: unknown file extension.\n", argv[i]);
			usage();
			return EXIT_FAILURE;
		}
	}

	/* create the output */
	out_f=fopen(po.out_filename, "wb");
	if(!out_f) {
		perror(argv[i]);
		return EXIT_FAILURE;
	}

	if(!write_ines(out_f, po.out_filename, prg_rom_size, prg_base, chr_rom_size, chr_base, po.mapper, po.extended_mapper, po.ram_size)) {
		return EXIT_FAILURE;
	}

	fclose(out_f);
	return 0;
}
