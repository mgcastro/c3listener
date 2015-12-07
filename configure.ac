AC_PREREQ([2.68])
AC_INIT([c3listener],[3.1],[nock@nocko.se])
AM_INIT_AUTOMAKE([-Wall -Werror foreign subdir-objects])
AC_CONFIG_SRCDIR([config.h.in])
AC_CONFIG_HEADERS([config.h])
AC_USE_SYSTEM_EXTENSIONS

# Gettext
AM_GNU_GETTEXT_VERSION([0.19])
AM_GNU_GETTEXT([external])

# Checks for programs.
AC_PROG_CC_C99

# Checks for libraries.
AC_CHECK_LIB([config], [config_init],[],[AC_MSG_ERROR('missing libconfig',1)])
AC_CHECK_LIB([bluetooth], [hci_open_dev],[],[AC_MSG_ERROR('missing libbluetooth',1)])

# Checks for header files.
AC_CHECK_HEADERS([libintl.h locale.h stdlib.h string.h sys/socket.h unistd.h])

# Checks for typedefs, structures, and compiler characteristics.
AC_C_INLINE
AC_TYPE_INT8_T
AC_TYPE_SIZE_T
AC_TYPE_UINT16_T
AC_TYPE_UINT8_T

# Checks for library functions.
AC_FUNC_MALLOC
AC_CHECK_FUNCS([gethostname memset setlocale])

AC_CONFIG_FILES([Makefile po/Makefile.in
                 src/Makefile])
AC_OUTPUT
