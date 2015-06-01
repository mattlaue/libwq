AC_INIT([wq], [1.0.0])
AC_PREREQ([2.67])
AM_INIT_AUTOMAKE([foreign -Wall -Werror])

AC_CONFIG_HEADERS([config.h])
AC_CONFIG_MACRO_DIR([m4])
AC_PROG_CC
AC_PROG_LIBTOOL

m4_ifdef([AM_PROG_AR], [AM_PROG_AR]) dnl Workaround for Automake 1.12

AC_CACHE_VAL(lt_cv_deplibs_check_method,
    [lt_cv_deplibs_check_method=pass_all])

MINGW32=no
DARWIN=no

case "${host_os}" in
  *mingw32* )
    MINGW32=yes
    ;;
  darwin* )
    DARWIN=yes    
    ;;
esac
AM_CONDITIONAL(MINGW, [test "x$MINGW32" = "xyes"])

AC_CHECK_LIB([pthread], [pthread_create],,
    AC_MSG_ERROR([*** Please install libpthread on your system ***]))


dnl check libraries
if test "x$MINGW32" = "xyes"; then
    AC_LIBTOOL_WIN32_DLL
else
    if test "x$DARWIN" != "xyes"; then
        AC_CHECK_LIB([rt], [clock_gettime],,
                AC_MSG_ERROR([*** Please install librt on your system ***]))
    fi
fi

AM_CONDITIONAL(WQ_ENABLE_DOC, false)

ac_enable_examples=yes
AC_ARG_ENABLE(examples,
     AS_HELP_STRING([--disable-examples], [Disable examples]),
     [ ac_enable_examples=$enableval ], [ ac_enable_examples=yes] )
AM_CONDITIONAL(WQ_ENABLE_EXAMPLES, [test "x${ac_enable_examples}" != "xno"])

dnl Debug version
AC_ARG_ENABLE(debug,
AS_HELP_STRING([--enable-debug],
               [enable debugging, default: no]),
[case "${enableval}" in
             yes) debug=true ;;
             no)  debug=false ;;
             *)   AC_MSG_ERROR([bad value ${enableval} for --enable-debug]) ;;
esac],
[debug=false])

AM_CONDITIONAL(WQ_ENABLE_DEBUG, test x"$debug" = x"true")

AC_SUBST([WQ_SO_VERSION], [1:0:1])

AC_OUTPUT([
    Makefile
    src/Makefile
    doc/Makefile
    examples/Makefile
])
AC_OUTPUT

