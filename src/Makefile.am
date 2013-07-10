#
#  Copyright (C) 2012 by Jules Colding <jcolding@gmail.com>.
#
#  All Rights Reserved.
#
# Copying and distribution of this file, with or without modification,
# are permitted in any medium without royalty provided the copyright
# notice and this notice are preserved.  This file is offered as-is,
# without any warranty.

noinst_PROGRAMS = pagesize
pagesize_SOURCES = pagesize.c
AM_CFLAGS = $(DISRUPTORC_CFLAGS)

all: pagesize pagesize.h

pagesize.h: pagesize
	$(srcdir)/pagesize


DISTCLEANFILES = $(BUILT_SOURCES) $(CLEAN_IN_FILES) Makefile.in Makefile
CLEANFILES = *~ pagesize pagesize.h
