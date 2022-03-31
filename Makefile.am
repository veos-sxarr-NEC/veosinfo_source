ACLOCAL_AMFLAGS = -I m4
lib_LTLIBRARIES = libveosinfo.la
libveosinfo_la_SOURCES = \
	veosinfo.c \
	veosinfo.h \
	veosinfo_internal.h \
	veosinfo_comm.h \
	ve_sock.c \
	ve_sock.h \
	veosinfo_log.c \
	veosinfo_log.h \
	veos_RPM.pb-c.c\
	veos_RPM.pb-c.h
libveosinfo_la_CFLAGS = -g -Wall -fPIC -I${prefix}/include
libveosinfo_la_LDFLAGS = -version-info 1:0:0
libveosinfo_la_LIBADD = -lveproductinfo
libveosinfo_la_includedir = $(includedir)/veosinfo
libveosinfo_la_include_HEADERS = veosinfo.h veosinfo_log.h
EXTRA_DIST = debian
