#
# Copyright (c) 2009-2010 Cisco Systems, Inc.  All rights reserved. 
# Copyright (c) 2012      Oak Ridge National Labs.  All rights reserved.
# $COPYRIGHT$
# 
# Additional copyrights may follow
# 
# $HEADER$
#

sources = \
	routed_rts.h \
	routed_rts.c \
	routed_rts_component.c

# Make the output library in this directory, and name it either
# mca_<type>_<name>.la (for DSO builds) or libmca_<type>_<name>.la
# (for static builds).

if MCA_BUILD_orte_routed_rts_DSO
component_noinst =
component_install = mca_routed_rts.la
else
component_noinst = libmca_routed_rts.la
component_install =
endif

mcacomponentdir = $(pkglibdir)
mcacomponent_LTLIBRARIES = $(component_install)
mca_routed_rts_la_SOURCES = $(sources)
mca_routed_rts_la_LDFLAGS = -module -avoid-version

noinst_LTLIBRARIES = $(component_noinst)
libmca_routed_rts_la_SOURCES = $(sources)
libmca_routed_rts_la_LDFLAGS = -module -avoid-version

