/*
 * Copyright (c) 2004-2007 The Trustees of Indiana University and Indiana
 *                         University Research and Technology
 *                         Corporation.  All rights reserved.
 * Copyright (c) 2004-2015 The University of Tennessee and The University
 *                         of Tennessee Research Foundation.  All rights
 *                         reserved.
 * Copyright (c) 2004-2008 High Performance Computing Center Stuttgart, 
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

#include "ompi/mpi/c/bindings.h"
#include "ompi/runtime/params.h"
#include "ompi/communicator/communicator.h"
#include "ompi/errhandler/errhandler.h"
#include "ompi/mca/pml/pml.h"
#include "ompi/request/request.h"
#include "ompi/memchecker.h"

#if OPAL_HAVE_WEAK_SYMBOLS && OMPI_PROFILING_DEFINES
#pragma weak MPI_Sendrecv = PMPI_Sendrecv
#endif

#if OMPI_PROFILING_DEFINES
#include "ompi/mpi/c/profile/defines.h"
#endif

static const char FUNC_NAME[] = "MPI_Sendrecv";


int MPI_Sendrecv(void *sendbuf, int sendcount, MPI_Datatype sendtype,
                 int dest, int sendtag, void *recvbuf, int recvcount,
                 MPI_Datatype recvtype, int source, int recvtag,
                 MPI_Comm comm,  MPI_Status *status)
{
    ompi_request_t* req;
    int rc = MPI_SUCCESS, rcs = MPI_SUCCESS;
    int zero = 0;

    MEMCHECKER(
        memchecker_datatype(sendtype);
        memchecker_datatype(recvtype);
        memchecker_call(&opal_memchecker_base_isdefined, sendbuf, sendcount, sendtype);
        memchecker_comm(comm);
    );

    if ( MPI_PARAM_CHECK ) {
        OMPI_ERR_INIT_FINALIZE(FUNC_NAME);
        OMPI_CHECK_DATATYPE_FOR_SEND(rc, sendtype, sendcount);
        OMPI_CHECK_DATATYPE_FOR_RECV(rc, recvtype, recvcount);
        OMPI_CHECK_USER_BUFFER(rc, sendbuf, sendtype, sendcount);
        OMPI_CHECK_USER_BUFFER(rc, recvbuf, recvtype, recvcount);

        if (ompi_comm_invalid(comm)) {
            return OMPI_ERRHANDLER_INVOKE(MPI_COMM_WORLD, MPI_ERR_COMM, FUNC_NAME);
        } else if (dest != MPI_PROC_NULL && ompi_comm_peer_invalid(comm, dest)) {
            rc = MPI_ERR_RANK;
        } else if (sendtag < 0 || sendtag > mca_pml.pml_max_tag) {
            rc = MPI_ERR_TAG;
        } else if (source != MPI_PROC_NULL && source != MPI_ANY_SOURCE && ompi_comm_peer_invalid(comm, source)) {
            rc = MPI_ERR_RANK;
        } else if (((recvtag < 0) && (recvtag !=  MPI_ANY_TAG)) || (recvtag > mca_pml.pml_max_tag)) {
                rc = MPI_ERR_TAG;
        }
        OMPI_ERRHANDLER_CHECK(rc, comm, rc, FUNC_NAME);

#if OPAL_ENABLE_FT_MPI
        /*
         * An early check, so as to return early if we are communicating with
         * a failed process. This is not absolutely necessary since we will
         * check for this, and other, error conditions during the completion
         * call in the PML.
         */
        if( !ompi_comm_iface_p2p_check_proc(comm, dest, &rc) ) {
            /* Do *NOT* set status->MPI_ERROR here!  See MPI-1.1 doc,
               sec 3.2.5, p.22 */
            if( MPI_STATUS_IGNORE != status ) {
                status->MPI_SOURCE = source;
                status->MPI_TAG    = recvtag;
                OMPI_STATUS_SET_COUNT(&status->_ucount, &zero);
                status->_cancelled = false;
            }
            OMPI_ERRHANDLER_RETURN(rc, comm, rc, FUNC_NAME);
        }

        if( !ompi_comm_iface_p2p_check_proc(comm, source, &rc) ) {
            /* Do *NOT* set status->MPI_ERROR here!  See MPI-1.1 doc,
               sec 3.2.5, p.22 */
            if( MPI_STATUS_IGNORE != status ) {
                status->MPI_SOURCE = source;
                status->MPI_TAG    = recvtag;
                OMPI_STATUS_SET_COUNT(&status->_ucount, &zero);
                status->_cancelled = false;
            }
            OMPI_ERRHANDLER_RETURN(rc, comm, rc, FUNC_NAME);
        }
#endif
    }

    OPAL_CR_ENTER_LIBRARY();

    if (source != MPI_PROC_NULL) { /* post recv */
        rc = MCA_PML_CALL(irecv(recvbuf, recvcount, recvtype,
                                source, recvtag, comm, &req));
        OMPI_ERRHANDLER_CHECK(rc, comm, rc, FUNC_NAME);
    }

    if (dest != MPI_PROC_NULL) { /* send */
        rc = MCA_PML_CALL(send(sendbuf, sendcount, sendtype, dest,
                               sendtag, MCA_PML_BASE_SEND_STANDARD, comm));
        /* If ULFM is enabled we need to wait for the posted receive to complete */
#if !OPAL_ENABLE_FT_MPI
        OMPI_ERRHANDLER_RETURN(rc, comm, rc, FUNC_NAME);
#else
        rcs = rc;
#endif  /* OPAL_ENABLE_FT_MPI */
    }

    if (source != MPI_PROC_NULL) { /* wait for recv */
        rc = ompi_request_wait(&req, status);
    } else {
        if (MPI_STATUS_IGNORE != status) {
            *status = ompi_request_empty.req_status;
        }
        rc = MPI_SUCCESS;
    }
    if( OPAL_UNLIKELY(MPI_SUCCESS != rcs && MPI_SUCCESS == rc) ) {
        rc = rcs;
    }

    OMPI_ERRHANDLER_RETURN(rc, comm, rc, FUNC_NAME);
}
