#
# Copyright (c) 2004-2005 The Trustees of Indiana University and Indiana
#                         University Research and Technology
#                         Corporation.  All rights reserved.
# Copyright (c) 2004-2015 The University of Tennessee and The University
#                         of Tennessee Research Foundation.  All rights
#                         reserved.
# Copyright (c) 2004-2005 High Performance Computing Center Stuttgart, 
#                         University of Stuttgart.  All rights reserved.
# Copyright (c) 2004-2005 The Regents of the University of California.
#                         All rights reserved.
# Copyright (c) 2006-2007 Sun Microsystems, Inc.  All rights reserved.
# Copyright (c) 2012      Los Alamos National Security, Inc.  All rights reserved.
# $COPYRIGHT$
# 
# Additional copyrights may follow
# 
# $HEADER$
#

# Use the Open MPI-provided wrapper compilers.  Note that gmake
# requires the CXX macro, while other versions of make (such as Sun's
# make) require the CCC macro.

CC = mpicc
CXX = mpic++
CCC = mpic++
F90 = mpif90
F77 = mpif77
JAVAC = mpijavac

# Using -g is not necessary, but it is helpful for example programs,
# especially if users want to examine them with debuggers.  Note that
# gmake requires the CXXFLAGS macro, while other versions of make
# (such as Sun's make) require the CCFLAGS macro.

CFLAGS = -g
CXXFLAGS = -g
CCFLAGS = -g
F77FLAGS = -g
F90FLAGS = -g

# Example programs to build

EXAMPLES = hello_c hello_cxx hello_f77 hello_f90 Hello.class \
           ring_c ring_cxx ring_f77 ring_f90 connectivity_c Ring.class

# Default target.  Always build the C example.  Only build the others
# if Open MPI was build with the relevant language bindings.

all: hello_c ring_c connectivity_c
	@ if ompi_info --parsable | grep bindings:cxx:yes >/dev/null; then \
	    $(MAKE) hello_cxx ring_cxx; \
	fi
	@ if ompi_info --parsable | grep bindings:f77:yes >/dev/null; then \
	    $(MAKE) hello_f77 ring_f77; \
	fi
	@ if ompi_info --parsable | grep bindings:f90:yes >/dev/null; then \
	    $(MAKE) hello_f90 ring_f90; \
	fi
	@ if ompi_info --parsable | grep bindings:java:yes >/dev/null; then \
	    $(MAKE) Hello.class; \
	fi
	@ if ompi_info --parsable | grep bindings:java:yes >/dev/null; then \
	    $(MAKE) Ring.class; \
	fi
	@ if ompi_info --all --parsable | grep mca:coll:ftbasic:param:coll_ftbasic_method:value:0 >/dev/null; then \
	    $(MAKE) simpleagree; \
	fi
	@ if ompi_info --all --parsable | grep mca:coll:ftbasic:param:coll_ftbasic_method:value:0 >/dev/null; then \
	    $(MAKE) agree4ever; \
	fi


# The usual "clean" target

clean:
	rm -f $(EXAMPLES) *~ *.o

# Don't rely on default rules for the fortran and Java examples

hello_f77: hello_f77.f
	$(F77) $(F77FLAGS) $^ -o $@
ring_f77: ring_f77.f
	$(F77) $(F77FLAGS) $^ -o $@

hello_f90: hello_f90.f90
	$(F90) $(F90FLAGS) $^ -o $@
ring_f90: ring_f90.f90
	$(F90) $(F90FLAGS) $^ -o $@

Hello.class: Hello.java
	$(JAVAC) Hello.java

Ring.class: Ring.java
	$(JAVAC) Ring.java

simpleagree: simpleagree.c
	$(CC) simpleagree.c -o simpleagree

agree4ever: agree4ever.c
	$(CC) -g agree4ever.c -o agree4ever
