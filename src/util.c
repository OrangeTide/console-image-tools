/* util.c
 */
#include <assert.h>
#include <errno.h>
#include <stddef.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h> 
#include "util.h"
#include "log.h"

/* return non-zero on success */
int make_file_name(char *dest, size_t max, const char *orig, const char *newext) {
	const char *oldext;
	/* copy string up to old extension, if any */
	oldext=strrchr(orig, '.');
	while(*orig && orig!=oldext) {
		if(max<=0) return 0;
		max--;
		*(dest++)=*(orig++);
	}
	/* copy the new extension */
	do {
		if(max<=0) return 0;
		max--;
		*(dest++)=*newext;
	} while(*(newext++));
	return 1;
}

/**
 * get size of file in bytes
 * @returns -1 on error, >=0 on success
 */
long filesize(const char *filename, FILE *f) {
	struct stat st;
	assert(f != NULL);	
	if(fstat(fileno(f), &st)!=0) {
		PERROR(filename);
		return -1;
	}
	
	return st.st_size;
}
