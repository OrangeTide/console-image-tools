/*
 * Copyright 2012 Jon Mayo
 *
 * Permission to use, copy, modify, and distribute this software for any
 * purpose with or without fee is hereby granted, provided that the above
 * copyright notice and this permission notice appear in all copies.
 *
 * THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
 * WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR
 * ANY SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
 * WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN
 * ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF
 * OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
 */

#include <assert.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>

// TODO: rewrite these macros
#define BYTE3_TO_UINT(bp) \
	(((unsigned int)(bp)[0] << 16) & 0x00ff0000) | \
	(((unsigned int)(bp)[1] << 8) & 0x0000ff00) | \
	((unsigned int)(bp)[2] & 0x000000ff)

#define BYTE2_TO_UINT(bp) \
	(((unsigned int)(bp)[0] << 8) & 0xff00) | \
	((unsigned int) (bp)[1] & 0x00ff)

#define error(...) do { \
		if (verbose_level > 0) \
			fprintf(stderr, __VA_ARGS__); \
	} while(0)

#define verbose(...) do { \
		if (verbose_level > 1) \
			fprintf(stderr, __VA_ARGS__); \
	} while(0)

#ifndef NDEBUG
# define debug(...) do { \
		if (verbose_level > 2) \
			fprintf(stderr, __VA_ARGS__); \
	} while(0)
#else
/* disable debug messages */
# define debug(...)
#endif

enum patch_type {
	PATCH_RLE,
	PATCH_BIN,
};

struct patch {
	enum patch_type type;
	unsigned offset;
	unsigned len;
	unsigned char *data;
	struct patch *next;
};

static int verbose_level = 1;

static void new_patch(struct patch **head, enum patch_type type,
	unsigned offset, unsigned len, unsigned char *data)
{
	struct patch *new;

	while (*head && (*head)->offset < offset)
		head = &(*head)->next;

	new = calloc(1, sizeof(*new));
	new->type = type;
	new->offset = offset;
	new->len = len;
	new->data = data;
	new->next = *head;
	*head = new;
}

static void free_patch(struct patch *head)
{
	while (head) {
		struct patch *curr = head;

		head = head->next;
		free(curr->data);
		free(curr);
	}
}

static int read_record(const char *patchfile, int fd, int *errout,
	struct patch **patchhead)
{
	int cnt;
	unsigned char offset[3];
	unsigned char size[2];
	unsigned size_val;
	unsigned offset_val;

	cnt = read(fd, offset, sizeof(offset));
	if (cnt < (int)sizeof(offset))
		goto trunc_detected;
	if (!memcmp(offset, "EOF", sizeof(offset))) {
		debug("EOF RECORD detected\n");
		*errout = 0;
		return 0;
	}
	offset_val = BYTE3_TO_UINT(offset);

	cnt = read(fd, size, sizeof(size));
	if (cnt < (int)sizeof(size))
		goto trunc_detected;
	size_val = BYTE2_TO_UINT(size);

	debug("RECORD! offset=%d size=%d\n", offset_val, size_val);

	if (size_val) { /* binary patch */
		unsigned char *data;

		data = malloc(size_val);
		cnt = read(fd, data, size_val);
		if (cnt != (int)size_val) {
			free(data);
			goto trunc_detected;
		}

		new_patch(patchhead, PATCH_BIN, offset_val, size_val, data);
	} else { /* RLE patch */
		unsigned char rlesize[2];
		unsigned rlesize_val;
		unsigned char *value;

		cnt = read(fd, rlesize, sizeof(rlesize));
		if (cnt < (int)sizeof(rlesize))
			goto trunc_detected;
		rlesize_val = BYTE2_TO_UINT(rlesize);

		value = malloc(1);
		cnt = read(fd, &value, 1);
		if (cnt != 1) {
			free(value);
			goto trunc_detected;
		}

		new_patch(patchhead, PATCH_RLE, offset_val, rlesize_val, value);
	}

	*errout = 0;
	return -1;
trunc_detected:
	error("%s: Truncated file detected\n", patchfile);
	*errout = 1;
	return 0;
}

static int load_patch(const char *patchfile, struct patch **patchhead)
{
	int fd;
	unsigned char header[5];
	int cnt;
	int e;

	assert(patchfile != NULL);
	fd = open(patchfile, O_RDONLY);
	if (fd < 0) {
		perror(patchfile);
		return -1;
	}

	cnt = read(fd, header, sizeof(header));
	if (cnt < (int)sizeof(header))
		goto out_perror;
	if (memcmp(header, "PATCH", sizeof(header))) {
		error("%s: Header signature invalid\n", patchfile);
		goto out_close;
	}

	e = 0;
	while (read_record(patchfile, fd, &e, patchhead)) ;

	if (e) {
		error("%s: Error reading patch file\n", patchfile);
		goto out_close;
	}

	close(fd);
	return 0;
out_perror:
	perror(patchfile);
out_close:
	close(fd);
	return -1;
}

static int discard(const char *infile, int infd, size_t bytes)
{
	char buf[512];
	int len;

	debug("%s:bytes=%zd\n", __func__, bytes);
	while (bytes > 0) {
		len = bytes > sizeof(buf) ? sizeof(buf) : bytes;
		len = read(infd, buf, len);
		if (len < 0) {
			perror(infile);
			return -1;
		}
		bytes -= len;
	}

	return 0;
}

