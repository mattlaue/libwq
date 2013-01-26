#noinst_LTLIBRARIES = libwq.la
lib_LTLIBRARIES = libwq.la

library_includedir=$(includedir)
library_include_HEADERS = wq.h

if MINGW
AM_CPPFLAGS = -Wall -Werror -DPTW32_STATIC_LIB
libwq_la_SOURCES = wq.c thread-win32.c
else
AM_CPPFLAGS = -Wall -Werror
libwq_la_SOURCES = wq.c time.c thread.c process.c
endif


libwq_la_LDFLAGS = -no-undefined -version-info $(WQ_SO_VERSION)
