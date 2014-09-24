AC_INIT([multi-config-test], 0.1)

AC_CONFIG_AUX_DIR([build])
AC_CONFIG_MACRO_DIR([build/autotools])
AC_CONFIG_SRCDIR([multi-context-test.c])
AM_CONFIG_HEADER([config.h])
AC_GNU_SOURCE

AC_PROG_CC

LT_LIB_M
AC_SUBST(LIBM)

AM_INIT_AUTOMAKE([1.9 foreign no-dist-gzip dist-xz tar-ustar])

m4_ifdef([AM_SILENT_RULES], [AM_SILENT_RULES([yes])])

MULTI_CONFIG_TEST_EXTRA_CFLAGS=""
MULTI_CONFIG_TEST_EXTRA_LIBS=""

dnl     ============================================================
dnl     Enable strict compiler flags
dnl     ============================================================

# use strict compiler flags only when building from git; the rules for
# distcheck will take care of turning this on when making a release
m4_define([maintainer_default], [yes])
AC_ARG_ENABLE(
  [maintainer-flags],
  [AC_HELP_STRING([--enable-maintainer-flags=@<:@no/yes/error@:>@],
                  [Use strict compiler flags @<:@default=]maintainer_default[@:>@])],
  [],
  enable_maintainer_flags=maintainer_default
)

MAINTAINER_COMPILER_FLAGS="-Wall -Wcast-align -Wuninitialized
                           -Wno-strict-aliasing -Wempty-body -Wformat
                           -Wformat-security -Winit-self -Wundef
                           -Wdeclaration-after-statement
                           -Wpointer-arith -Wmissing-declarations"

AS_CASE(
  [$enable_maintainer_flags],
  [yes],
  [
    AS_COMPILER_FLAGS([MAINTAINER_CFLAGS], [$MAINTAINER_COMPILER_FLAGS])
  ],
  [no],
  [
  ],
  [error],
  [
    MAINTAINER_COMPILER_FLAGS="$MAINTAINER_COMPILER_FLAGS -Werror"
    AS_COMPILER_FLAGS([MAINTAINER_CFLAGS], [$MAINTAINER_COMPILER_FLAGS])
  ],
  [*],
  [AC_MSG_ERROR([Invalid option for --enable-maintainer-flags])]
)

# strip leading spaces
MULTI_CONFIG_TEST_EXTRA_CFLAGS="$MULTI_CONFIG_TEST_EXTRA_CFLAGS ${MAINTAINER_CFLAGS#*  }"

AC_SUBST(MULTI_CONFIG_TEST_EXTRA_CFLAGS)
AC_SUBST(MULTI_CONFIG_TEST_EXTRA_LIBS)

PKG_CHECK_MODULES(EPOXY, [epoxy])
PKG_CHECK_MODULES(GL, [gl])
PKG_CHECK_MODULES(X11, [x11])

AC_CONFIG_FILES([
        Makefile
        build/Makefile
])

AC_OUTPUT
