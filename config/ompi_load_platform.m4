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
# $COPYRIGHT$
# 
# Additional copyrights may follow
# 
# $HEADER$
#

# OMPI_LOAD_PLATFORM()
# --------------------
AC_DEFUN([OMPI_LOAD_PLATFORM], [
    AC_ARG_WITH([platform],
        [AC_HELP_STRING([--with-platform=FILE],
                        [Load options for build from FILE.  Options on the
                         command line not in FILE are used.  Options on the
                         command line and in FILE are replaced by what is
                         in FILE.])])
    m4_ifval([autogen_platform_file], [
        if test "$with_platform" = "" ; then
            with_platform=autogen_platform_file
        fi])
    if test "$with_platform" = "yes" ; then
        AC_MSG_ERROR([--with-platform argument must include FILE option])
    elif test "$with_platform" = "no" ; then
        AC_MSG_ERROR([--without-platform is not a valid argument])
    elif test "$with_platform" != "" ; then
        # if not an absolute path, check in contrib/platform
        if test ! "`echo $with_platform | cut -c1`" = "/" -a ! "`echo $with_platform | cut -c2`" = ".." ; then
            if test -r "${srcdir}/contrib/platform/$with_platform" ; then
                with_platform="${srcdir}/contrib/platform/$with_platform"
            fi
        fi

        # make sure file exists
        if test ! -r "$with_platform" ; then
            AC_MSG_ERROR([platform file $with_platform not found])
        fi

        # eval into environment
        OPAL_LOG_MSG([Loading environment file $with_platform, with contents below])
        OPAL_LOG_FILE([$with_platform])
        . "$with_platform"

        # see if they left us a name
        if test "$OMPI_PLATFORM_LOADED" != "" ; then
           platform_loaded="$OMPI_PLATFORM_LOADED"
        else
           platform_loaded="$with_platform"
        fi
        echo "Loaded platform arguments for $platform_loaded"
        OPAL_LOG_MSG([Loaded platform arguments for $platform_loaded])

        # look for default mca param file

        # setup by getting full pathname for the platform directories
        platform_base="`dirname $with_platform`"
        # get full pathname of where we are so we can return
        platform_savedir="`pwd`"
        # go to where the platform file is located
        cd "$platform_base"
        # get the full path to this location
        platform_file_dir=`pwd`
        # return to where we started
        cd "$platform_savedir"

        # define an alternate default mca param filename
        platform_alt_mca_file="`basename $platform_loaded`.conf"

        # look where platform file is located for platform.conf name
        if test -r "${platform_file_dir}/${platform_alt_mca_file}" ; then
            AC_SUBST(OPAL_DEFAULT_MCA_PARAM_CONF, [$platform_file_dir/$platform_alt_mca_file])
            AC_SUBST(OPAL_PARAM_FROM_PLATFORM, "yes")
        # if not, see if a file is there with the default name
        elif test -r "${platform_file_dir}/openmpi-mca-params.conf" ; then
            AC_SUBST(OPAL_DEFAULT_MCA_PARAM_CONF, [$platform_file_dir/openmpi-mca-params.conf])
            AC_SUBST(OPAL_PARAM_FROM_PLATFORM, "yes")
        # if not, then just use the default
        else
            AC_SUBST(OPAL_DEFAULT_MCA_PARAM_CONF, [openmpi-mca-params.conf])
            AC_SUBST(OPAL_PARAM_FROM_PLATFORM, "no")
        fi

    else
        AC_SUBST(OPAL_DEFAULT_MCA_PARAM_CONF, [openmpi-mca-params.conf])
    fi
])
