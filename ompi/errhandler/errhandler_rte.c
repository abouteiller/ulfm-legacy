/*
 * Copyright (c) 2010-2012 Oak Ridge National Labs.  All rights reserved.
 *
 * $COPYRIGHT$
 * 
 * Additional copyrights may follow
 * 
 * $HEADER$
 */

#include "ompi_config.h"

#ifdef HAVE_STRING_H
#include <string.h>
#endif

#include "opal/class/opal_pointer_array.h"

#include "orte/util/name_fns.h"
#include "orte/util/error_strings.h"
#include "orte/mca/errmgr/errmgr.h"
#include "orte/mca/errmgr/base/base.h"

#include "ompi/communicator/communicator.h"
#include "ompi/request/request.h"
#include "ompi/runtime/params.h"

#include "ompi/errhandler/errhandler.h"
#include "ompi/errhandler/errhandler_predefined.h"

#if OPAL_ENABLE_FT_MPI

/*
 * Local variables and functions
 */
static int ompi_errmgr_callback(orte_process_name_t proc, orte_proc_state_t state);

/*
 * Interface Functions
 */
int ompi_errhandler_internal_rte_init(void)
{
    int ret, exit_status = OMPI_SUCCESS;

    /*
     * Register to get a callback when a process fails
     */
    if( OMPI_SUCCESS != (ret = orte_errmgr_base_app_reg_notify_callback(ompi_errmgr_callback, NULL)) ) {
        ORTE_ERROR_LOG(ret);
        exit_status = ret;
        goto cleanup;
    }

 cleanup:
    return exit_status;
}

int ompi_errhandler_internal_rte_finalize(void)
{
    int ret, exit_status = OMPI_SUCCESS;

    /*
     * Deregister the process fail callback.
     */
    if( OMPI_SUCCESS != (ret = orte_errmgr_base_app_reg_notify_callback(NULL, NULL)) ) {
        ORTE_ERROR_LOG(ret);
        exit_status = ret;
        goto cleanup;
    }

 cleanup:
    return exit_status;
}

int ompi_errmgr_mark_failed_peer(ompi_proc_t *ompi_proc, orte_proc_state_t state)
{
    int exit_status = OMPI_SUCCESS;
    int max_num_comm = 0, i;
    ompi_communicator_t *comm = NULL;
    int proc_rank;
    bool remote = false;

    /*
     * If we have already detected this error, ignore
     */
    if( !ompi_proc_is_active(ompi_proc) ) {
        goto cleanup;
    }

    OPAL_OUTPUT_VERBOSE((2, ompi_ftmpi_output_handle,
                         "%s ompi: Process %s failed (state = %s).",
                         ORTE_NAME_PRINT(ORTE_PROC_MY_NAME),
                         ORTE_NAME_PRINT(&ompi_proc->proc_name),
                         orte_proc_state_to_str(state) ));

    /*
     * Process State:
     * Update process state to failed
     */
    ompi_proc_mark_as_failed( ompi_proc );

    /*
     * Group State:
     * Nothing to do.
     */

    /*
     * Communicator State:
     * Let them know about the failure.
     */
    max_num_comm = opal_pointer_array_get_size(&ompi_mpi_communicators);
    for( i = 0; i < max_num_comm; ++i ) {
        comm = (ompi_communicator_t *)opal_pointer_array_get_item(&ompi_mpi_communicators, i);
        if( NULL == comm ) {
            continue;
        }

        /*
         * Look in both the local and remote group for this process
         */
        proc_rank = ompi_group_peer_lookup_id(comm->c_local_group, ompi_proc);
        if( proc_rank < 0 ) {
            proc_rank = ompi_group_peer_lookup_id(comm->c_remote_group, ompi_proc);
            if( proc_rank < 0 ) {
                /* Not in this communicator, continue */
                continue;
            } else {
                remote = true;
            }
        } else {
            remote = false;
        }

        /* Notify the communicator to update as necessary */
        ompi_comm_set_rank_failed(comm, proc_rank, remote);

        OPAL_OUTPUT_VERBOSE((10, ompi_ftmpi_output_handle,
                             "%s ompi: Process %s is in comm (%d) with rank %d. (%2d of %2d / %2d of %2d) [%s]",
                             ORTE_NAME_PRINT(ORTE_PROC_MY_NAME),
                             ORTE_NAME_PRINT(&ompi_proc->proc_name),
                             comm->c_contextid,
                             proc_rank,
                             ompi_comm_num_active_local(comm),
                             ompi_group_size(comm->c_local_group),
                             ompi_comm_num_active_remote(comm),
                             ompi_group_size(comm->c_remote_group),
                             (OMPI_ERRHANDLER_TYPE_PREDEFINED == comm->errhandler_type ? "P" :
                              (OMPI_ERRHANDLER_TYPE_COMM == comm->errhandler_type ? "C" :
                               (OMPI_ERRHANDLER_TYPE_WIN == comm->errhandler_type ? "W" :
                                (OMPI_ERRHANDLER_TYPE_FILE == comm->errhandler_type ? "F" : "U") ) ) )
                             ));
        /*
         * Invoke the users error handler for each communicator in which this
         * process is found.
         * Do not do this here, only activated when referenced.
         *
         * OMPI_ERRHANDLER_INVOKE(comm, MPI_ERR_PROC_FAILED, "ompi_errmgr_mark_failed_peer");
         */
    }

    /*
     * Point-to-Point:
     * Let the active request know of the process state change.
     * The wait function has a check, so all we need to do here is
     * signal it so it will check again.
     */
    OPAL_THREAD_LOCK(&ompi_request_lock);
    opal_condition_signal(&ompi_request_cond);
    OPAL_THREAD_UNLOCK(&ompi_request_lock);

    /*
     * Flush modex information?
     */

    /*
     * Collectives:
     * Flush collective P2P channels?
     */

 cleanup:
    return exit_status;
}

/*
 * Local Functions
 */
static int ompi_errmgr_callback(orte_process_name_t proc, orte_proc_state_t state)
{
    int ret;
    ompi_proc_t *ompi_proc = NULL;

    /*
     * Find the ompi_proc_t
     */
    if( NULL == (ompi_proc = ompi_proc_find(&proc)) ) {
        ret = OMPI_ERROR;
        ORTE_ERROR_LOG(ret);
        return ret;
    }

    return ompi_errmgr_mark_failed_peer(ompi_proc, state);
}

#endif /* OPAL_ENABLE_FT_MPI */