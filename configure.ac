AC_INIT([c3listener],[1.1],[nock@nocko.se])
AM_INIT_AUTOMAKE([foreign -Wall -Werror subdir-objects])
AC_CONFIG_SRCDIR([config.h.in])
AC_CONFIG_HEADERS([config.h])

# Checks for programs.
AC_PROG_CC_C99

# Checks for libraries.
AC_CHECK_LIB([config], [config_init])
AC_CHECK_LIB([bluetooth], [hci_open_dev])
AC_CHECK_LIB([avahi-common], [avahi_strerror])
AC_CHECK_LIB([avahi-client], [avahi_client_get_version_string])
AC_CHECK_LIB([curl], [curl_global_init])
AC_CHECK_LIB([json-c], [json_object_new_object])


# Checks for header files.
AC_CHECK_HEADERS([stdint.h stdlib.h string.h sys/socket.h unistd.h bluetooth/bluetooth.h json-c/json.h curl/curl.h libconfig.h avahi-common/simple-watch.h avahi-common/error.h avahi-common/malloc.h avahi-common/domain.h avahi-common/llist.h avahi-client/client.h avahi-client/lookup.h])

# Checks for typedefs, structures, and compiler characteristics.
AC_TYPE_INT8_T
AC_TYPE_UINT16_T
AC_TYPE_UINT8_T

# Checks for library functions.
AC_CHECK_FUNCS([gethostname])

AC_CONFIG_FILES([Makefile
                 src/Makefile])
AC_OUTPUT