static int copy_data(const void *data, const char *outfile, int outfd,
	size_t bytes)
{
	int res;
	int ofs;

	debug("%s:bytes=%zd\n", __func__, bytes);
	ofs = 0;
	while (bytes > 0) {
		res = write(outfd, data + ofs, bytes);
		if (res < 0) {
			perror(outfile);
			return -1;
		}
		// debug("%s:wrote %zd (@%d)\n", __func__, res, (int)lseek(outfd, SEEK_CUR, 0));
		bytes -= res;
		ofs += res;
	}
	return 0;
}

static int copy_file(const char *infile, int infd, const char *outfile,
	int outfd, size_t bytes)
{
	char buf[512];
	int len;

	debug("%s:bytes=%zd\n", __func__, bytes);
	while (bytes > 0) {
		len = bytes > sizeof(buf) ? sizeof(buf) : bytes;
		len = read(infd, buf, len);
		if (len < 0) {
			perror(infile);
			return -1;
		}
		if (copy_data(buf, outfile, outfd, len))
			return -1;
		bytes -= len;
	}

	return 0;
}

static int copy_file_remaining(const char *infile, int infd,
	const char *outfile, int outfd)
{
	char buf[512];
	int len;

	debug("%s:to EOF\n", __func__);
	while ((len = read(infd, buf, sizeof(buf)))) {
		if (len < 0) {
			perror(infile);
			return -1;
		}
		if (copy_data(buf, outfile, outfd, len))
			return -1;
		//debug("%s:offset=%d\n", __func__, (int)lseek(outfd, SEEK_CUR, 0));
	}

	return 0;
}

static int fill_data(unsigned char fill, const char *outfile, int outfd,
	size_t bytes)
{
	char buf[512];
	int len;

	debug("%s:bytes=%zd\n", __func__, bytes);
	memset(buf, fill, sizeof(buf));
	while (bytes) {
		len = bytes > sizeof(buf) ? sizeof(buf) : bytes;
		if (copy_data(buf, outfile, outfd, len))
			return -1;
		bytes -= len;
	}
	return 0;
}

static int apply_patch(struct patch *patchhead, const char *infile, int infd,
	const char *outfile, int outfd)
{
	unsigned prev_offset = 0;
	struct patch *curr;
	int e;
	size_t bytes;

	for (curr = patchhead; curr; curr = curr->next) {
		/* check file position */
		// debug("%s:offset=%d\n", __func__, lseek(outfd, SEEK_CUR, 0));
		// TODO: fix this assert assert(lseek(outfd, SEEK_CUR, 0) == prev_offset);

		/* copy data from file before a patch point */
		verbose("WRITE %d-%d\n", prev_offset, curr->offset - 1);
		bytes = curr->offset - prev_offset;
		e = copy_file(infile, infd, outfile, outfd, bytes);
		if (e)
			return -1;

		/* copy patch data */
		prev_offset = curr->offset + curr->len;
		verbose("PATCH %d-%d\n", curr->offset, prev_offset - 1);
		e = discard(infile, infd, curr->len);
		if (e)
			return -1;
		switch(curr->type) {
		case PATCH_BIN:
			e = copy_data(curr->data, outfile, outfd, curr->len);
			break;
		case PATCH_RLE:
			e = fill_data(*curr->data, outfile, outfd, curr->len);
			break;
		}
		if (e)
			return -1;
	}
	/* copy remaining portion of file */
	verbose("WRITE %d-EOF\n", prev_offset);
	e = copy_file_remaining(infile, infd, outfile, outfd);
	if (e)
		return -1;

	return 0;
}

static int patch(const char *patchfile, const char *infile,
	const char *outfile)
{
	int e;
	int infd, outfd;
	struct patch *patchhead = NULL;

	e = load_patch(patchfile, &patchhead);
	if (e) {
		error("%s: Error loading patch\n", patchfile);
		return -1;
	}

	infd = open(infile, O_RDONLY);
	if (infd < 0) {
		perror(infile);
		return -1;
	}

	outfd = open(outfile, O_CREAT | O_EXCL | O_WRONLY, 0666);
	if (outfd < 0) {
		perror(outfile);
		goto out_close_in;
	}

	e = apply_patch(patchhead, infile, infd, outfile, outfd);

	close(outfd);
	close(infd);
	free_patch(patchhead);
	return e;
out_close_in:
	close(infd);
	free_patch(patchhead);
	return -1;
}

int main(int argc, char **argv)
{
	const char *patchfile = NULL;
	const char *infile = NULL;
	const char *outfile = NULL;
	int e;
	int opt;

	while ((opt = getopt(argc, argv, "hvq")) != -1) {
		switch (opt) {
		default:
		case 'h':
usage:
			fprintf(stderr, "Usage: %s [-hvq] patchfile in out\n",
				argv[0]);
			return 1;
		case 'v':
			verbose_level++;
			break;
		case 'q':
			verbose_level = 0;
			break;
		}
	}

	if ((optind + 2) >= argc)
		goto usage;

	patchfile = argv[optind];
	infile = argv[optind + 1];
	outfile = argv[optind + 2];

	e = patch(patchfile, infile, outfile);
	if (e) {
		error("%s: Failed to patch\n", outfile);
		return 1;
	}

	return 0;
}
