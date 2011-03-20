#                                               -*- Autoconf -*-
# Process this file with autoconf to produce a configure script.

AC_INIT([writenfi],[0.1],[enigma2-devel@lists.elitedvb.net])
AC_CONFIG_SRCDIR([src/writenfi.c])
AC_CONFIG_HEADERS([writenfi_config.h])
AM_INIT_AUTOMAKE([dist-bzip2 foreign no-define tar-pax])

# Silent rules are available since 1.11, but older versions
# are still in use. So don't use them unconditionally.
m4_ifdef([AM_SILENT_RULES], [AM_SILENT_RULES([yes])])
 
AC_USE_SYSTEM_EXTENSIONS
AC_SYS_LARGEFILE

# Checks for programs.
AC_PROG_CC

# Checks for libraries.

# Checks for header files.
AC_CHECK_HEADERS([arpa/inet.h fcntl.h stdlib.h string.h sys/ioctl.h unistd.h])

# Checks for typedefs, structures, and compiler characteristics.
AC_TYPE_SIZE_T

# Checks for library functions.
AC_FUNC_MMAP
AC_CHECK_FUNCS([memset])

AC_CONFIG_FILES([
Makefile
src/Makefile
])
AC_OUTPUT
