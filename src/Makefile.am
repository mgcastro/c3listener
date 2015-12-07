bin_PROGRAMS = c3listener
c3listener_SOURCES = main.c gettext.h c3listener.h ble.c udp.c kalman.h kalman.c report.h report.c hash.h hash.c beacon.h beacon.c time_util.h time_util.c log.h log.c
c3listener_LDADD = $(LIBINTL)
AM_CPPFLAGS = -DLOCALEDIR=\"$(localedir)\" -DSYSCONFDIR=\"${sysconfdir}\" -Wall
