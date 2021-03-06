/*
 * Copyright (c) 2004-2007 The Trustees of Indiana University and Indiana
 *                         University Research and Technology
 *                         Corporation.  All rights reserved.
 * Copyright (c) 2004-2005 The University of Tennessee and The University
 *                         of Tennessee Research Foundation.  All rights
 *                         reserved.
 * Copyright (c) 2004-2005 High Performance Computing Center Stuttgart, 
 *                         University of Stuttgart.  All rights reserved.
 * Copyright (c) 2004-2005 The Regents of the University of California.
 *                         All rights reserved.
 * Copyright (c) 2008-2009 Cisco Systems, Inc.  All rights reserved.
 * Copyright (c) 2010-2012 Oak Ridge National Labs.  All rights reserved.
 * $COPYRIGHT$
 * 
 * Additional copyrights may follow
 * 
 * $HEADER$
 */
#include "ompi_config.h"
#include <stdio.h>

#include "ompi/mpi/c/bindings.h"
#include "ompi/runtime/params.h"
#include "ompi/communicator/communicator.h"
#include "ompi/errhandler/errhandler.h"
#include "ompi/win/win.h"

#if OPAL_HAVE_WEAK_SYMBOLS && OMPI_PROFILING_DEFINES
#pragma weak MPI_Win_set_errhandler = PMPI_Win_set_errhandler
#endif

#if OMPI_PROFILING_DEFINES
#include "ompi/mpi/c/profile/defines.h"
#endif

static const char FUNC_NAME[] = "MPI_Win_set_errhandler";


int MPI_Win_set_errhandler(MPI_Win win, MPI_Errhandler errhandler) 
{
    MPI_Errhandler tmp;

    OPAL_CR_NOOP_PROGRESS();

    if (MPI_PARAM_CHECK) {
        OMPI_ERR_INIT_FINALIZE(FUNC_NAME);

        if (ompi_win_invalid(win)) {
            return OMPI_ERRHANDLER_INVOKE(MPI_COMM_WORLD, MPI_ERR_WIN,
                                          FUNC_NAME);
        } else if (NULL == errhandler ||
                   MPI_ERRHANDLER_NULL == errhandler ||
                   (OMPI_ERRHANDLER_TYPE_WIN != errhandler->eh_mpi_object_type && 
                    OMPI_ERRHANDLER_TYPE_PREDEFINED != errhandler->eh_mpi_object_type) ) {
            return OMPI_ERRHANDLER_INVOKE(win, MPI_ERR_ARG, FUNC_NAME);
        }
    }

#if OPAL_ENABLE_FT_MPI
    OMPI_ERRHANDLER_RETURN(OMPI_ERR_NOT_SUPPORTED, win,
                           OMPI_ERR_NOT_SUPPORTED, FUNC_NAME);
#endif

    /* Prepare the new error handler */
    OBJ_RETAIN(errhandler);

    /* Ditch the old errhandler, and decrement its refcount.  On 64
       bits environments we have to make sure the reading of the
       error_handler became atomic. */
    do {
        tmp = win->error_handler;
    } while (!OPAL_ATOMIC_CMPSET(&(win->error_handler), tmp, errhandler));
    OBJ_RELEASE(tmp);

    /* All done */
    return MPI_SUCCESS;
}
