#                                               -*- Autoconf -*-
# Process this file with autoconf to produce a configure script.

AC_PREREQ(2.64)
AC_INIT([CellWriter], [1.3.6], [risujin@risujin.org])
AM_INIT_AUTOMAKE
AC_CONFIG_SRCDIR([src/main.c])
AC_CONFIG_HEADER([config.h])

# URL launched when Help button is pressed
AC_ARG_VAR([CELLWRITER_URL], [URL for help website])
if test "x${CELLWRITER_URL}" == "x"; then
        AC_DEFINE(CELLWRITER_URL, "http://risujin.org/cellwriter",
                  [URL launched when the Help button is clicked])
fi

# Icon path for the program, installation paths are controlled in Makefile.am
AC_DEFINE(ICON_PATH, ["/icons/hicolor/scalable/apps/"],
          [Path to icon directory (with trailing slash)])

# Checks for programs
AC_PROG_CC
AC_PROG_INSTALL
AC_PROG_AWK
AC_PROG_CPP
AC_PROG_MKDIR_P

# Math library
AC_CHECK_LIB(m, atan2, [], [AC_ERROR(Math library not installed or invalid!)])

# GTK+2
PKG_CHECK_MODULES(GTK, gtk+-2.0 >= 2.8)
AC_SUBST(GTK_CFLAGS)
AC_SUBST(GTK_LIBS)

# Only use GtkStatusIcon if we have it and want it
AC_ARG_WITH(gtkstatusicon,
            AC_HELP_STRING([--without-gtkstatusicon],
	                   [use libegg status icon (GTK < 2.10)]))
if test x$with_gtkstatusicon != xno; then
        AC_CHECK_LIB(gtk-x11-2.0, gtk_status_icon_new_from_file, [],
                     [with_gtkstatusicon=no])
fi
if test x$with_gtkstatusicon = xno; then
        AC_MSG_NOTICE([using libegg tray icon])
        AC_DEFINE(USING_LIBEGG, [],
                  [Using LibEgg tray icon instead of GtkStatusIcon])
fi
AM_CONDITIONAL([GTKSTATUSICON], [test x$with_gtkstatusicon != xno])

# Xlib and Xtest extension
AC_PATH_X
AC_CHECK_LIB(X11, XChangeKeyboardMapping, [],
             [AC_ERROR(X11 library not found or outdated!
                  Please install the latest libx11-dev package.)])
AC_CHECK_LIB(Xtst, XTestFakeKeyEvent, [],
             [AC_ERROR(XTest library not found or outdated!
                  Please install the latest libxtst-dev package.)])

# Checks for header files
AC_HEADER_STDC

# Checks for typedefs, structures, and compiler characteristics
AC_C_CONST
AC_C_INLINE
AC_TYPE_SIZE_T

# Checks for library functions
AC_TYPE_SIGNAL
AC_FUNC_MALLOC
AC_FUNC_REALLOC
AC_CHECK_FUNCS([memmove memset sqrt strcasecmp])

AC_OUTPUT(Makefile)
