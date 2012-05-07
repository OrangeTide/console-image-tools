AC_INIT([console-image-tools], [0.3], [jon.mayo@gmail.com])
AC_CANONICAL_HOST
AM_INIT_AUTOMAKE([foreign dist-zip])
AC_PROG_CC
AC_PROG_RANLIB

VERSION="0.3"
AC_SUBST(VERSION)

PKG_PROG_PKG_CONFIG(0.1)

PKG_CHECK_MODULES(PNG, libpng,
	have_libpng="yes",
	have_libpng="no (usable libpng not found)"
)
AM_CONDITIONAL(HAVE_PNG, test "x$have_libpng" = "xyes")

AC_SUBST(PNG_CFLAGS)
AC_SUBST(PNG_LIBS)

AC_CHECK_HEADERS([sys/file.h])
AC_CONFIG_FILES([Makefile src/Makefile])
AC_OUTPUT
