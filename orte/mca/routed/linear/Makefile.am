#
# Copyright (c) 2007      Los Alamos National Security, LLC.
#                         All rights reserved. 
# Copyright (c) 2009      High Performance Computing Center Stuttgart, 
#                         University of Stuttgart.  All rights reserved.
# Copyright (c) 2010      Cisco Systems, Inc.  All rights reserved.
# $COPYRIGHT$
# 
# Additional copyrights may follow
# 
# $HEADER$
#

EXTRA_DIST = .windows

sources = \
	routed_linear.h \
	routed_linear.c \
	routed_linear_component.c

# Make the output library in this directory, and name it either
# mca_<type>_<name>.la (for DSO builds) or libmca_<type>_<name>.la
# (for static builds).

if MCA_BUILD_orte_routed_linear_DSO
component_noinst =
component_install = mca_routed_linear.la
else
component_noinst = libmca_routed_linear.la
component_install =
endif

mcacomponentdir = $(pkglibdir)
mcacomponent_LTLIBRARIES = $(component_install)
mca_routed_linear_la_SOURCES = $(sources)
mca_routed_linear_la_LDFLAGS = -module -avoid-version

noinst_LTLIBRARIES = $(component_noinst)
libmca_routed_linear_la_SOURCES = $(sources)
libmca_routed_linear_la_LDFLAGS = -module -avoid-version

