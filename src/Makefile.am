AUTOMAKE_OPTIONS = gnu
LDADD = @PNG_LIBS@
AM_CPPFLAGS = @PNG_CFLAGS@ -DNTRACE -DNDEBUG
bin_PROGRAMS = pngtochr chrtopng nessplit nescombine
pngtochr_SOURCES = pngtochr.c image.c util.c
chrtopng_SOURCES = chrtopng.c image.c util.c
nessplit_SOURCES = nessplit.c util.c
nescombine_SOURCES = nescombine.c util.c
