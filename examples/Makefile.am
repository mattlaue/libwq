LDADD = $(top_srcdir)/src/libwq.la

if MINGW
AM_CPPFLAGS = -I$(top_srcdir)/src -Werror -Wall -DPTW32_STATIC_LIB
EXTRA_PROGRAMS = hello thread
else
AM_CPPFLAGS = -I$(top_srcdir)/src -Werror -Wall
EXTRA_PROGRAMS = hello thread process
endif

noinst_PROGRAMS = $(EXTRA_PROGRAMS)
