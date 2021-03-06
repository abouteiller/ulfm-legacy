# -*- shell-script -*-
#
# Copyright (c) 2004-2005 The Trustees of Indiana University and Indiana
#                         University Research and Technology
#                         Corporation.  All rights reserved.
# Copyright (c) 2004-2005 The University of Tennessee and The University
#                         of Tennessee Research Foundation.  All rights
#                         reserved.
# Copyright (c) 2004-2005 High Performance Computing Center Stuttgart, 
#                         University of Stuttgart.  All rights reserved.
# Copyright (c) 2004-2005 The Regents of the University of California.
#                         All rights reserved.
# Copyright (c) 2010      Cisco Systems, Inc.  All rights reserved.
# Copyright (c) 2011      IBM Corporation.  All rights reserved.
# Copyright (c) 2011      Los Alamos National Security, LLC.
#                         All rights reserved.
# $COPYRIGHT$
# 
# Additional copyrights may follow
# 
# $HEADER$
#

# MCA_plm_poe_CONFIG([action-if-found], [action-if-not-found])
# -----------------------------------------------------------

AC_DEFUN([MCA_orte_plm_poe_CONFIG],[
    AC_CONFIG_FILES([orte/mca/plm/poe/Makefile])

    # POE is only supported on AIX and Linux.  We only need executables (no
    # header files or libraries), but those can be found (or not) at
    # run-time.  So if we're on AIX or Linux, and can find the poe executable
    # build this component.
    case $host_os in
    aix* | linux* )
        AC_CHECK_PROG(POE,poe,poe,no)
        if test "$POE" = no ; then
            happy=no	
        else
            happy=yes
        fi
        ;;
    *)
        happy=no
        ;;
    esac
    AS_IF([test "$happy" = "yes" -a "$orte_without_full_support" = 0], [$1], [$2])
])
