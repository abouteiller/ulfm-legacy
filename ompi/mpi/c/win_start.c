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
#include "ompi/mca/osc/osc.h"

#if OPAL_HAVE_WEAK_SYMBOLS && OMPI_PROFILING_DEFINES
#pragma weak MPI_Win_start = PMPI_Win_start
#endif

#if OMPI_PROFILING_DEFINES
#include "ompi/mpi/c/profile/defines.h"
#endif

static const char FUNC_NAME[] = "MPI_Win_start";


int MPI_Win_start(MPI_Group group, int assert, MPI_Win win) 
{
    int rc;

    if (MPI_PARAM_CHECK) {
        OMPI_ERR_INIT_FINALIZE(FUNC_NAME);

        if (ompi_win_invalid(win)) {
            return OMPI_ERRHANDLER_INVOKE(MPI_COMM_WORLD, MPI_ERR_WIN, FUNC_NAME);
        } else if (0 != (assert & ~(MPI_MODE_NOCHECK))) {
            return OMPI_ERRHANDLER_INVOKE(win, MPI_ERR_ASSERT, FUNC_NAME);
        } else if (0 != (ompi_win_get_mode(win) & OMPI_WIN_ACCESS_EPOCH)) {
            return OMPI_ERRHANDLER_INVOKE(win, MPI_ERR_RMA_SYNC, FUNC_NAME);
        }
    }

#if OPAL_ENABLE_FT_MPI
    OMPI_ERRHANDLER_RETURN(OMPI_ERR_NOT_SUPPORTED, win,
                           OMPI_ERR_NOT_SUPPORTED, FUNC_NAME);
#endif

    OPAL_CR_ENTER_LIBRARY();

    rc = win->w_osc_module->osc_start(group, assert, win);
    OMPI_ERRHANDLER_RETURN(rc, win, rc, FUNC_NAME);
}
