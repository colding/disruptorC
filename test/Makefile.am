#
#  Copyright (C) 2012 by Jules Colding <jcolding@gmail.com>.
#
#  All Rights Reserved.
#
# Copying and distribution of this file, with or without modification,
# are permitted in any medium without royalty provided the copyright
# notice and this notice are preserved.  This file is offered as-is,
# without any warranty.

noinst_PROGRAMS = correctness performance

correctness_LDFLAGS = -all-static
performance_LDFLAGS = -all-static

correctness_SOURCES = correctness.c
performance_SOURCES = performance.c

AM_CFLAGS = $(DISRUPTORC_CFLAGS)

DISTCLEANFILES = $(BUILT_SOURCES) $(CLEAN_IN_FILES) Makefile.in Makefile
CLEANFILES = *~
