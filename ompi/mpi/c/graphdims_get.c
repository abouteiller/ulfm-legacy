/*
 * Copyright (c) 2004-2007 The Trustees of Indiana University and Indiana
 *                         University Research and Technology
 *                         Corporation.  All rights reserved.
 * Copyright (c) 2004-2005 The University of Tennessee and The University
 *                         of Tennessee Research Foundation.  All rights
 *                         reserved.
 * Copyright (c) 2004-2008 High Performance Computing Center Stuttgart, 
 *                         University of Stuttgart.  All rights reserved.
 * Copyright (c) 2004-2005 The Regents of the University of California.
 *                         All rights reserved.
 * Copyright (c) 2007      Cisco Systems, Inc.  All rights reserved.
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
#include "ompi/mca/topo/topo.h"
#include "ompi/memchecker.h"

#if OPAL_HAVE_WEAK_SYMBOLS && OMPI_PROFILING_DEFINES
#pragma weak MPI_Graphdims_get = PMPI_Graphdims_get
#endif

#if OMPI_PROFILING_DEFINES
#include "ompi/mpi/c/profile/defines.h"
#endif

static const char FUNC_NAME[] = "MPI_Graphdims_get";


int MPI_Graphdims_get(MPI_Comm comm, int *nnodes, int *nedges) 
{
    int err;
    mca_topo_base_module_graphdims_get_fn_t func;

    MEMCHECKER(
        memchecker_comm(comm);
    );

    /* check the arguments */
    if (MPI_PARAM_CHECK) {
        OMPI_ERR_INIT_FINALIZE(FUNC_NAME);
        if (ompi_comm_invalid(comm)) {
            return OMPI_ERRHANDLER_INVOKE (MPI_COMM_WORLD, MPI_ERR_COMM,
                                           FUNC_NAME);
        }
        if (OMPI_COMM_IS_INTER(comm)) {
            return OMPI_ERRHANDLER_INVOKE (comm, MPI_ERR_COMM,
                                           FUNC_NAME);
        }
        if (!OMPI_COMM_IS_GRAPH(comm)) {
            return OMPI_ERRHANDLER_INVOKE (comm, MPI_ERR_TOPOLOGY,
                                           FUNC_NAME);
        }
        if (NULL == nnodes || NULL == nedges) {
            return OMPI_ERRHANDLER_INVOKE (comm, MPI_ERR_ARG,
                                           FUNC_NAME);
        }
    }

#if OPAL_ENABLE_FT_MPI
    OMPI_ERRHANDLER_RETURN(OMPI_ERR_NOT_SUPPORTED, comm,
                           OMPI_ERR_NOT_SUPPORTED, FUNC_NAME);
#endif

    OPAL_CR_ENTER_LIBRARY();

    /* get the function pointer to do the right thing */
    func = comm->c_topo->topo_graphdims_get;

    /* call the function */
    err = func(comm, nnodes, nedges);
    OPAL_CR_EXIT_LIBRARY();
    if ( MPI_SUCCESS != err ) {
        return OMPI_ERRHANDLER_INVOKE(comm, err, FUNC_NAME);
    }
    
    /* All done */
    return MPI_SUCCESS;
}
