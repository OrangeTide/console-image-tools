/* nessplit.c
 * splits iNES format ROM files(.nes) to Pasofami style CHR/PRG files.
 *
 * PUBLIC DOMAIN - Novemeber 20, 2007 - Jon Mayo
 *
 * BUGS:
 * cannot split .nes files that have trainers
 *
 */
/* iNES Format (.NES)
 *  +--------+------+------------------------------------------+
 *  | Offset | Size | Content(s)                               |
 *  +--------+------+------------------------------------------+
 *  |   0    |  3   | 'NES'                                    |
 *  |   3    |  1   | $1A                                      |
 *  |   4    |  1   | 16K PRG-ROM page count                   |
 *  |   5    |  1   | 8K CHR-ROM page count                    |
 *  |   6    |  1   | ROM Control Byte #1                      |
 *  |        |      |   %####vTsM                              |
 *  |        |      |    |  ||||+- 0=Horizontal Mirroring      |
 *  |        |      |    |  ||||   1=Vertical Mirroring        |
 *  |        |      |    |  |||+-- 1=SRAM enabled              |
 *  |        |      |    |  ||+--- 1=512-byte trainer present  |
 *  |        |      |    |  |+---- 1=Four-screen VRAM layout   |
 *  |        |      |    |  |                                  |
 *  |        |      |    +--+----- Mapper # (lower 4-bits)     |
 *  |   7    |  1   | ROM Control Byte #2                      |
 *  |        |      |   %####0000                              |
 *  |        |      |    |  |                                  |
 *  |        |      |    +--+----- Mapper # (upper 4-bits)     |
 *  |  8-15  |  8   | $00                                      |
 *  | 16-..  |      | Actual 16K PRG-ROM pages (in linear      |
 *  |  ...   |      | order). If a trainer exists, it precedes |
 *  |  ...   |      | the first PRG-ROM bank.                  |
 *  | ..-EOF |      | CHR-ROM pages (in ascending order).      |
 *  +--------+------+------------------------------------------+
 */

#include <stdlib.h>
#include <stdio.h>
#include <stdint.h>
#include <string.h>

#include "util.h"

struct ines_hdr {
	size_t prg_rom_size;
	size_t chr_rom_size;
	unsigned trainer_fl;
	unsigned mapper, mirroring;
};

static int verbose_fl=1;

static int read_ines_hdr(FILE *in, struct ines_hdr *hdr) {
	uint8_t buf[16];

	if(fread(buf, 1l, sizeof buf, in)!=sizeof buf) {
		fprintf(stderr, "Truncated file.\n");
		return 0;
	}

	if(buf[0]!='N' || buf[1]!='E' || buf[2]!='S' || buf[3]!=0x1a) {
		fprintf(stderr, "Not an iNES file.\n");
		return 0;
	}

	if(hdr) {
		hdr->prg_rom_size=buf[4]*16384l;
		hdr->chr_rom_size=buf[5]*8192l;
		hdr->trainer_fl=(buf[6]>>2)&1; /* 512-byte trainer is before the PRG ROM */
		hdr->mapper=((buf[6]>>4)&15)|(buf[7]&0xf0);
		hdr->mirroring=buf[6]&3;
	}

	if(verbose_fl>1)
		fprintf(stderr, "  header: %02hhx %02hhx %02hhx %02hhx\n", buf[4], buf[5], buf[6], buf[7]);
	if(hdr) {
		fprintf(stderr, "  PRG-ROM %ldK\n", hdr->prg_rom_size/1024);
		fprintf(stderr, "  CHR-ROM %ldK\n", hdr->chr_rom_size/1024);
		fprintf(stderr, "  Mapper=%d\n", hdr->mapper);
		fprintf(stderr, "  Trainer=%d\n", hdr->trainer_fl);
		fprintf(stderr, "  Mirroring=%d\n", hdr->mirroring);
		fprintf(stderr, "  Battery=%d\n", (buf[6]>>1)&1);
		fprintf(stderr, "  4-screen VRAM=%d\n", (buf[6]>>3)&1);
	}

	return 1;
}

static int dump_bin(FILE *in, const char *out_filename, size_t len) {
	FILE *out;
	char *buf;
	int res;
	out=fopen(out_filename, "wb");
	if(!out) {
		perror(out_filename);
		return 0;
	}
	buf=malloc(len);
	res=fread(buf, 1l, len, in);
	if(res<0) {
		perror(out_filename);
		free(buf);
		return 0;
	}
	if((unsigned)res!=len) {
		fprintf(stderr, "%s:short read while copying.\n", out_filename);
		free(buf);
		return 0;
	}
	res=fwrite(buf, 1l, len, out);
	if(res<0) {
		perror(out_filename);
		free(buf);
		return 0;
	}
	if((unsigned)res!=len) {
		fprintf(stderr, "%s:short write while copying.\n", out_filename);
		free(buf);
		return 0;
	}

	free(buf);
	fprintf(stderr, "Wrote %s\n", out_filename);
	return 1;
}

int main(int argc, char **argv) {
	int i;
	FILE *f;
	struct ines_hdr hdr;
	char chr_filename[512], prg_filename[512];

	if (argc==1) {
		fprintf(stderr, "usage: nessplit [file.nes ...]\nSplits iNES files into CHR and PRG.\n");
		return EXIT_FAILURE;
	} else for(i=1;i<argc;i++) {
		printf("** %s\n", argv[i]);
		f=fopen(argv[i], "rb");
		if(!f) {
			perror(argv[i]);
			return EXIT_FAILURE;
		}

		if(read_ines_hdr(f, &hdr)) {
			if(hdr.trainer_fl) {
				fprintf(stderr, "Cannot dump trainers.\n");
				goto done;
			}
			if(hdr.prg_rom_size) {
				if(!make_file_name(prg_filename, sizeof prg_filename, argv[i], ".prg")) {
					fprintf(stderr, "Cannot output PRG file.\n");
					goto done;
				}
				if(!dump_bin(f, prg_filename, hdr.prg_rom_size)) {
					fprintf(stderr, "Error outputing PRG file.\n");
					goto done;
				}
			}
			if(hdr.chr_rom_size) {
				if(!make_file_name(chr_filename, sizeof chr_filename, argv[i], ".chr")) {
					fprintf(stderr, "Cannot output CHR file.\n");
					goto done;
				}
				if(!dump_bin(f, chr_filename, hdr.chr_rom_size)) {
					fprintf(stderr, "Error outputing CHR file.\n");
					goto done;
				}
			}
		}

		done:
		fclose(f);
	}
	return 0;
}
