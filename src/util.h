#ifndef UTIL_H
#define UTIL_H
#include <stddef.h>
#include <stdio.h>
int make_file_name(char *dest, size_t max, const char *orig, const char *newext);
long filesize(const char *filename, FILE *f);
const char *file_extension(const char *filename);
#endif
