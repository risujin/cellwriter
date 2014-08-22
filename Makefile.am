
INCLUDES = @GTK_CFLAGS@
LDADD = @GTK_LIBS@ -lX11 -lXtst
AM_CFLAGS = -DDATADIR=\"$(datadir)\" -DPKGDATADIR=\"$(pkgdatadir)\"

bin_PROGRAMS = cellwriter
cellwriter_SOURCES = \
        src/common.h \
        src/recognize.h \
        src/main.c \
        src/window.c \
        src/keyevent.c \
        src/cellwidget.c \
        src/recognize.c \
        src/stroke.c \
        src/options.c \
        src/averages.c \
        src/wordfreq.c \
        src/preprocess.c \
        src/keywidget.c \
        src/keys.h \
        src/singleinstance.c \
        src/statusicon.c

# We either build the new GtkStatusIcon for GTK 2.10 or libegg for GTK 2.8
if !GTKSTATUSICON
cellwriter_SOURCES += \
        src/libegg/eggtrayicon.c \
        src/libegg/eggtrayicon.h
endif !GTKSTATUSICON

# CellWriter tray and program icon
iconsdir = $(datadir)/icons/hicolor/scalable/apps
dist_icons_DATA = share/icons/hicolor/scalable/apps/cellwriter.svg

# CellWriter menu icon
menuicondir = $(datadir)/pixmaps
dist_menuicon_DATA = share/pixmaps/cellwriter.xpm

# Updates the icon cache so that the CellWriter icon will show up in the menu
gtk_update_icon_cache = gtk-update-icon-cache -f -t $(datadir)/icons/hicolor

install-data-hook:
	@-if test -z "$(DESTDIR)"; then \
		echo "Updating Gtk icon cache."; \
		$(gtk_update_icon_cache); \
	else \
		echo "*** Icon cache not updated.  After install, run this:"; \
		echo "***   $(gtk_update_icon_cache)"; \
	fi

# X11 .desktop program name and icon information
applicationsdir = $(datadir)/applications
dist_applications_DATA = cellwriter.desktop

# Initial profile and English word frequency file
shareddir = $(pkgdatadir)
dist_shared_DATA = \
        share/cellwriter/wordfreq \
        share/cellwriter/profile

# Manpage
cellwritermandir = $(mandir)/man1
dist_cellwriterman_DATA = cellwriter.1

# Distribute the testing application as an added bonus
EXTRA_DIST = \
        test/test.c \
        test/Makefile
