# - Find LibEvhtp (replacement for evhttp in libevent)
# This module defines
# LIBEVHTP_INCLUDE_DIR, where to find libevhtp headers
# LIBEVHTP_LIB, libevhtp libraries
# LIBEVHTP_FOUND, If false, do not try to use libevhtp

set(LibEvent_EXTRA_PREFIXES /usr/local /opt/local "$ENV{HOME}")
foreach(prefix ${LIBEVHTP_EXTRA_PREFIXES})
  list(APPEND LIBEVHTP_INCLUDE_PATHS "${prefix}/include")
  list(APPEND LIBEVHTP_LIB_PATHS "${prefix}/lib")
endforeach()

find_path(LIBEVHTP_INCLUDE_DIR event.h PATHS ${LIBEVHTP_INCLUDE_PATHS})
find_library(LIBEVHTP_LIB NAMES evhtp PATHS ${LIBEVHTP_LIB_PATHS})

if (LIBEVHTP_LIB AND LIBEVHTP_INCLUDE_DIR)
  set(LIBEVHTP_FOUND TRUE)
  set(LIBEVHTP_LIB ${LIBEVHTP_LIB})
else ()
  set(LIBEVHTP_FOUND FALSE)
endif ()

if (LIBEVHTP_FOUND)
  if (NOT LIBEVHTP_FIND_QUIETLY)
    message(STATUS "Found libevhtp: ${LIBEVHTP_LIB}")
  endif ()
else ()
  if (LIBEVHTP_FIND_REQUIRED)
    message(FATAL_ERROR "Could NOT find libevhtp.")
  endif ()
  message(STATUS "libevhtp NOT found.")
endif ()

mark_as_advanced(
    LIBEVHTP_LIB
    LIBEVHTP_INCLUDE_DIR
  )
