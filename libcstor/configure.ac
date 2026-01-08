#                                               -*- Autoconf -*-
# Process this file with autoconf to produce a configure script.

AC_PREREQ([2.63])
AC_INIT([libcstor], [1.0.0], [])
AC_CONFIG_MACRO_DIR([m4])
AC_CONFIG_AUX_DIR(config)
AC_CONFIG_HEADERS([config.h])
AC_CONFIG_SRCDIR([src])
AM_INIT_AUTOMAKE([-Wall -Werror foreign subdir-objects])
AC_PREFIX_DEFAULT([/usr])

MYCFLAGS="-Wall -Werror -g -D_GNU_SOURCE -D_BSD_SOURCE -D_DEFAULT_SOURCE"
AC_ARG_ENABLE(debug, AS_HELP_STRING([--enable-debug], [turns debug output on]), \
              MYCFLAGS="$MYCFLAGS -DDEBUG", MYCFLAGS="$MYCFLAGS")
CFLAGS="$CFLAGS $MYCFLAGS"

AC_CANONICAL_HOST
AM_MAINTAINER_MODE

AC_DISABLE_STATIC
# automake versions >1.12 seems to require AM_PROG_AR
m4_ifdef([AM_PROG_AR], [AM_PROG_AR], [AC_MSG_NOTICE([AM_PROG_AR is not available])])
LT_INIT

# Checks for programs.
AC_PROG_CC
AC_PATH_PROG([LDCONFIG], [ldconfig], no)

# Checks for header files.
AC_CHECK_HEADERS([arpa/inet.h netinet/in.h stdlib.h string.h sys/socket.h syslog.h unistd.h])

# Checks for typedefs, structures, and compiler characteristics.
AC_C_CONST

# Check OS.
case "${host}" in
i[[3456]]86-*-linux*) AC_DEFINE(LINUX_BUILD, 1, [OS is linux]) ac_cv_linux_build="yes" ;;
esac

AM_CONDITIONAL(LINUX_BUILD, test "$ac_cv_linux_build" = "yes")
AM_CONDITIONAL([RUN_LDCONFIG], [test "$LDCONFIG" != "no"])

AC_CONFIG_FILES([Makefile])
AC_OUTPUT
