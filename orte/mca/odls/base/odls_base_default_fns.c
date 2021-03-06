/*
 * Copyright (c) 2004-2010 The Trustees of Indiana University and Indiana
 *                         University Research and Technology
 *                         Corporation.  All rights reserved.
 * Copyright (c) 2004-2011 The University of Tennessee and The University
 *                         of Tennessee Research Foundation.  All rights
 *                         reserved.
 * Copyright (c) 2004-2005 High Performance Computing Center Stuttgart, 
 *                         University of Stuttgart.  All rights reserved.
 * Copyright (c) 2004-2005 The Regents of the University of California.
 *                         All rights reserved.
 * Copyright (c) 2007-2011 Oracle and/or its affiliates.  All rights reserved. 
 * Copyright (c) 2011-2012 Oak Ridge National Labs.  All rights reserved.
 * Copyright (c) 2011      Los Alamos National Security, LLC.
 *                         All rights reserved.
 * Copyright (c) 2011 Cisco Systems, Inc.  All rights reserved.
 * $COPYRIGHT$
 * 
 * Additional copyrights may follow
 * 
 * $HEADER$
 */


#include "orte_config.h"
#include "orte/constants.h"
#include "orte/types.h"

#ifdef HAVE_SYS_WAIT_H
#include <sys/wait.h>
#endif
#include <errno.h>
#ifdef HAVE_SYS_STAT_H
#include <sys/stat.h>
#endif  /* HAVE_SYS_STAT_H */
#ifdef HAVE_SYS_PARAM_H
#include <sys/param.h>
#endif
#ifdef HAVE_TIME_H
#include <time.h>
#endif

#include <signal.h>

#include "opal/util/opal_environ.h"
#include "opal/util/argv.h"
#include "opal/util/os_path.h"
#include "opal/util/path.h"
#include "opal/util/sys_limits.h"
#include "opal/dss/dss.h"
#include "opal/mca/hwloc/hwloc.h"
#include "opal/mca/shmem/base/base.h"
#include "opal/mca/pstat/pstat.h"

#include "orte/mca/errmgr/errmgr.h"
#include "orte/mca/rml/rml.h"
#include "orte/mca/routed/routed.h"
#include "orte/mca/iof/iof.h"
#include "orte/mca/iof/base/iof_base_setup.h"
#include "orte/mca/ess/base/base.h"
#include "orte/mca/plm/base/base.h"
#include "orte/mca/routed/base/base.h"
#include "orte/mca/rmaps/rmaps_types.h"
#include "orte/mca/sensor/sensor.h"

#include "orte/util/context_fns.h"
#include "orte/util/name_fns.h"
#include "orte/util/session_dir.h"
#include "orte/util/proc_info.h"
#include "orte/util/nidmap.h"
#include "orte/util/regex.h"
#include "orte/util/show_help.h"
#include "orte/runtime/orte_globals.h"
#include "orte/runtime/orte_wait.h"
#include "orte/orted/orted.h"

#if OPAL_ENABLE_FT_CR == 1
#include "orte/mca/snapc/snapc.h"
#include "orte/mca/snapc/base/base.h"
#include "orte/mca/sstore/sstore.h"
#include "orte/mca/sstore/base/base.h"
#include "opal/mca/crs/crs.h"
#include "opal/mca/crs/base/base.h"
#endif

#include "orte/mca/odls/base/base.h"
#include "orte/mca/odls/base/odls_private.h"

/* IT IS CRITICAL THAT ANY CHANGE IN THE ORDER OF THE INFO PACKED IN
 * THIS FUNCTION BE REFLECTED IN THE CONSTRUCT_CHILD_LIST PARSER BELOW
*/
int orte_odls_base_default_get_add_procs_data(opal_buffer_t *data,
                                              orte_jobid_t job)
{
    int rc;
    orte_job_t *jdata=NULL;
    orte_proc_t *proc;
    orte_job_map_t *map=NULL;
    opal_buffer_t *wireup;
    opal_byte_object_t bo, *boptr;
    int32_t numbytes;
    int8_t flag;
    int j;
    orte_daemon_cmd_flag_t command;
    orte_app_context_t *app;

    if (NULL != orte_debugger_daemon && ORTE_JOBID_INVALID == job) {
        /* all we are doing is launching debugger daemons */
        goto nodemap;
    }
    
    /* get the job data pointer */
    if (NULL == (jdata = orte_get_job_data_object(job))) {
        ORTE_ERROR_LOG(ORTE_ERR_BAD_PARAM);
        return ORTE_ERR_BAD_PARAM;
    }
    
    /* get a pointer to the job map */
    map = jdata->map;
    /* if there is no map, just return */
    if (NULL == map) {
        return ORTE_SUCCESS;
    }
     
 nodemap:
    /* construct a nodemap */
    if (ORTE_SUCCESS != (rc = orte_util_encode_nodemap(&bo))) {
        ORTE_ERROR_LOG(rc);
        return rc;
    }
    
    /* store it */
    boptr = &bo;
    if (ORTE_SUCCESS != (rc = opal_dss.pack(data, &boptr, 1, OPAL_BYTE_OBJECT))) {
        ORTE_ERROR_LOG(rc);
        return rc;
    }
    /* release the data since it has now been copied into our buffer */
    free(bo.bytes);
    
    /* if we are not using static ports, we need to send the wireup info */
    if (!orte_static_ports) {
        /* pack a flag indicating wiring info is provided */
        flag = 1;
        opal_dss.pack(data, &flag, 1, OPAL_INT8);
        /* get wireup info for daemons per the selected routing module */
        wireup = OBJ_NEW(opal_buffer_t);
        if (ORTE_SUCCESS != (rc = orte_routed.get_wireup_info(wireup))) {
            ORTE_ERROR_LOG(rc);
            OBJ_RELEASE(wireup);
            return rc;
        }
        /* if anything was inserted, put it in a byte object for xmission */
        if (0 < wireup->bytes_used) {
            opal_dss.unload(wireup, (void**)&bo.bytes, &numbytes);
            /* pack the number of bytes required by payload */
            if (ORTE_SUCCESS != (rc = opal_dss.pack(data, &numbytes, 1, OPAL_INT32))) {
                ORTE_ERROR_LOG(rc);
                OBJ_RELEASE(wireup);
                return rc;
            }
            /* pack the byte object */
            bo.size = numbytes;
            boptr = &bo;
            if (ORTE_SUCCESS != (rc = opal_dss.pack(data, &boptr, 1, OPAL_BYTE_OBJECT))) {
                ORTE_ERROR_LOG(rc);
                OBJ_RELEASE(wireup);
                return rc;
            }
            /* release the data since it has now been copied into our buffer */
            free(bo.bytes);
        } else {
            /* pack numbytes=0 so the unpack routine remains sync'd to us */
            numbytes = 0;
            if (ORTE_SUCCESS != (rc = opal_dss.pack(data, &numbytes, 1, OPAL_INT32))) {
                ORTE_ERROR_LOG(rc);
                OBJ_RELEASE(wireup);
                return rc;
            }
        }
        OBJ_RELEASE(wireup);
    } else {
        /* pack a flag indicating no wireup data is provided */
        flag = 0;
        opal_dss.pack(data, &flag, 1, OPAL_INT8);
    }

    /* insert an "add-procs" command here so we can cleanly process it on the
     * other end
     */
    command = ORTE_DAEMON_ADD_LOCAL_PROCS;
    if (ORTE_SUCCESS != (rc = opal_dss.pack(data, &command, 1, ORTE_DAEMON_CMD))) {
        ORTE_ERROR_LOG(rc);
        return rc;
    }
    
    /* are we co-locating debugger daemons? */
    if (NULL != orte_debugger_daemon) {        
        /* flag that we are */
        flag = 1;
        if (ORTE_SUCCESS != (rc = opal_dss.pack(data, &flag, 1, OPAL_INT8))) {
            ORTE_ERROR_LOG(rc);
            return rc;
        }
        /* pack the jobid for the debugger daemons */
        if (ORTE_SUCCESS != (rc = opal_dss.pack(data, &orte_debugger_daemon->jobid, 1, ORTE_JOBID))) {
            ORTE_ERROR_LOG(rc);
            return rc;
        }        
        /* pack the executable name */
        app = (orte_app_context_t*)opal_pointer_array_get_item(orte_debugger_daemon->apps, 0);
        if (ORTE_SUCCESS != (rc = opal_dss.pack(data, &app, 1, ORTE_APP_CONTEXT))) {
            ORTE_ERROR_LOG(rc);
            return rc;
        }
        /* pack the control flags */
        if (ORTE_SUCCESS != (rc = opal_dss.pack(data, &orte_debugger_daemon->controls, 1, ORTE_JOB_CONTROL))) {
            ORTE_ERROR_LOG(rc);
            return rc;
        }        
    } else {
        /* flag that we are NOT */
        flag = 0;
        if (ORTE_SUCCESS != (rc = opal_dss.pack(data, &flag, 1, OPAL_INT8))) {
            ORTE_ERROR_LOG(rc);
            return rc;
        }
    }
    
    if (NULL != orte_debugger_daemon && ORTE_JOBID_INVALID == job) {
        /* all we are doing is launching debugger daemons, so we are done */
        return ORTE_SUCCESS;
    }
    
    /* pack the jobid so it can be extracted later */
    if (ORTE_SUCCESS != (rc = opal_dss.pack(data, &job, 1, ORTE_JOBID))) {
        ORTE_ERROR_LOG(rc);
        return rc;
    }
    
    /* pack the job state so it can be extracted later */
    if (ORTE_SUCCESS != (rc = opal_dss.pack(data, &jdata->state, 1, ORTE_JOB_STATE))) {
        ORTE_ERROR_LOG(rc);
        return rc;
    }
    
    /* pack the number of nodes involved in this job */
    if (ORTE_SUCCESS != (rc = opal_dss.pack(data, &map->num_nodes, 1, ORTE_STD_CNTR))) {
        ORTE_ERROR_LOG(rc);
        return rc;
    }
    
    /* pack the number of procs in this launch */
    if (ORTE_SUCCESS != (rc = opal_dss.pack(data, &jdata->num_procs, 1, ORTE_VPID))) {
        ORTE_ERROR_LOG(rc);
        return rc;
    }
    
    /* pack the total slots allocated to us */
    if (ORTE_SUCCESS != (rc = opal_dss.pack(data, &jdata->total_slots_alloc, 1, ORTE_STD_CNTR))) {
        ORTE_ERROR_LOG(rc);
        return rc;
    }
    
#if OPAL_HAVE_HWLOC
    /* pack the binding policy so the daemon knows if binding is required */
    if (ORTE_SUCCESS != (rc = opal_dss.pack(data, &jdata->map->binding, 1, OPAL_BINDING_POLICY))) {
        ORTE_ERROR_LOG(rc);
        return rc;
    }
#endif

    /* pack the control flags for this job */
    if (ORTE_SUCCESS != (rc = opal_dss.pack(data, &jdata->controls, 1, ORTE_JOB_CONTROL))) {
        ORTE_ERROR_LOG(rc);
        return rc;
    }
    
    /* pack the stdin target  */
    if (ORTE_SUCCESS != (rc = opal_dss.pack(data, &jdata->stdin_target, 1, ORTE_VPID))) {
        ORTE_ERROR_LOG(rc);
        return rc;
    }
    
    /* pack whether or not process recovery is allowed for this job */
    if (ORTE_SUCCESS != (rc = opal_dss.pack(data, &jdata->enable_recovery, 1, OPAL_BOOL))) {
        ORTE_ERROR_LOG(rc);
        return rc;
    }
    
    /* pack the number of app_contexts for this job */
    if (ORTE_SUCCESS != (rc = opal_dss.pack(data, &jdata->num_apps, 1, ORTE_APP_IDX))) {
        ORTE_ERROR_LOG(rc);
        return rc;
    }
    
    /* pack the app_contexts for this job */
    for (j=0; j < jdata->apps->size; j++) {
        if (NULL != (app = (orte_app_context_t*)opal_pointer_array_get_item(jdata->apps, j))) {
            if (ORTE_SUCCESS != (rc = opal_dss.pack(data, &app, 1, ORTE_APP_CONTEXT))) {
                ORTE_ERROR_LOG(rc);
                return rc;
            }
        }
    }
    
    /* encode the pidmap */
    if (ORTE_SUCCESS != (rc = orte_util_encode_pidmap(&bo))) {
        ORTE_ERROR_LOG(rc);
        return rc;
    }
    /* store it */
    boptr = &bo;
    if (ORTE_SUCCESS != (rc = opal_dss.pack(data, &boptr, 1, OPAL_BYTE_OBJECT))) {
        ORTE_ERROR_LOG(rc);
        return rc;
    }
    /* release the data since it has now been copied into our buffer */
    free(bo.bytes);
    
    /* pack the procs for this job */
    for (j=0; j < jdata->procs->size; j++) {
        if (NULL == (proc = (orte_proc_t*)opal_pointer_array_get_item(jdata->procs, j))) {
            continue;
        }
        if (ORTE_SUCCESS != (rc = opal_dss.pack(data, &proc, 1, ORTE_PROC))) {
            ORTE_ERROR_LOG(rc);
            return rc;
        }
    }

    return ORTE_SUCCESS;
}

int orte_odls_base_default_update_daemon_info(opal_buffer_t *data)
{
    opal_buffer_t wireup;
    opal_byte_object_t *bo;
    int rc;
    orte_std_cntr_t cnt;
    int32_t numbytes;
    int8_t flag;

    /* extract the byte object holding the daemonmap */
    cnt=1;
    if (ORTE_SUCCESS != (rc = opal_dss.unpack(data, &bo, &cnt, OPAL_BYTE_OBJECT))) {
        ORTE_ERROR_LOG(rc);
        return rc;
    }
    /* retain a copy for downloading to child processes */
    if (NULL != orte_odls_globals.dmap) {
        free(orte_odls_globals.dmap->bytes);
        free(orte_odls_globals.dmap);
        orte_odls_globals.dmap = NULL;
    }
    opal_dss.copy((void**)&orte_odls_globals.dmap, bo, OPAL_BYTE_OBJECT);
    
    /* update our local nidmap, if required - the decode function
     * knows what to do - it will also free the bytes in the bo
     */
    OPAL_OUTPUT_VERBOSE((5, orte_odls_globals.output,
                         "%s odls:update:daemon:info updating nidmap",
                         ORTE_NAME_PRINT(ORTE_PROC_MY_NAME)));
    
    if (ORTE_SUCCESS != (rc = orte_ess.update_nidmap(bo))) {
        ORTE_ERROR_LOG(rc);
        return rc;
    }
    /* update the routing tree */
    if (ORTE_SUCCESS != (rc = orte_routed.update_routing_tree(ORTE_PROC_MY_NAME->jobid))) {
        ORTE_ERROR_LOG(rc);
        return rc;
    }
    
    /* see if we have wiring info as well */
    cnt=1;
    if (ORTE_SUCCESS != (rc = opal_dss.unpack(data, &flag, &cnt, OPAL_INT8))) {
        ORTE_ERROR_LOG(rc);
        return rc;
    }
    if (0 == flag) {
        /* no - just return */
        return rc;
    }

    /* unpack the #bytes of daemon wireup info in the message */
    cnt=1;
    if (ORTE_SUCCESS != (rc = opal_dss.unpack(data, &numbytes, &cnt, OPAL_INT32))) {
        ORTE_ERROR_LOG(rc);
        return rc;
    }
    /* any bytes there? */
    if (0 < numbytes) {
        /* unpack the byte object */
        cnt=1;
        if (ORTE_SUCCESS != (rc = opal_dss.unpack(data, &bo, &cnt, OPAL_BYTE_OBJECT))) {
            ORTE_ERROR_LOG(rc);
            return rc;
        }
        /* load it into a buffer */
        OBJ_CONSTRUCT(&wireup, opal_buffer_t);
        opal_dss.load(&wireup, bo->bytes, bo->size);
        /* pass it for processing */
        if (ORTE_SUCCESS != (rc = orte_routed.init_routes(ORTE_PROC_MY_NAME->jobid, &wireup))) {
            ORTE_ERROR_LOG(rc);
            OBJ_DESTRUCT(&wireup);
            return rc;
        }
        /* done with the buffer - dump it */
        OBJ_DESTRUCT(&wireup);
    }
    
    return ORTE_SUCCESS;
}

int orte_odls_base_default_construct_child_list(opal_buffer_t *data,
                                                orte_jobid_t *job)
{
    int rc;
    orte_vpid_t j, host_daemon;
    orte_odls_child_t *child;
    orte_std_cntr_t cnt;
    orte_odls_job_t *jobdat=NULL;
    opal_byte_object_t *bo;
    opal_list_item_t *item;
    int8_t flag;
    orte_jobid_t debugger;
    bool add_child;
    orte_ns_cmp_bitmask_t mask;
    orte_app_context_t *app;
    orte_proc_t *pptr;

    OPAL_OUTPUT_VERBOSE((5, orte_odls_globals.output,
                         "%s odls:constructing child list",
                         ORTE_NAME_PRINT(ORTE_PROC_MY_NAME)));

    *job = ORTE_JOBID_INVALID;
    
    /* unpack the flag - are we co-locating debugger daemons? */
    cnt=1;
    if (ORTE_SUCCESS != (rc = opal_dss.unpack(data, &flag, &cnt, OPAL_INT8))) {
        ORTE_ERROR_LOG(rc);
        goto REPORT_ERROR;
    }
    if (0 != flag) {
        OPAL_OUTPUT_VERBOSE((5, orte_odls_globals.output,
                             "%s odls:construct_child_list unpacking debugger daemon",
                             ORTE_NAME_PRINT(ORTE_PROC_MY_NAME)));
        /* yep - create a jobdat object for it. In this case, we don't have to
         * worry about race conditions as the debugger daemons do not use
         * the daemon collective system
         */
        orte_odls_globals.debugger = OBJ_NEW(orte_odls_job_t);
        /* get the debugger daemon jobid */
        cnt=1;
        if (ORTE_SUCCESS != (rc = opal_dss.unpack(data, &debugger, &cnt, ORTE_JOBID))) {
            ORTE_ERROR_LOG(rc);
            goto REPORT_ERROR;
        }
        orte_odls_globals.debugger->jobid = debugger;
        orte_odls_globals.debugger->num_apps = 1;
        orte_odls_globals.debugger->num_local_procs = 1;
        opal_list_append(&orte_local_jobdata, &(orte_odls_globals.debugger)->super);
        /* retrieve the info */
        cnt = 1;
        app = NULL;
        if (ORTE_SUCCESS != (rc = opal_dss.unpack(data, &app, &cnt, ORTE_APP_CONTEXT))) {
            ORTE_ERROR_LOG(rc);
            goto REPORT_ERROR;
        }
        opal_pointer_array_add(&orte_odls_globals.debugger->apps, app);
        cnt=1;
        if (ORTE_SUCCESS != (rc = opal_dss.unpack(data, &(orte_odls_globals.debugger->controls), &cnt, ORTE_JOB_CONTROL))) {
            ORTE_ERROR_LOG(rc);
            goto REPORT_ERROR;
        }
    }
    
    /* unpack the jobid we are to launch */
    cnt=1;
    if (ORTE_SUCCESS != (rc = opal_dss.unpack(data, job, &cnt, ORTE_JOBID))) {
        /* if the buffer was empty, then we know that all we are doing is
         * launching debugger daemons
         */
        if (ORTE_ERR_UNPACK_READ_PAST_END_OF_BUFFER == rc) {
            goto done;
        }
        *job = ORTE_JOBID_INVALID;
        ORTE_ERROR_LOG(rc);
        goto REPORT_ERROR;
    }
    
    OPAL_OUTPUT_VERBOSE((5, orte_odls_globals.output,
                         "%s odls:construct_child_list unpacking data to launch job %s",
                         ORTE_NAME_PRINT(ORTE_PROC_MY_NAME), ORTE_JOBID_PRINT(*job)));
    
    /* even though we are unpacking an add_local_procs cmd, we cannot assume
     * that no job record for this jobid exists. A race condition exists that
     * could allow another daemon's procs to call us with a collective prior
     * to our unpacking add_local_procs. So lookup the job record for this jobid
     * and see if it already exists
     */
    for (item = opal_list_get_first(&orte_local_jobdata);
         item != opal_list_get_end(&orte_local_jobdata);
         item = opal_list_get_next(item)) {
        orte_odls_job_t *jdat = (orte_odls_job_t*)item;
        
        /* is this the specified job? */
        if (jdat->jobid == *job) {
            OPAL_OUTPUT_VERBOSE((5, orte_odls_globals.output,
                                 "%s odls:construct_child_list found existing jobdat for job %s",
                                 ORTE_NAME_PRINT(ORTE_PROC_MY_NAME), ORTE_JOBID_PRINT(*job)));
            jobdat = jdat;
            break;
        }
    }
    if (NULL == jobdat) {
        /* setup jobdat object for this job */
        OPAL_OUTPUT_VERBOSE((5, orte_odls_globals.output,
                             "%s odls:construct_child_list adding new jobdat for job %s",
                             ORTE_NAME_PRINT(ORTE_PROC_MY_NAME), ORTE_JOBID_PRINT(*job)));
        jobdat = OBJ_NEW(orte_odls_job_t);
        jobdat->jobid = *job;
        opal_list_append(&orte_local_jobdata, &jobdat->super);
    }
    /* if we are doing a timing test, store the time the msg was recvd */
    if (orte_timing) {
        jobdat->launch_msg_recvd.tv_sec = orte_daemon_msg_recvd.tv_sec;
        jobdat->launch_msg_recvd.tv_usec = orte_daemon_msg_recvd.tv_usec;
    }
    
    
    /* UNPACK JOB-SPECIFIC DATA */
    /* unpack the job state so we can know if this is a restart vs initial launch */
    cnt=1;
    if (ORTE_SUCCESS != (rc = opal_dss.unpack(data, &jobdat->state, &cnt, ORTE_JOB_STATE))) {
        *job = ORTE_JOBID_INVALID;
        ORTE_ERROR_LOG(rc);
        goto REPORT_ERROR;
    }
    
    /* unpack the number of nodes involved in this job */
    cnt=1;
    if (ORTE_SUCCESS != (rc = opal_dss.unpack(data, &jobdat->num_nodes, &cnt, ORTE_STD_CNTR))) {
        ORTE_ERROR_LOG(rc);
        goto REPORT_ERROR;
    }    
    /* unpack the number of procs in this launch */
    cnt=1;
    if (ORTE_SUCCESS != (rc = opal_dss.unpack(data, &jobdat->num_procs, &cnt, ORTE_VPID))) {
        ORTE_ERROR_LOG(rc);
        goto REPORT_ERROR;
    }    
    /* unpack the total slots allocated to us */
    cnt=1;
    if (ORTE_SUCCESS != (rc = opal_dss.unpack(data, &jobdat->total_slots_alloc, &cnt, ORTE_STD_CNTR))) {
        ORTE_ERROR_LOG(rc);
        goto REPORT_ERROR;
    }
#if OPAL_HAVE_HWLOC
    /* unpack the binding policy */
    cnt=1;
    if (ORTE_SUCCESS != (rc = opal_dss.unpack(data, &jobdat->binding, &cnt, OPAL_BINDING_POLICY))) {
        ORTE_ERROR_LOG(rc);
        goto REPORT_ERROR;
    }
#endif
    /* unpack the control flags for the job */
    cnt=1;
    if (ORTE_SUCCESS != (rc = opal_dss.unpack(data, &jobdat->controls, &cnt, ORTE_JOB_CONTROL))) {
        ORTE_ERROR_LOG(rc);
        goto REPORT_ERROR;
    }
    /* unpack the stdin target for the job */
    cnt=1;
    if (ORTE_SUCCESS != (rc = opal_dss.unpack(data, &jobdat->stdin_target, &cnt, ORTE_VPID))) {
        ORTE_ERROR_LOG(rc);
        goto REPORT_ERROR;
    }
    /* unpack whether or not process recovery is allowed for this job */
    cnt=1;
    if (ORTE_SUCCESS != (rc = opal_dss.unpack(data, &jobdat->enable_recovery, &cnt, OPAL_BOOL))) {
        ORTE_ERROR_LOG(rc);
        goto REPORT_ERROR;
    }
    /* unpack the number of app_contexts for this job */
    cnt=1;
    if (ORTE_SUCCESS != (rc = opal_dss.unpack(data, &jobdat->num_apps, &cnt, ORTE_APP_IDX))) {
        ORTE_ERROR_LOG(rc);
        goto REPORT_ERROR;
    }
    OPAL_OUTPUT_VERBOSE((5, orte_odls_globals.output,
                         "%s odls:construct_child_list unpacking %ld app_contexts",
                         ORTE_NAME_PRINT(ORTE_PROC_MY_NAME), (long)jobdat->num_apps));
    for (j=0; j < jobdat->num_apps; j++) {
        cnt = 1;
        app = NULL;
        if (ORTE_SUCCESS != (rc = opal_dss.unpack(data, &app, &cnt, ORTE_APP_CONTEXT))) {
            ORTE_ERROR_LOG(rc);
            goto REPORT_ERROR;
        }
        opal_pointer_array_set_item(&jobdat->apps, app->idx, app);
    }
    
    /* unpack the pidmap byte object */
    cnt=1;
    if (ORTE_SUCCESS != (rc = opal_dss.unpack(data, &bo, &cnt, OPAL_BYTE_OBJECT))) {
        ORTE_ERROR_LOG(rc);
        goto REPORT_ERROR;
    }
    /* retain a copy for downloading to child processes */
    if (NULL != jobdat->pmap && NULL != jobdat->pmap->bytes) {
        free(jobdat->pmap->bytes);
        free(jobdat->pmap);
    }
    opal_dss.copy((void**)&jobdat->pmap, bo, OPAL_BYTE_OBJECT);
    /* decode the pidmap  - this will also free the bytes in bo */
    if (ORTE_SUCCESS != (rc = orte_ess.update_pidmap(bo))) {
        ORTE_ERROR_LOG(rc);
        goto REPORT_ERROR;
    }
   
    /* unpack the procs */
    for (j=0; j < jobdat->num_procs; j++) {
        cnt=1;
        if (ORTE_SUCCESS != (rc = opal_dss.unpack(data, &pptr, &cnt, ORTE_PROC))) {
            ORTE_ERROR_LOG(rc);
            goto REPORT_ERROR;
        }

        /* see if it is one of mine */
        ORTE_EPOCH_SET(proc.epoch,orte_ess.proc_get_epoch(&pptr->name));
        /* get the vpid of the daemon that is to host this proc */
        OPAL_OUTPUT_VERBOSE((20, orte_odls_globals.output,
                             "%s odls:constructing child list - looking for daemon for proc %s",
                             ORTE_NAME_PRINT(ORTE_PROC_MY_NAME), ORTE_NAME_PRINT(&pptr->name)));
        if (ORTE_VPID_INVALID == (host_daemon = orte_ess.proc_get_daemon(&pptr->name))) {
            ORTE_ERROR_LOG(ORTE_ERR_NOT_FOUND);
            rc = ORTE_ERR_NOT_FOUND;
            goto REPORT_ERROR;
        }
        
        OPAL_OUTPUT_VERBOSE((20, orte_odls_globals.output,
                             "%s odls:constructing child list - checking proc %s on daemon %s",
                             ORTE_NAME_PRINT(ORTE_PROC_MY_NAME), ORTE_NAME_PRINT(&pptr->name),
                             ORTE_VPID_PRINT(host_daemon)));

        /* does this proc belong to us? */
        if (ORTE_PROC_MY_NAME->vpid == host_daemon) {
            
            OPAL_OUTPUT_VERBOSE((10, orte_odls_globals.output,
                                 "%s odls:constructing child list - found proc %s for me!",
                                 ORTE_NAME_PRINT(ORTE_PROC_MY_NAME), ORTE_NAME_PRINT(&pptr->name)));
            
            add_child = true;
            /* if this job is restarting procs, then we need to treat things
             * a little differently. We may be adding a proc to our local
             * children (if the proc moved here from somewhere else), or we
             * may simply be restarting someone already here.
             */
            if (ORTE_JOB_STATE_RESTART == jobdat->state) {
                /* look for this job on our current list of children */
                for (item = opal_list_get_first(&orte_local_children);
                     item != opal_list_get_end(&orte_local_children);
                     item = opal_list_get_next(item)) {
                    child = (orte_odls_child_t*)item;

                    mask = ORTE_NS_CMP_ALL;

                    if (OPAL_EQUAL == 
                        orte_util_compare_name_fields(mask, child->name, &pptr->name)) {
                        /* do not duplicate this child on the list! */
                        OPAL_OUTPUT_VERBOSE((5, orte_odls_globals.output,
                                             "proc %s is on list and is %s",
                                             ORTE_NAME_PRINT(&pptr->name),
                                             (child->alive) ? "ALIVE" : "DEAD"));
                        add_child = false;
                        child->restarts = pptr->restarts;
                        child->do_not_barrier = true;
                        /* mark that this app_context is being used on this node */
                        app = (orte_app_context_t*)opal_pointer_array_get_item(&jobdat->apps, pptr->app_idx);
                        app->used_on_node = true;
                        break;
                    }
                }
            }
            
            /* if we need to add the child, do so */
            if (add_child) {
                OPAL_OUTPUT_VERBOSE((5, orte_odls_globals.output,
                                     "adding proc %s to my local list",
                                     ORTE_NAME_PRINT(&pptr->name)));
                /* keep tabs of the number of local procs */
                jobdat->num_local_procs++;
                /* add this proc to our child list */
                child = OBJ_NEW(orte_odls_child_t);
                /* copy the name to preserve it */
                if (ORTE_SUCCESS != (rc = opal_dss.copy((void**)&child->name, &pptr->name, ORTE_NAME))) {
                    ORTE_ERROR_LOG(rc);
                    goto REPORT_ERROR;
                }
                child->app_idx = pptr->app_idx;  /* save the index into the app_context objects */
                child->restarts = pptr->restarts;
                /* if the job is in restart mode, the child must not barrier when launched */
                if (ORTE_JOB_STATE_RESTART == jobdat->state) {
                    child->do_not_barrier = true;
                }
#if OPAL_HAVE_HWLOC
                if (NULL != pptr->cpu_bitmap) {
                    child->cpu_bitmap = strdup(pptr->cpu_bitmap);
                }
#endif
                /* mark that this app_context is being used on this node */
                app = (orte_app_context_t*)opal_pointer_array_get_item(&jobdat->apps, pptr->app_idx);
                app->used_on_node = true;
                /* protect operation on the global list of children */
                OPAL_THREAD_LOCK(&orte_odls_globals.mutex);
                opal_list_append(&orte_local_children, &child->super);
                opal_condition_signal(&orte_odls_globals.cond);
                OPAL_THREAD_UNLOCK(&orte_odls_globals.mutex);
            }
        }
        OBJ_RELEASE(pptr);
    }
    
    /* flag that the launch msg has been processed so daemon collectives can proceed */
    OPAL_THREAD_LOCK(&jobdat->lock);
    jobdat->launch_msg_processed = true;
    opal_condition_broadcast(&jobdat->cond);
    OPAL_THREAD_UNLOCK(&jobdat->lock);
    
 done:
    return ORTE_SUCCESS;

 REPORT_ERROR:
    /* we have to report an error back to the HNP so we don't just
     * hang. Although there shouldn't be any errors once this is
     * all debugged, it is still good practice to have a way
     * for it to happen - especially so developers don't have to
     * deal with the hang!
     */
    orte_errmgr.update_state(*job, ORTE_JOB_STATE_NEVER_LAUNCHED,
                             NULL, ORTE_PROC_STATE_UNDEF, 0, rc);
   
    return rc;
}

static int odls_base_default_setup_fork(orte_app_context_t *context,
                                        int32_t num_local_procs,
                                        orte_vpid_t vpid_range,
                                        orte_std_cntr_t total_slots_alloc,
                                        int num_nodes,
                                        bool oversubscribed, char ***environ_copy)
{
    int i;
    char *param, *param2;

    /* setup base environment: copy the current environ and merge
       in the app context environ */
    if (NULL != context->env) {
        *environ_copy = opal_environ_merge(orte_launch_environ, context->env);
    } else {
        *environ_copy = opal_argv_copy(orte_launch_environ);
    }

    /* special case handling for --prefix: this is somewhat icky,
       but at least some users do this.  :-\ It is possible that
       when using --prefix, the user will also "-x PATH" and/or
       "-x LD_LIBRARY_PATH", which would therefore clobber the
       work that was done in the prior pls to ensure that we have
       the prefix at the beginning of the PATH and
       LD_LIBRARY_PATH.  So examine the context->env and see if we
       find PATH or LD_LIBRARY_PATH.  If found, that means the
       prior work was clobbered, and we need to re-prefix those
       variables. */
    for (i = 0; NULL != context->prefix_dir && NULL != context->env && NULL != context->env[i]; ++i) {
        char *newenv;
        
        /* Reset PATH */
        if (0 == strncmp("PATH=", context->env[i], 5)) {
            asprintf(&newenv, "%s/bin:%s",
                     context->prefix_dir, context->env[i] + 5);
            opal_setenv("PATH", newenv, true, environ_copy);
            free(newenv);
        }
        
        /* Reset LD_LIBRARY_PATH */
        else if (0 == strncmp("LD_LIBRARY_PATH=", context->env[i], 16)) {
            asprintf(&newenv, "%s/lib:%s",
                     context->prefix_dir, context->env[i] + 16);
            opal_setenv("LD_LIBRARY_PATH", newenv, true, environ_copy);
            free(newenv);
        }
    }
    
    /* pass my contact info to the local proc so we can talk */
    param = mca_base_param_environ_variable("orte","local_daemon","uri");
    opal_setenv(param, orte_process_info.my_daemon_uri, true, environ_copy);
    free(param);
    
    /* pass the hnp's contact info to the local proc in case it
     * needs it
     */
    if (NULL != orte_process_info.my_hnp_uri) {
        param = mca_base_param_environ_variable("orte","hnp","uri");
        opal_setenv(param, orte_process_info.my_hnp_uri, true, environ_copy);
        free(param);
    }
    
    /* setup yield schedule - do not override any user-supplied directive! */
    if (oversubscribed) {
        param = mca_base_param_environ_variable("mpi", NULL, "yield_when_idle");
        opal_setenv(param, "1", false, environ_copy);
    } else {
        param = mca_base_param_environ_variable("mpi", NULL, "yield_when_idle");
        opal_setenv(param, "0", false, environ_copy);
    }
    free(param);
    
    /* set the app_context number into the environment */
    param = mca_base_param_environ_variable("orte","app","num");
    asprintf(&param2, "%ld", (long)context->idx);
    opal_setenv(param, param2, true, environ_copy);
    free(param);
    free(param2);
    
    /* although the total_slots_alloc is the universe size, users
     * would appreciate being given a public environmental variable
     * that also represents this value - something MPI specific - so
     * do that here. Also required by the ompi_attributes code!
     *
     * AND YES - THIS BREAKS THE ABSTRACTION BARRIER TO SOME EXTENT.
     * We know - just live with it
     */
    asprintf(&param2, "%ld", (long)total_slots_alloc);
    opal_setenv("OMPI_UNIVERSE_SIZE", param2, true, environ_copy);
    free(param2);
    
    /* pass the number of nodes involved in this job */
    param = mca_base_param_environ_variable("orte","num","nodes");
    asprintf(&param2, "%ld", (long)num_nodes);
    opal_setenv(param, param2, true, environ_copy);
    free(param);
    free(param2);

#if OPAL_HAVE_HWLOC
    {
        /* pass a param telling the child what type and model of cpu we are on,
         * if we know it. If hwloc has the value, use what it knows. Otherwise,
         * see if we were explicitly given it and use that value.
         */
        hwloc_obj_t obj;
        char *htmp;
        if (NULL != opal_hwloc_topology) {
            obj = hwloc_get_root_obj(opal_hwloc_topology);
            if (NULL != (htmp = (char*)hwloc_obj_get_info_by_name(obj, "CPUType")) ||
                NULL != (htmp = orte_local_cpu_type)) {
                param = mca_base_param_environ_variable("orte","cpu","type");
                opal_setenv(param, htmp, true, environ_copy);
                free(param);
            }
            if (NULL != (htmp = (char*)hwloc_obj_get_info_by_name(obj, "CPUModel")) ||
                NULL != (htmp = orte_local_cpu_model)) {
                param = mca_base_param_environ_variable("orte","cpu","model");
                opal_setenv(param, htmp, true, environ_copy);
                free(param);
            }
        } else {
            if (NULL != orte_local_cpu_type) {
                param = mca_base_param_environ_variable("orte","cpu","type");
                opal_setenv(param, orte_local_cpu_type, true, environ_copy);
                free(param);
            }
            if (NULL != orte_local_cpu_model) {
                param = mca_base_param_environ_variable("orte","cpu","model");
                opal_setenv(param, orte_local_cpu_model, true, environ_copy);
                free(param);
            }
        }
    }
#endif

    /* get shmem's best component name so we can provide a hint to the shmem
     * framework. the idea here is to have someone figure out what component to
     * select (via the shmem framework) and then have the rest of the
     * components in shmem obey that decision. for more details take a look at
     * the shmem framework in opal.
     */
    if (NULL != (param2 = opal_shmem_base_best_runnable_component_name())) {
        if (NULL != (param =
                     mca_base_param_environ_variable("shmem_RUNTIME_QUERY_hint",
                                                     NULL, NULL))) {
            opal_setenv(param, param2, true, environ_copy);
            free(param);
        }
        free(param2);
    }
    
    /* push data into environment - don't push any single proc
     * info, though. We are setting the environment up on a
     * per-context basis, and will add the individual proc
     * info later. This also sets the mca param to select
     * the "env" component in the SDS framework.
     */
    orte_ess_env_put(vpid_range, num_local_procs, environ_copy);
    
    /* forcibly set the local tmpdir base to match ours */
    param = mca_base_param_environ_variable("orte","tmpdir","base");
    opal_setenv(param, orte_process_info.tmpdir_base, true, environ_copy);
    free(param);

    return ORTE_SUCCESS;
}

static int setup_child(orte_odls_child_t *child, orte_odls_job_t *jobdat, char ***env)
{
    char *param, *value;
    orte_node_rank_t node_rank;
    orte_local_rank_t local_rank;
    int rc;
    
    /* setup the jobid */
    if (ORTE_SUCCESS != (rc = orte_util_convert_jobid_to_string(&value, child->name->jobid))) {
        ORTE_ERROR_LOG(rc);
        return rc;
    }
    if (NULL == (param = mca_base_param_environ_variable("orte","ess","jobid"))) {
        ORTE_ERROR_LOG(ORTE_ERR_OUT_OF_RESOURCE);
        rc = ORTE_ERR_OUT_OF_RESOURCE;
        return rc;
    }
    opal_setenv(param, value, true, env);
    free(param);
    free(value);

#if ORTE_ENABLE_EPOCH
    /* setup the epoch */
    if (ORTE_SUCCESS != (rc = orte_util_convert_epoch_to_string(&value, child->name->epoch))) {
        ORTE_ERROR_LOG(rc);
        return rc;
    }
    if (NULL == (param = mca_base_param_environ_variable("orte","ess","epoch"))) {
        ORTE_ERROR_LOG(ORTE_ERR_OUT_OF_RESOURCE);
        rc = ORTE_ERR_OUT_OF_RESOURCE;
        return rc;
    }
    opal_setenv(param, value, true, env);
    free(param);
    free(value);
#endif

    /* setup the vpid */
    if (ORTE_SUCCESS != (rc = orte_util_convert_vpid_to_string(&value, child->name->vpid))) {
        ORTE_ERROR_LOG(rc);
        return rc;
    }
    if (NULL == (param = mca_base_param_environ_variable("orte","ess","vpid"))) {
        ORTE_ERROR_LOG(ORTE_ERR_OUT_OF_RESOURCE);
        rc = ORTE_ERR_OUT_OF_RESOURCE;
        return rc;
    }
    opal_setenv(param, value, true, env);
    free(param);

    /* although the vpid IS the process' rank within the job, users
     * would appreciate being given a public environmental variable
     * that also represents this value - something MPI specific - so
     * do that here.
     *
     * AND YES - THIS BREAKS THE ABSTRACTION BARRIER TO SOME EXTENT.
     * We know - just live with it
     */
    opal_setenv("OMPI_COMM_WORLD_RANK", value, true, env);
    free(value);  /* done with this now */
    
    /* users would appreciate being given a public environmental variable
     * that also represents the local rank value - something MPI specific - so
     * do that here.
     *
     * AND YES - THIS BREAKS THE ABSTRACTION BARRIER TO SOME EXTENT.
     * We know - just live with it
     */
    if (ORTE_LOCAL_RANK_INVALID == (local_rank = orte_ess.get_local_rank(child->name))) {
        ORTE_ERROR_LOG(ORTE_ERR_VALUE_OUT_OF_BOUNDS);
        rc = ORTE_ERR_VALUE_OUT_OF_BOUNDS;
        return rc;
    }
    asprintf(&value, "%lu", (unsigned long) local_rank);
    opal_setenv("OMPI_COMM_WORLD_LOCAL_RANK", value, true, env);
    free(value);
    
    /* users would appreciate being given a public environmental variable
     * that also represents the node rank value - something MPI specific - so
     * do that here.
     *
     * AND YES - THIS BREAKS THE ABSTRACTION BARRIER TO SOME EXTENT.
     * We know - just live with it
     */
    if (ORTE_NODE_RANK_INVALID == (node_rank = orte_ess.get_node_rank(child->name))) {
        ORTE_ERROR_LOG(ORTE_ERR_VALUE_OUT_OF_BOUNDS);
        rc = ORTE_ERR_VALUE_OUT_OF_BOUNDS;
        return rc;
    }
    asprintf(&value, "%lu", (unsigned long) node_rank);
    opal_setenv("OMPI_COMM_WORLD_NODE_RANK", value, true, env);
    /* set an mca param for it too */
    if(NULL == (param = mca_base_param_environ_variable("orte","ess","node_rank"))) {
        ORTE_ERROR_LOG(ORTE_ERR_OUT_OF_RESOURCE);
        rc = ORTE_ERR_OUT_OF_RESOURCE;
        return rc;
    }
    opal_setenv(param, value, true, env);
    free(param);
    free(value);
    
    /* pass the number of restarts for this proc - will be zero for
     * an initial start, but procs would like to know if they are being
     * restarted so they can take appropriate action
     */
    if (NULL == (param = mca_base_param_environ_variable("orte","num","restarts"))) {
        ORTE_ERROR_LOG(ORTE_ERR_OUT_OF_RESOURCE);
        rc = ORTE_ERR_OUT_OF_RESOURCE;
        return rc;
    }
    asprintf(&value, "%d", child->restarts);
    opal_setenv(param, value, true, env);
    free(param);
    free(value);
    
    /* if the proc should not barrier in orte_init, tell it */
    if (child->do_not_barrier || 0 < child->restarts) {
        if (NULL == (param = mca_base_param_environ_variable("orte","do_not","barrier"))) {
            ORTE_ERROR_LOG(ORTE_ERR_OUT_OF_RESOURCE);
            rc = ORTE_ERR_OUT_OF_RESOURCE;
            return rc;
        }
        opal_setenv(param, "1", true, env);
        free(param);
    }
    
    /* if the proc isn't going to forward IO, then we need to flag that
     * it has "completed" iof termination as otherwise it will never fire
     */
    if (!(ORTE_JOB_CONTROL_FORWARD_OUTPUT & jobdat->controls)) {
        child->iof_complete = true;
    }

    return ORTE_SUCCESS;
}

static int setup_path(orte_app_context_t *app)
{
    int rc;
    char dir[MAXPATHLEN];
    char **argvptr;
    char *pathenv = NULL, *mpiexec_pathenv = NULL;
    char *full_search;

    /* Try to change to the app's cwd and check that the app
       exists and is executable The function will
       take care of outputting a pretty error message, if required
    */
    if (ORTE_SUCCESS != (rc = orte_util_check_context_cwd(app, true))) {
        /* do not ERROR_LOG - it will be reported elsewhere */
        goto CLEANUP;
    }
        
    /* The prior function will have done a chdir() to jump us to
     * wherever the app is to be executed. This could be either where
     * the user specified (via -wdir), or to the user's home directory
     * on this node if nothing was provided. It seems that chdir doesn't
     * adjust the $PWD enviro variable when it changes the directory. This
     * can cause a user to get a different response when doing getcwd vs
     * looking at the enviro variable. To keep this consistent, we explicitly
     * ensure that the PWD enviro variable matches the CWD we moved to.
     *
     * NOTE: if a user's program does a chdir(), then $PWD will once
     * again not match getcwd! This is beyond our control - we are only
     * ensuring they start out matching.
     */
    getcwd(dir, sizeof(dir));
    opal_setenv("PWD", dir, true, &app->env);
        
    /* Search for the OMPI_exec_path and PATH settings in the environment. */
    for (argvptr = app->env; *argvptr != NULL; argvptr++) { 
        if (0 == strncmp("OMPI_exec_path=", *argvptr, 15)) {
            mpiexec_pathenv = *argvptr + 15;
        }
        if (0 == strncmp("PATH=", *argvptr, 5)) {
            pathenv = *argvptr + 5;
        }
    }
        
    /* If OMPI_exec_path is set (meaning --path was used), then create a
       temporary environment to be used in the search for the executable.
       The PATH setting in this temporary environment is a combination of
       the OMPI_exec_path and PATH values.  If OMPI_exec_path is not set,
       then just use existing environment with PATH in it.  */
    if (NULL != mpiexec_pathenv) {
        argvptr = NULL;
        if (pathenv != NULL) {
            asprintf(&full_search, "%s:%s", mpiexec_pathenv, pathenv);
        } else {
            asprintf(&full_search, "%s", mpiexec_pathenv);
        }
        opal_setenv("PATH", full_search, true, &argvptr);
        free(full_search);
    } else {
        argvptr = app->env;
    }
        
    rc = orte_util_check_context_app(app, argvptr);
    /* do not ERROR_LOG - it will be reported elsewhere */
    if (NULL != mpiexec_pathenv) {
        opal_argv_free(argvptr);
    }

 CLEANUP:
    return rc;
}


/* define a timer release point so that we can wait for
 * file descriptors to come available, if necessary
 */
static bool time_is_up;

static void timer_cb(int fd, short event, void *cbdata)
{
    opal_event_t *ev = (opal_event_t*)cbdata;
    
    /* free event */
    if (NULL != ev) {
        free(ev);
    }
    /* declare time is up */
    time_is_up = true;
}

static int compute_num_procs_alive(orte_jobid_t job)
{
    opal_list_item_t *item;
    orte_odls_child_t *child;
    int num_procs_alive = 0;

    for (item  = opal_list_get_first(&orte_local_children);
         item != opal_list_get_end  (&orte_local_children);
         item  = opal_list_get_next(item)) {
        child = (orte_odls_child_t*)item;
        if (!child->alive) {
            continue;
        }
        if (job == child->name->jobid) {
            continue;
        }
        num_procs_alive++;
    }
    return num_procs_alive;
}

int orte_odls_base_default_launch_local(orte_jobid_t job,
                                        orte_odls_base_fork_local_proc_fn_t fork_local)
{
    opal_list_item_t *item;
    orte_app_context_t *app, *dbg;
    orte_odls_child_t *child=NULL;
    bool oversubscribed;
    int rc=ORTE_SUCCESS;
    bool launch_failed=true;
    opal_buffer_t alert;
    orte_std_cntr_t proc_rank;
    orte_odls_job_t *jobdat;
    char basedir[MAXPATHLEN];
    char **argvsav=NULL;
    int inm, j;
    opal_event_t *delay;
    int total_num_local_procs = 0;
    orte_nid_t *nid;
    orte_node_t *node;

    /* protect operations involving the global list of children */
    OPAL_THREAD_LOCK(&orte_odls_globals.mutex);

    /* establish our baseline working directory - we will be potentially
     * bouncing around as we execute various apps, but we will always return
     * to this place as our default directory
     */
    getcwd(basedir, sizeof(basedir));

    /* find the jobdat for this job */
    jobdat = NULL;
    for (item = opal_list_get_first(&orte_local_jobdata);
         item != opal_list_get_end(&orte_local_jobdata);
         item = opal_list_get_next(item)) {
        jobdat = (orte_odls_job_t*)item;
        
        /* is this the specified job? */
        if (jobdat->jobid == job) {
            break;
        }
    }
    if (NULL == jobdat) {
        ORTE_ERROR_LOG(ORTE_ERR_NOT_FOUND);
        rc = ORTE_ERR_NOT_FOUND;
        goto GETOUT;
    }
    
    /* do we have any local procs to launch? */
    if (0 == jobdat->num_local_procs) {
        /* no - just return */
        opal_condition_signal(&orte_odls_globals.cond);
        OPAL_THREAD_UNLOCK(&orte_odls_globals.mutex);
        return ORTE_SUCCESS;
    }
    
    /* see if the mapper thinks we are oversubscribed */
    oversubscribed = false;
    if (ORTE_PROC_IS_HNP) {
        /* just fake it - we don't keep a local nidmap */
        if (NULL == (node = (orte_node_t*)opal_pointer_array_get_item(orte_node_pool, 0))) {
            ORTE_ERROR_LOG(ORTE_ERR_NOT_FOUND);
            rc = ORTE_ERR_NOT_FOUND;
            goto CLEANUP;
        }
        if (node->oversubscribed) {
            oversubscribed = true;
        }
    } else {
        /* RHC: the nidmap will eventually disappear, so for now just
         * make this a non-fatal error
         */
        if (NULL != (nid = orte_util_lookup_nid(ORTE_PROC_MY_NAME))) {
            if (nid->oversubscribed) {
                oversubscribed = true;
            }
        }
    }

#if OPAL_ENABLE_FT_CR == 1
    /*
     * Notify the local SnapC component regarding new job
     */
    if( ORTE_SUCCESS != (rc = orte_snapc.setup_job(job) ) ) {
        /* Silent Failure :/ JJH */
        ORTE_ERROR_LOG(rc);
    }
#endif
    
    /* Now we preload any files that are needed. This is done on a per
     * app context basis
     */
    for (j=0; j < jobdat->apps.size; j++) {
        if (NULL == (app = (orte_app_context_t*)opal_pointer_array_get_item(&jobdat->apps, j))) {
            continue;
        }
        if(app->used_on_node &&
           (app->preload_binary || NULL != app->preload_files)) {
            if( ORTE_SUCCESS != (rc = orte_odls_base_preload_files_app_context(app)) ) {
                ORTE_ERROR_LOG(rc);
                /* JJH: Do not fail here, instead try to execute without the preloaded options*/
            }
        }
    }

#if OPAL_ENABLE_FT_CR == 1
    for (j=0; j < jobdat->apps.size; j++) {
        if (NULL == (app = (orte_app_context_t*)opal_pointer_array_get_item(&jobdat->apps, j))) {
            continue;
        }
        orte_sstore.fetch_app_deps(app);
    }
    orte_sstore.wait_all_deps();
#endif

    /* setup to report the proc state to the HNP */
    OBJ_CONSTRUCT(&alert, opal_buffer_t);
    
    /* compute the total number of local procs currently alive and about to be launched */
    total_num_local_procs = compute_num_procs_alive(job) + jobdat->num_local_procs;

    for (j=0; j < jobdat->apps.size; j++) {
        if (NULL == (app = (orte_app_context_t*)opal_pointer_array_get_item(&jobdat->apps, j))) {
            continue;
        }
        
        /* if this app isn't being used on our node, skip it */
        if (!app->used_on_node) {
            continue;
        }
        
        /* check the system limits - if we are at our max allowed children, then
         * we won't be allowed to do this anyway, so we may as well abort now.
         * According to the documentation, num_procs = 0 is equivalent to
         * no limit, so treat it as unlimited here.
         */
        if (0 < opal_sys_limits.num_procs) {
            OPAL_OUTPUT_VERBOSE((10,  orte_odls_globals.output,
                                 "%s checking limit on num procs %d #children needed %d",
                                 ORTE_NAME_PRINT(ORTE_PROC_MY_NAME),
                                 opal_sys_limits.num_procs, total_num_local_procs));
            if (opal_sys_limits.num_procs < total_num_local_procs) {
                /* don't have enough - wait a little time */
                time_is_up = false;
                ORTE_DETECT_TIMEOUT(&delay, 1000, 1000, -1, timer_cb);
                /* wait */
                ORTE_PROGRESSED_WAIT(time_is_up, 0, 1);
                /* recompute the num local procs */
                total_num_local_procs = compute_num_procs_alive(job) + jobdat->num_local_procs;
                /* see if we still have a problem */
                OPAL_OUTPUT_VERBOSE((10,  orte_odls_globals.output,
                                     "%s rechecking limit on num procs %d #children needed %d",
                                     ORTE_NAME_PRINT(ORTE_PROC_MY_NAME),
                                     opal_sys_limits.num_procs, total_num_local_procs));
                if (opal_sys_limits.num_procs < total_num_local_procs) {
                    /* at the system limit - abort */
                    ORTE_ERROR_LOG(ORTE_ERR_SYS_LIMITS_CHILDREN);
                    rc = ORTE_ERR_SYS_LIMITS_CHILDREN;
                    goto CLEANUP;
                }
            }
        }
        
        
        /* setup the environment for this app */
        if (ORTE_SUCCESS != (rc = odls_base_default_setup_fork(app,
                                                               jobdat->num_local_procs,
                                                               jobdat->num_procs,
                                                               jobdat->total_slots_alloc,
                                                               jobdat->num_nodes,
                                                               oversubscribed,
                                                               &app->env))) {
            
            OPAL_OUTPUT_VERBOSE((10, orte_odls_globals.output,
                                 "%s odls:launch:setup_fork failed with error %s",
                                 ORTE_NAME_PRINT(ORTE_PROC_MY_NAME),
                                 ORTE_ERROR_NAME(rc)));
            
            /* do not ERROR_LOG this failure - it will be reported
             * elsewhere. The launch is going to fail. Since we could have
             * multiple app_contexts, we need to ensure that we flag only
             * the correct one that caused this operation to fail. We then have
             * to flag all the other procs from the app_context as having "not failed"
             * so we can report things out correctly
             */
            /* cycle through children to find those for this jobid */
            for (item = opal_list_get_first(&orte_local_children);
                 item != opal_list_get_end(&orte_local_children);
                 item = opal_list_get_next(item)) {
                child = (orte_odls_child_t*)item;
                if (OPAL_EQUAL == opal_dss.compare(&job, &(child->name->jobid), ORTE_JOBID) &&
                    j == (int)child->app_idx) {
                    child->exit_code = rc;
                }
            }
            /* okay, now tell the HNP we couldn't do it */
            goto CLEANUP;
        }
        
        /* setup the working directory for this app - will jump us
         * to that directory
         */
        if (ORTE_SUCCESS != (rc = setup_path(app))) {
            OPAL_OUTPUT_VERBOSE((5, orte_odls_globals.output,
                                 "%s odls:launch:setup_path failed with error %s(%d)",
                                 ORTE_NAME_PRINT(ORTE_PROC_MY_NAME),
                                 ORTE_ERROR_NAME(rc), rc));
            /* do not ERROR_LOG this failure - it will be reported
             * elsewhere. The launch is going to fail. Since we could have
             * multiple app_contexts, we need to ensure that we flag only
             * the correct one that caused this operation to fail. We then have
             * to flag all the other procs from the app_context as having "not failed"
             * so we can report things out correctly
             */
            /* cycle through children to find those for this jobid */
            for (item = opal_list_get_first(&orte_local_children);
                 item != opal_list_get_end(&orte_local_children);
                 item = opal_list_get_next(item)) {
                child = (orte_odls_child_t*)item;
                if (OPAL_EQUAL == opal_dss.compare(&job, &(child->name->jobid), ORTE_JOBID) &&
                    j == (int)child->app_idx) {
                    child->exit_code = rc;
                }
            }
            /* okay, now tell the HNP we couldn't do it */
            goto CLEANUP;
        }

        /* okay, now let's launch all the local procs for this app using the provided fork_local fn */
        for (proc_rank = 0, item = opal_list_get_first(&orte_local_children);
             item != opal_list_get_end(&orte_local_children);
             item = opal_list_get_next(item)) {
            child = (orte_odls_child_t*)item;
            
            /* does this child belong to this app? */
            if (j != (int)child->app_idx) {
                continue;
            }
            
            /* is this child already alive? This can happen if
             * we are asked to launch additional processes.
             * If it has been launched, then do nothing
             */
            if (child->alive) {
                
                OPAL_OUTPUT_VERBOSE((5, orte_odls_globals.output,
                                     "%s odls:launch child %s is already alive",
                                     ORTE_NAME_PRINT(ORTE_PROC_MY_NAME),
                                     ORTE_NAME_PRINT(child->name)));
                
                continue;
            }
            
            /* do we have a child from the specified job. Because the
             * job could be given as a WILDCARD value, we must use
             * the dss.compare function to check for equality.
             */
            if (OPAL_EQUAL != opal_dss.compare(&job, &(child->name->jobid), ORTE_JOBID)) {
                
                OPAL_OUTPUT_VERBOSE((5, orte_odls_globals.output,
                                     "%s odls:launch child %s is not in job %s being launched",
                                     ORTE_NAME_PRINT(ORTE_PROC_MY_NAME),
                                     ORTE_NAME_PRINT(child->name),
                                     ORTE_JOBID_PRINT(job)));
                
                continue;
            }
            
            OPAL_OUTPUT_VERBOSE((5, orte_odls_globals.output,
                                 "%s odls:launch working child %s",
                                 ORTE_NAME_PRINT(ORTE_PROC_MY_NAME),
                                 ORTE_NAME_PRINT(child->name)));

            /* ensure we clear any prior info regarding state or exit status in
             * case this is a restart
             */
            child->state = ORTE_PROC_STATE_FAILED_TO_START;
            child->exit_code = 0;
            child->waitpid_recvd = false;
            /* if we are not forwarding output for this job, then
             * flag iof as complete
             */
            if (ORTE_JOB_CONTROL_FORWARD_OUTPUT & jobdat->controls) {
                child->iof_complete = false;
            } else {
                child->iof_complete = true;
            }
            child->coll_recvd = false;
            child->pid = 0;
            if (NULL != child->rml_uri) {
                free(child->rml_uri);
                child->rml_uri = NULL;
            }
            
            /* check to see if we have enough available file descriptors
             * to launch another child - if not, then let's wait a little
             * while to see if some come free. This can happen if we are
             * in a tight loop over comm_spawn
             */
            if (0 < opal_sys_limits.num_files) {
                int limit;
                limit = 4*total_num_local_procs + 6;
                OPAL_OUTPUT_VERBOSE((10,  orte_odls_globals.output,
                                     "%s checking limit on file descriptors %d need %d",
                                     ORTE_NAME_PRINT(ORTE_PROC_MY_NAME),
                                     opal_sys_limits.num_files, limit));
                if (opal_sys_limits.num_files < limit) {
                    /* don't have enough - wait a little time */
                    time_is_up = false;
                    ORTE_DETECT_TIMEOUT(&delay, 1000, 1000, -1, timer_cb);
                    /* wait */
                    ORTE_PROGRESSED_WAIT(time_is_up, 0, 1);
                    /* recompute the num procs alive */
                    total_num_local_procs = compute_num_procs_alive(job) + jobdat->num_local_procs;
                    /* see if we still have a problem */
                    limit = 4*total_num_local_procs + 6;
                    OPAL_OUTPUT_VERBOSE((10,  orte_odls_globals.output,
                                         "%s rechecking limit on file descriptors %d need %d",
                                         ORTE_NAME_PRINT(ORTE_PROC_MY_NAME),
                                         opal_sys_limits.num_files, limit));
                    if (opal_sys_limits.num_files < limit) {
                        /* nope - abort */
                        ORTE_ERROR_LOG(ORTE_ERR_SYS_LIMITS_PIPES);
                        child->exit_code = rc;
                        goto CLEANUP;
                    }
                }
            }
            
            /* did the user request we display output in xterms? */
            if (NULL != orte_xterm) {
                opal_list_item_t *nmitem;
                orte_namelist_t *nm;
                /* see if this rank is one of those requested */
                for (nmitem = opal_list_get_first(&orte_odls_globals.xterm_ranks);
                     nmitem != opal_list_get_end(&orte_odls_globals.xterm_ranks);
                     nmitem = opal_list_get_next(nmitem)) {
                    nm = (orte_namelist_t*)nmitem;
                    if (ORTE_VPID_WILDCARD == nm->name.vpid ||
                        child->name->vpid == nm->name.vpid) {
                        /* we want this one - modify the app's command to include
                         * the orte xterm cmd. Need to be careful, though, that we
                         * don't modify the app for ALL ranks that use it! So we
                         * will create a copy of the argv so we can restore it later
                         */
                        argvsav = opal_argv_copy(app->argv);
                        /* free the argv */
                        opal_argv_free(app->argv);
                        app->argv = NULL;
                        /* now create a new one that starts with the xtermcmd */
                        for (inm=0; inm < opal_argv_count(orte_odls_globals.xtermcmd); inm++) {
                            opal_argv_append_nosize(&app->argv, orte_odls_globals.xtermcmd[inm]);
                        }
                        /* insert the rank into the correct place as a window title */
                        free(app->argv[2]);
                        asprintf(&app->argv[2], "Rank %s", ORTE_VPID_PRINT(child->name->vpid));
                        /* add back the original argv */
                        for (inm=0; inm < opal_argv_count(argvsav); inm++) {
                            opal_argv_append_nosize(&app->argv, argvsav[inm]);
                        }
                        /* the app exe name itself is in the argvsav array, so
                         * we can recover it from there later
                         */
                        free(app->app);
                        app->app = strdup(orte_odls_globals.xtermcmd[0]);
                        break;
                    } else if (jobdat->num_procs <= nm->name.vpid) {  /* check for bozo case */
                        /* can't be done! */
                        orte_show_help("help-orte-odls-base.txt",
                                       "orte-odls-base:xterm-rank-out-of-bounds",
                                       true, nm->name.vpid, jobdat->num_procs);
                        rc = ORTE_ERR_VALUE_OUT_OF_BOUNDS;
                        child->exit_code = ORTE_ERR_SILENT;
                        goto CLEANUP;
                    }
                    
                }
            } else if (NULL != orte_fork_agent) {
                /* we were given a fork agent - use it */
                argvsav = opal_argv_copy(app->argv);
                /* free the argv */
                opal_argv_free(app->argv);
                app->argv = NULL;
                /* now create a new one that starts with the fork agent */
                app->argv = opal_argv_copy(orte_fork_agent);
                /* add back the original argv */
                for (inm=0; NULL != argvsav[inm]; inm++) {
                    opal_argv_append_nosize(&app->argv, argvsav[inm]);
                }
                /* the app exe name itself is in the argvsav array, so
                 * we can recover it from there later
                 */
                free(app->app);
                app->app = opal_path_findv(orte_fork_agent[0], X_OK, orte_launch_environ, NULL);
                if (NULL == app->app) {
                    orte_show_help("help-orte-odls-base.txt",
                                   "orte-odls-base:fork-agent-not-found",
                                   true, orte_process_info.nodename, orte_fork_agent[0]);
                    rc = ORTE_ERR_SILENT;
                    child->exit_code = ORTE_ERR_SILENT;
                    goto CLEANUP;
                }
            }

            /* setup the rest of the environment with the proc-specific items - these
             * will be overwritten for each child
             */
            if (ORTE_SUCCESS != (rc = setup_child(child, jobdat, &app->env))) {
                ORTE_ERROR_LOG(rc);
                goto CLEANUP;
            }

            /* if we are timing things, record when we are going to launch this proc */
            if (orte_timing) {
                gettimeofday(&child->starttime, NULL);
            }
            
            /* must unlock prior to fork to keep things clean in the
             * event library
             */
            opal_condition_signal(&orte_odls_globals.cond);
            OPAL_THREAD_UNLOCK(&orte_odls_globals.mutex);
            
#if OPAL_ENABLE_FT_CR == 1
            /*
             * OPAL CRS components need the opportunity to take action before a process
             * is forked.
             * Needs access to:
             *   - Environment
             *   - Rank/ORTE Name
             *   - Binary to exec
             */
            if( NULL != opal_crs.crs_prelaunch ) {
                if( OPAL_SUCCESS != (rc = opal_crs.crs_prelaunch(child->name->vpid,
                                                                 orte_sstore_base_prelaunch_location,
                                                                 &(app->app),
                                                                 &(app->cwd),
                                                                 &(app->argv),
                                                                 &(app->env) ) ) ) {
                    ORTE_ERROR_LOG(rc);
                    goto CLEANUP;
                }
            }
#endif
            if (5 < opal_output_get_verbosity(orte_odls_globals.output)) {
                opal_output(orte_odls_globals.output, "%s odls:launch: spawning child %s",
                            ORTE_NAME_PRINT(ORTE_PROC_MY_NAME),
                            ORTE_NAME_PRINT(child->name));
                
                /* dump what is going to be exec'd */
                if (7 < opal_output_get_verbosity(orte_odls_globals.output)) {
                    opal_dss.dump(orte_odls_globals.output, app, ORTE_APP_CONTEXT);
                }
            }
            
            rc = fork_local(app, child, app->env, jobdat);
            /* reaquire lock so we don't double unlock... */
            OPAL_THREAD_LOCK(&orte_odls_globals.mutex);
            if (ORTE_SUCCESS != rc) {
                /* do NOT ERROR_LOG this error - it generates
                 * a message/node as most errors will be common
                 * across the entire cluster. Instead, we let orterun
                 * output a consolidated error message for us
                 */
                goto CLEANUP;
            } else {
                child->alive = true;
                child->state = ORTE_PROC_STATE_LAUNCHED;
                if (ORTE_SUCCESS != (rc = orte_errmgr.update_state(child->name->jobid, ORTE_JOB_STATE_LAUNCHED,
                                                                   child->name, child->state,
                                                                   child->pid, child->exit_code))) {
                    ORTE_ERROR_LOG(rc);
                    goto CLEANUP;
                }
            }
            /* move to next processor */
            proc_rank++;
            /* reset the exe name, if necessary */
            if (NULL != argvsav) {
                /* release the current argv array */
                opal_argv_free(app->argv);
                /* restore the original one */
                app->argv = argvsav;
                argvsav = NULL;
                /* the app exe name itself is now in the argv[0] posn */
                free(app->app);
                app->app = strdup(app->argv[0]);
            }
        }  /* complete launching all children for this app */
        /* reset our working directory back to our default location - if we
         * don't do this, then we will be looking for relative paths starting
         * from the last wdir option specified by the user. Thus, we would
         * be requiring that the user keep track on the cmd line of where
         * each app was located relative to the prior app, instead of relative
         * to their current location
         */
        chdir(basedir);
    }
    launch_failed = false;

 CLEANUP:
    /* ensure we reset our working directory back to our default location  */
    chdir(basedir);

    OPAL_OUTPUT_VERBOSE((5, orte_odls_globals.output,
                         "%s odls:launch reporting job %s launch status",
                         ORTE_NAME_PRINT(ORTE_PROC_MY_NAME),
                         ORTE_JOBID_PRINT(job)));
    
    /* if the launch failed, we need to flag all the procs from this job
     * that didn't launch as having failed, or else we will hang
     */
    if (launch_failed) {
        if (ORTE_SUCCESS != (rc = orte_errmgr.update_state(jobdat->jobid, ORTE_JOB_STATE_FAILED_TO_START,
                                                           NULL, ORTE_PROC_STATE_UNDEF, 0,
                                                           child->exit_code))) {
            ORTE_ERROR_LOG(rc);
        }
    } else {
        /* if the launch succeeded, check to see if we need to
         * co-locate any debugger daemons so that they get launched
         * before we report anything to the HNP. This ensures that
         * the debugger daemons are ready-to-go before mpirun returns
         * from the plm.spawn command. Only spawn the debugger, though,
         * if we have local children - otherwise, the HNP could spawn
         * a debugger when it doesn't have any local procs
         */
        if (NULL != orte_odls_globals.debugger &&
            !orte_odls_globals.debugger_launched &&
            0 < opal_list_get_size(&orte_local_children)) {
            dbg = (orte_app_context_t*)opal_pointer_array_get_item(&orte_odls_globals.debugger->apps, 0);
            OPAL_OUTPUT_VERBOSE((5, orte_odls_globals.output,
                                 "%s odls:launch forking debugger %s with %s",
                                 ORTE_NAME_PRINT(ORTE_PROC_MY_NAME), dbg->app,
                                 (ORTE_JOB_CONTROL_FORWARD_OUTPUT & orte_odls_globals.debugger->controls) ? "output forwarded" : "no output"));
            
            odls_base_default_setup_fork(dbg, 1, orte_process_info.num_procs,
                                         orte_process_info.num_procs,
                                         orte_process_info.num_procs, false, &dbg->env);
            fork_local(dbg, NULL, dbg->env, orte_odls_globals.debugger);
            orte_odls_globals.debugger_launched = true;
            if (ORTE_SUCCESS != (rc = orte_errmgr.update_state(orte_odls_globals.debugger->jobid,
                                                               ORTE_JOB_STATE_RUNNING,
                                                               NULL, ORTE_PROC_STATE_UNDEF, 0,
                                                               ORTE_ERROR_DEFAULT_EXIT_CODE))) {
                ORTE_ERROR_LOG(rc);
            }
        }
        
        if (ORTE_SUCCESS != (rc = orte_errmgr.update_state(jobdat->jobid, ORTE_JOB_STATE_RUNNING,
                                                           NULL, ORTE_PROC_STATE_UNDEF, 0,
                                                           ORTE_ERROR_DEFAULT_EXIT_CODE))) {
            ORTE_ERROR_LOG(rc);
        }
        
        OPAL_OUTPUT_VERBOSE((5, orte_odls_globals.output,
                             "%s odls:launch setting waitpids",
                             ORTE_NAME_PRINT(ORTE_PROC_MY_NAME)));
        
        /* start the sensors for this job (if any) */
        orte_sensor.start(jobdat->jobid);
        
        /* if the launch didn't fail, setup the waitpids on the children */
        for (item = opal_list_get_first(&orte_local_children);
             item != opal_list_get_end(&orte_local_children);
             item = opal_list_get_next(item)) {
            child = (orte_odls_child_t*)item;
            
            if (child->name->jobid == jobdat->jobid) {
                OPAL_THREAD_UNLOCK(&orte_odls_globals.mutex);
                orte_wait_cb(child->pid, odls_base_default_wait_local_proc, NULL);
                OPAL_THREAD_LOCK(&orte_odls_globals.mutex);
            }
        }
    }

 GETOUT:
    opal_condition_signal(&orte_odls_globals.cond);
    OPAL_THREAD_UNLOCK(&orte_odls_globals.mutex);
    return rc;
}

int orte_odls_base_default_deliver_message(orte_jobid_t job, opal_buffer_t *buffer, orte_rml_tag_t tag)
{
    int rc, exit_status = ORTE_SUCCESS;
    opal_list_item_t *item;
    orte_odls_child_t *child;
    
    /* protect operations involving the global list of children */
    OPAL_THREAD_LOCK(&orte_odls_globals.mutex);
    
    for (item = opal_list_get_first(&orte_local_children);
         item != opal_list_get_end(&orte_local_children);
         item = opal_list_get_next(item)) {
        child = (orte_odls_child_t*)item;
        
        /* do we have a child from the specified job. Because the
         *  job could be given as a WILDCARD value, we must use
         *  the dss.compare function to check for equality.
         */
        if (!child->alive ||
            OPAL_EQUAL != opal_dss.compare(&job, &(child->name->jobid), ORTE_JOBID)) {
            continue;
        }
        
        OPAL_OUTPUT_VERBOSE((5, orte_odls_globals.output,
                             "%s odls: sending message to tag %lu on child %s",
                             ORTE_NAME_PRINT(ORTE_PROC_MY_NAME),
                             (unsigned long)tag, ORTE_NAME_PRINT(child->name)));
        
        /* if so, send the message */
        rc = orte_rml.send_buffer(child->name, buffer, tag, 0);
        if (rc < 0 && rc != ORTE_ERR_ADDRESSEE_UNKNOWN) {
            /* ignore if the addressee is unknown as a race condition could
             * have allowed the child to exit before we send it a barrier
             * due to the vagaries of the event library.
             *
             * If we do get an error it is likely that the orte_local_children
             * has changed to reflect it, so we can no longer deliver messages.
             * So just break out and return the error code.
             */
            ORTE_ERROR_LOG(rc);
            exit_status = rc;
            goto cleanup;
        }
    }

 cleanup:
    opal_condition_signal(&orte_odls_globals.cond);
    OPAL_THREAD_UNLOCK(&orte_odls_globals.mutex);

    return exit_status;
}


/**
*  Pass a signal to my local procs
 */

int orte_odls_base_default_signal_local_procs(const orte_process_name_t *proc, int32_t signal,
                                              orte_odls_base_signal_local_fn_t signal_local)
{
    int rc;
    opal_list_item_t *item;
    orte_odls_child_t *child;
    
    OPAL_OUTPUT_VERBOSE((5, orte_odls_globals.output,
                         "%s odls: signaling proc %s",
                         ORTE_NAME_PRINT(ORTE_PROC_MY_NAME),
                         (NULL == proc) ? "NULL" : ORTE_NAME_PRINT(proc)));
    
    /* protect operations involving the global list of children */
    OPAL_THREAD_LOCK(&orte_odls_globals.mutex);
    
    /* if procs is NULL, then we want to signal all
     * of the local procs, so just do that case
     */
    if (NULL == proc) {
        rc = ORTE_SUCCESS;  /* pre-set this as an empty list causes us to drop to bottom */
        for (item = opal_list_get_first(&orte_local_children);
             item != opal_list_get_end(&orte_local_children);
             item = opal_list_get_next(item)) {
            child = (orte_odls_child_t*)item;
            if (ORTE_SUCCESS != (rc = signal_local(child->pid, (int)signal))) {
                ORTE_ERROR_LOG(rc);
            }
        }
        opal_condition_signal(&orte_odls_globals.cond);
        OPAL_THREAD_UNLOCK(&orte_odls_globals.mutex);
        return rc;
    }
    
    /* we want it sent to some specified process, so find it */
    for (item = opal_list_get_first(&orte_local_children);
         item != opal_list_get_end(&orte_local_children);
         item = opal_list_get_next(item)) {
        child = (orte_odls_child_t*)item;
        if (OPAL_EQUAL == opal_dss.compare(&(child->name), (orte_process_name_t*)proc, ORTE_NAME)) {
            /* unlock before signaling as this may generate a callback */
            opal_condition_signal(&orte_odls_globals.cond);
            OPAL_THREAD_UNLOCK(&orte_odls_globals.mutex);
            if (ORTE_SUCCESS != (rc = signal_local(child->pid, (int)signal))) {
                ORTE_ERROR_LOG(rc);
            }
            return rc;
        }
    }
    
    /* only way to get here is if we couldn't find the specified proc.
     * report that as an error and return it
     */
    ORTE_ERROR_LOG(ORTE_ERR_NOT_FOUND);
    opal_condition_signal(&orte_odls_globals.cond);
    OPAL_THREAD_UNLOCK(&orte_odls_globals.mutex);
    return ORTE_ERR_NOT_FOUND;
}

void orte_odls_base_setup_singleton_jobdat(orte_jobid_t jobid)
{
    orte_odls_job_t *jobdat;
    orte_vpid_t vpid1;
    int32_t one32;
    orte_local_rank_t lrank;
    orte_node_rank_t nrank;
    opal_buffer_t buffer;
    opal_byte_object_t *bo;
    int rc;
#if OPAL_HAVE_HWLOC
    opal_hwloc_level_t bind_level;
    unsigned int bind_idx;
#endif

    /* create a job tracking object for it */
    jobdat = OBJ_NEW(orte_odls_job_t);
    jobdat->jobid = jobid;
    jobdat->num_procs = 1;
    jobdat->num_local_procs = 1;
    opal_list_append(&orte_local_jobdata, &jobdat->super);
    /* need to setup a pidmap for it */
    OBJ_CONSTRUCT(&buffer, opal_buffer_t);
    opal_dss.pack(&buffer, &jobid, 1, ORTE_JOBID); /* jobid */
    vpid1 = 1;
    opal_dss.pack(&buffer, &vpid1, 1, ORTE_VPID); /* num_procs */
#if OPAL_HAVE_HWLOC
    bind_level = OPAL_HWLOC_NODE_LEVEL;
    opal_dss.pack(&buffer, &bind_level, 1, OPAL_HWLOC_LEVEL_T); /* num_procs */
#endif
    one32 = 0;
    opal_dss.pack(&buffer, &one32, 1, OPAL_INT32); /* node index */
    lrank = 0;
    opal_dss.pack(&buffer, &lrank, 1, ORTE_LOCAL_RANK);  /* local rank */
    nrank = 0;
    opal_dss.pack(&buffer, &nrank, 1, ORTE_NODE_RANK);  /* node rank */
#if OPAL_HAVE_HWLOC
    bind_idx = 0;
    opal_dss.pack(&buffer, &bind_idx, 1, OPAL_UINT);
#endif
    /* setup a byte object and unload the packed data to it */
    bo = (opal_byte_object_t*)malloc(sizeof(opal_byte_object_t));
    opal_dss.unload(&buffer, (void**)&bo->bytes, &bo->size);
    OBJ_DESTRUCT(&buffer);
    /* save a copy to send back to the proc */
    opal_dss.copy((void**)&jobdat->pmap, bo, OPAL_BYTE_OBJECT);
    /* update our ess data - this will release the byte object's data */
    if (ORTE_SUCCESS != (rc = orte_ess.update_pidmap(bo))) {
        ORTE_ERROR_LOG(rc);
    }
    free(bo);
    
    /* if we don't yet have a daemon map, then we have to generate one
     * to pass back to it
     */
    if (NULL == orte_odls_globals.dmap) {
        orte_odls_globals.dmap = (opal_byte_object_t*)malloc(sizeof(opal_byte_object_t));
        /* construct a nodemap */
        if (ORTE_SUCCESS != (rc = orte_util_encode_nodemap(orte_odls_globals.dmap))) {
            ORTE_ERROR_LOG(rc);
        }
        /* we also need to update our local nidmap - copy the dmap
         * as this will release the byte object's data. The copy function
         * will automatically malloc the bo itself, so we don't need to do so here
         */
        opal_dss.copy((void**)&bo, orte_odls_globals.dmap, OPAL_BYTE_OBJECT);
        if (ORTE_SUCCESS != (rc = orte_ess.update_nidmap(bo))) {
            ORTE_ERROR_LOG(rc);
        }
        free(bo);
    }
    /* flag that the "launch msg" has been processed so that daemon
     * collectives can proceed
     */
    jobdat->launch_msg_processed = true;
}

int orte_odls_base_default_require_sync(orte_process_name_t *proc,
                                        opal_buffer_t *buf,
                                        bool drop_nidmap)
{
    opal_buffer_t buffer;
    opal_list_item_t *item;
    orte_odls_child_t *child;
    orte_std_cntr_t cnt;
    int rc=ORTE_SUCCESS;
    bool found=false, registering=false;
    orte_odls_job_t *jobdat, *jdat;

    OPAL_OUTPUT_VERBOSE((5, orte_odls_globals.output,
                         "%s odls: require sync on child %s",
                         ORTE_NAME_PRINT(ORTE_PROC_MY_NAME),
                         ORTE_NAME_PRINT(proc)));
 
    /* protect operations involving the global list of children */
    OPAL_THREAD_LOCK(&orte_odls_globals.mutex);
    
    for (item = opal_list_get_first(&orte_local_children);
         item != opal_list_get_end(&orte_local_children);
         item = opal_list_get_next(item)) {
        child = (orte_odls_child_t*)item;
        
        /* find this child */
        if (OPAL_EQUAL == opal_dss.compare(proc, child->name, ORTE_NAME)) {

            OPAL_OUTPUT_VERBOSE((5, orte_odls_globals.output,
                                 "%s odls: registering sync on child %s",
                                 ORTE_NAME_PRINT(ORTE_PROC_MY_NAME),
                                 ORTE_NAME_PRINT(child->name)));
            
            found = true;
            break;
        }
    }
    
    /* if it wasn't found on the list, then we need to add it - must have
     * come from a singleton
     */
    if (!found) {
        OPAL_OUTPUT_VERBOSE((5, orte_odls_globals.output,
                             "%s odls: registering sync on singleton %s",
                             ORTE_NAME_PRINT(ORTE_PROC_MY_NAME),
                             ORTE_NAME_PRINT(proc)));
        child = OBJ_NEW(orte_odls_child_t);
        if (ORTE_SUCCESS != (rc = opal_dss.copy((void**)&child->name, proc, ORTE_NAME))) {
            ORTE_ERROR_LOG(rc);
            goto CLEANUP;
        }
        opal_list_append(&orte_local_children, &child->super);
        /* we don't know any other info about the child, so just indicate it's
         * alive
         */
        child->alive = true;
        /* setup jobdat object for its job so daemon collectives work */
        orte_odls_base_setup_singleton_jobdat(proc->jobid);
    }
    
    /* if the contact info is already set, then we are "de-registering" the child
     * so free the info and set it to NULL
     */
    if (child->init_recvd && NULL != child->rml_uri) {
        child->fini_recvd = true;
        OPAL_OUTPUT_VERBOSE((5, orte_odls_globals.output,
                             "%s odls: require sync deregistering child %s",
                             ORTE_NAME_PRINT(ORTE_PROC_MY_NAME),
                             ORTE_NAME_PRINT(child->name)));
    } else {
        /* if the contact info is not set, then we are registering the child so
         * unpack the contact info from the buffer and store it
         */
        OPAL_OUTPUT_VERBOSE((5, orte_odls_globals.output,
                             "%s odls: require sync registering child %s",
                             ORTE_NAME_PRINT(ORTE_PROC_MY_NAME),
                             ORTE_NAME_PRINT(child->name)));
        child->init_recvd = true;
        registering = true;
        cnt = 1;
        if (ORTE_SUCCESS != (rc = opal_dss.unpack(buf, &(child->rml_uri), &cnt, OPAL_STRING))) {
            ORTE_ERROR_LOG(rc);
        }
    }
    
    /* ack the call */
    OBJ_CONSTRUCT(&buffer, opal_buffer_t);
    /* do they want the nidmap? */
    if (drop_nidmap) {
        /* get the jobdata object */
        jobdat = NULL;
        for (item = opal_list_get_first(&orte_local_jobdata);
             item != opal_list_get_end(&orte_local_jobdata);
             item = opal_list_get_next(item)) {
            jdat = (orte_odls_job_t*)item;
            if (jdat->jobid == child->name->jobid) {
                jobdat = jdat;
                break;
            }
        }
        if (NULL == jobdat) {
            ORTE_ERROR_LOG(ORTE_ERR_NOT_FOUND);
            goto CLEANUP;
        }
        
        OPAL_OUTPUT_VERBOSE((5, orte_odls_globals.output,
                             "%s odls:sync nidmap requested for job %s",
                             ORTE_NAME_PRINT(ORTE_PROC_MY_NAME),
                             ORTE_JOBID_PRINT(jobdat->jobid)));
        /* the proc needs a copy of both the daemon/node map, and
         * the process map for its peers
         */
        if (NULL != orte_odls_globals.dmap &&
            NULL != jobdat->pmap) {
            /* the data is in the local byte objects - send them */
            OPAL_OUTPUT_VERBOSE((5, orte_odls_globals.output,
                                 "%s odls:sync sending byte object",
                                 ORTE_NAME_PRINT(ORTE_PROC_MY_NAME)));
#if OPAL_HAVE_HWLOC
            /* send the local topology so the individual apps
             * don't hammer the system to collect it themselves
             */
            opal_dss.pack(&buffer, &opal_hwloc_topology, 1, OPAL_HWLOC_TOPO);
#endif
            opal_dss.pack(&buffer, &orte_odls_globals.dmap, 1, OPAL_BYTE_OBJECT);
            opal_dss.pack(&buffer, &jobdat->pmap, 1, OPAL_BYTE_OBJECT);
        }
    }
    
    OPAL_OUTPUT_VERBOSE((5, orte_odls_globals.output,
                         "%s odls: sending sync ack to child %s with %ld bytes of data",
                         ORTE_NAME_PRINT(ORTE_PROC_MY_NAME),
                        ORTE_NAME_PRINT(proc), (long)buffer.bytes_used));
    
    if (0 > (rc = orte_rml.send_buffer(proc, &buffer, ORTE_RML_TAG_SYNC, 0))) {
        ORTE_ERROR_LOG(rc);
        OBJ_DESTRUCT(&buffer);
        goto CLEANUP;
    }
    rc = ORTE_SUCCESS;
    OBJ_DESTRUCT(&buffer);

    OPAL_OUTPUT_VERBOSE((5, orte_odls_globals.output,
                         "%s odls: Finished sending sync ack to child %s (Registering %s)",
                         ORTE_NAME_PRINT(ORTE_PROC_MY_NAME),
                         ORTE_NAME_PRINT(proc), (registering ? "True" : "False") ));

    /* if we are deregistering, then we are done */
    if (!registering) {
        orte_routed.delete_route(child->name);
        if( NULL != child->rml_uri ) {
            free(child->rml_uri);
            child->rml_uri = NULL;
        }
        orte_errmgr.update_state(ORTE_JOBID_INVALID, ORTE_JOB_STATE_UNDEF,
                                 proc, ORTE_PROC_STATE_DEREGISTERED, 0, 0);
        goto CLEANUP;
    }
    
    /* update the proc state */
    orte_errmgr.update_state(ORTE_JOBID_INVALID, ORTE_JOB_STATE_UNDEF,
                             proc, ORTE_PROC_STATE_REGISTERED, 0, 0);
    
CLEANUP:
    opal_condition_signal(&orte_odls_globals.cond);
    OPAL_THREAD_UNLOCK(&orte_odls_globals.mutex);
    return rc;
}

/* receive external-to-odls notification that a proc has met some completion
 * requirements
 */
void orte_odls_base_notify_iof_complete(orte_process_name_t *proc)
{
    orte_odls_child_t *child;
    opal_list_item_t *item;
    int rc;
    orte_ns_cmp_bitmask_t mask;
    
    OPAL_OUTPUT_VERBOSE((5, orte_odls_globals.output,
                         "%s odls:notify_iof_complete for child %s",
                         ORTE_NAME_PRINT(ORTE_PROC_MY_NAME),
                         ORTE_NAME_PRINT(proc)));
    
    /* since we are going to be working with the global list of
     * children, we need to protect that list from modification
     * by other threads. This will also be used to protect us
     * from race conditions on any abort situation
     */
    OPAL_THREAD_LOCK(&orte_odls_globals.mutex);
    
    /* find this child */
    for (item = opal_list_get_first(&orte_local_children);
         item != opal_list_get_end(&orte_local_children);
         item = opal_list_get_next(item)) {
        child = (orte_odls_child_t*)item;

        mask = ORTE_NS_CMP_ALL;

        if (OPAL_EQUAL == orte_util_compare_name_fields(mask, child->name, proc)) { /* found it */
            goto GOTCHILD;
        }
    }
    /* get here if we didn't find the child, or if the specified child
     * is already dead. If the latter, then we have a problem as it
     * means we are detecting it exiting multiple times
     */
    
    OPAL_OUTPUT_VERBOSE((5, orte_odls_globals.output,
                         "%s odls:proc_complete did not find child %s in table!",
                         ORTE_NAME_PRINT(ORTE_PROC_MY_NAME),
                         ORTE_NAME_PRINT(proc)));
    
    /* it's just a race condition - don't error log it */
    opal_condition_signal(&orte_odls_globals.cond);
    OPAL_THREAD_UNLOCK(&orte_odls_globals.mutex);
    return;
    
GOTCHILD:
    /* flag the iof as complete */
    child->iof_complete = true;
    /* now check to see if the proc is truly done */
    if (child->waitpid_recvd) {
        /* CHILD IS COMPLETE */
        child->alive = false;
        
        /* Release only the stdin IOF file descriptor for this child, if one
         * was defined. File descriptors for the other IOF channels - stdout,
         * stderr, and stddiag - were released when their associated pipes
         * were cleared and closed due to termination of the process
         */
        if (NULL != orte_iof.close) {
            orte_iof.close(proc, ORTE_IOF_STDIN);
        }
        /* Clean up the session directory as if we were the process
         * itself.  This covers the case where the process died abnormally
         * and didn't cleanup its own session directory.
         */
        orte_session_dir_finalize(proc);
        /* alert the errmgr */
        if (ORTE_SUCCESS != (rc = orte_errmgr.update_state(ORTE_JOBID_INVALID, ORTE_JOB_STATE_UNDEF,
                                                           proc, child->state, child->pid,
                                                           child->exit_code))) {
            ORTE_ERROR_LOG(rc);
        }
    }
    
    opal_condition_signal(&orte_odls_globals.cond);
    OPAL_THREAD_UNLOCK(&orte_odls_globals.mutex);
}

void orte_odls_base_default_report_abort(orte_process_name_t *proc)
{
    orte_odls_child_t *child;
    opal_list_item_t *item;
    opal_buffer_t buffer;
    int rc;
    orte_ns_cmp_bitmask_t mask;

    /* since we are going to be working with the global list of
     * children, we need to protect that list from modification
     * by other threads. This will also be used to protect us
     * from race conditions on any abort situation
     */
    OPAL_THREAD_LOCK(&orte_odls_globals.mutex);
    
    /* find this child */
    for (item = opal_list_get_first(&orte_local_children);
         item != opal_list_get_end(&orte_local_children);
         item = opal_list_get_next(item)) {
        child = (orte_odls_child_t*)item;

        mask = ORTE_NS_CMP_ALL;
        
        if (OPAL_EQUAL ==
                orte_util_compare_name_fields(mask, proc, child->name)) { /* found it */
            /* If it has already terminated (or been terminated), then skip */
            if( child->state > ORTE_PROC_STATE_UNTERMINATED ) {
                break;
            }

            child->state = ORTE_PROC_STATE_CALLED_ABORT;
            /* send ack */
            OBJ_CONSTRUCT(&buffer, opal_buffer_t);
            if (0 > (rc = orte_rml.send_buffer(proc, &buffer, ORTE_RML_TAG_ABORT, 0))) {
                ORTE_ERROR_LOG(rc);
            }
            OBJ_DESTRUCT(&buffer);
            break;
        }
    }
    opal_condition_signal(&orte_odls_globals.cond);
    OPAL_THREAD_UNLOCK(&orte_odls_globals.mutex);
}

void orte_base_default_waitpid_fired(orte_process_name_t *proc, int32_t status)
{
    orte_odls_child_t *child, *chd;
    orte_odls_job_t *jobdat, *jdat;
    opal_list_item_t *item;
    int rc;
    orte_ns_cmp_bitmask_t mask;

    OPAL_OUTPUT_VERBOSE((5, orte_odls_globals.output,
                         "%s odls:waitpid_fired on child %s with status %d",
                         ORTE_NAME_PRINT(ORTE_PROC_MY_NAME),
                         ORTE_NAME_PRINT(proc), WEXITSTATUS(status)));

    /* since we are going to be working with the global list of
     * children, we need to protect that list from modification
     * by other threads. This will also be used to protect us
     * from race conditions on any abort situation
     */
    OPAL_THREAD_LOCK(&orte_odls_globals.mutex);
    
    /* find this child */
    for (item = opal_list_get_first(&orte_local_children);
         item != opal_list_get_end(&orte_local_children);
         item = opal_list_get_next(item)) {
        child = (orte_odls_child_t*)item;
        
        mask = ORTE_NS_CMP_ALL;
        
        if (OPAL_EQUAL ==
                orte_util_compare_name_fields(mask, proc, child->name)) { /* found it */
            goto GOTCHILD;
        }
    }
    /* get here if we didn't find the child, or if the specified child
     * is already dead. If the latter, then we have a problem as it
     * means we are detecting it exiting multiple times
     */
    
    OPAL_OUTPUT_VERBOSE((5, orte_odls_globals.output,
                         "%s odls:waitpid_fired did not find child %s in table!",
                         ORTE_NAME_PRINT(ORTE_PROC_MY_NAME),
                         ORTE_NAME_PRINT(proc)));
    
    /* it's just a race condition - don't error log it */
    opal_condition_signal(&orte_odls_globals.cond);
    OPAL_THREAD_UNLOCK(&orte_odls_globals.mutex);
    return;
    
 GOTCHILD:
    /* if the child was previously flagged as dead, then just
     * ensure that its exit state gets reported to avoid hanging
     */
    if (!child->alive) {
        OPAL_OUTPUT_VERBOSE((5, orte_odls_globals.output,
                             "%s odls:waitpid_fired child %s was already dead",
                             ORTE_NAME_PRINT(ORTE_PROC_MY_NAME),
                             ORTE_NAME_PRINT(child->name)));
        goto MOVEON;
    }

    /* get the jobdat for this child */
    jobdat = NULL;
    for (item = opal_list_get_first(&orte_local_jobdata);
         item != opal_list_get_end(&orte_local_jobdata);
         item = opal_list_get_next(item)) {
        jdat = (orte_odls_job_t*)item;
        if (jdat->jobid == child->name->jobid) {
            jobdat = jdat;
            break;
        }
    }
    if (NULL == jobdat) {
        ORTE_ERROR_LOG(ORTE_ERR_NOT_FOUND);
        goto MOVEON;
    }

    /* if this is a debugger daemon, then just report the state
     * and return as we aren't monitoring it
     */
    if (ORTE_JOB_CONTROL_DEBUGGER_DAEMON & jobdat->controls)  {
        child->state = ORTE_PROC_STATE_TERMINATED;
        goto MOVEON;
    }

    /* if this child was ordered to die, then just pass that along
     * so we don't hang
     */
    if (ORTE_PROC_STATE_KILLED_BY_CMD == child->state) {
        OPAL_OUTPUT_VERBOSE((5, orte_odls_globals.output,
                             "%s odls:waitpid_fired child %s was ordered to die",
                             ORTE_NAME_PRINT(ORTE_PROC_MY_NAME),
                             ORTE_NAME_PRINT(child->name)));
        goto MOVEON;
    }
    
    /* determine the state of this process */
    if(WIFEXITED(status)) {
        /* set the exit status appropriately */
        child->exit_code = WEXITSTATUS(status);

        if (ORTE_PROC_STATE_CALLED_ABORT == child->state) {
            /* even though the process exited "normally", it happened
             * via an orte_abort call, so we need to indicate this was
             * an "abnormal" termination.
             */
            OPAL_OUTPUT_VERBOSE((5, orte_odls_globals.output,
                                 "%s odls:waitpid_fired child %s died by call to abort",
                                 ORTE_NAME_PRINT(ORTE_PROC_MY_NAME),
                                 ORTE_NAME_PRINT(child->name)));
            child->state = ORTE_PROC_STATE_ABORTED;
            goto MOVEON;
        }
        
        /* check to see if a sync was required and if it was received */
        if (child->init_recvd) {
            if (!child->fini_recvd) {
                /* we required a finalizing sync and didn't get it, so this
                 * is considered an abnormal termination and treated accordingly
                 */
                if (0 != child->exit_code) {
                    child->state = ORTE_PROC_STATE_TERM_NON_ZERO;
                    OPAL_OUTPUT_VERBOSE((5, orte_odls_globals.output,
                                         "%s odls:waitpid_fired child process %s terminated normally "
                                         "but with a non-zero exit status - it "
                                         "will be treated as an abnormal termination",
                                         ORTE_NAME_PRINT(ORTE_PROC_MY_NAME),
                                         ORTE_NAME_PRINT(child->name)));
                } else {
                    child->state = ORTE_PROC_STATE_TERM_WO_SYNC;
                    OPAL_OUTPUT_VERBOSE((5, orte_odls_globals.output,
                                         "%s odls:waitpid_fired child process %s terminated normally "
                                         "but did not provide a required finalize sync - it "
                                         "will be treated as an abnormal termination",
                                         ORTE_NAME_PRINT(ORTE_PROC_MY_NAME),
                                         ORTE_NAME_PRINT(child->name)));
                }
                
                goto MOVEON;
            }
            /* if we did recv a finalize sync, then declare it normally terminated
             * unless it returned with a non-zero status indicating the code
             * felt it was non-normal
             */
            if (0 != child->exit_code) {
                child->state = ORTE_PROC_STATE_TERM_NON_ZERO;
                OPAL_OUTPUT_VERBOSE((5, orte_odls_globals.output,
                                     "%s odls:waitpid_fired child process %s terminated normally "
                                     "but with a non-zero exit status - it "
                                     "will be treated as an abnormal termination",
                                     ORTE_NAME_PRINT(ORTE_PROC_MY_NAME),
                                     ORTE_NAME_PRINT(child->name)));
            } else {
                child->state = ORTE_PROC_STATE_TERMINATED;
            }
        } else {
            /* has any child in this job already registered? */
            for (item = opal_list_get_first(&orte_local_children);
                 item != opal_list_get_end(&orte_local_children);
                 item = opal_list_get_next(item)) {
                chd = (orte_odls_child_t*)item;
                
                if (chd->init_recvd) {
                    /* someone has registered, and we didn't before
                     * terminating - this is an abnormal termination
                     */
                    if (0 != child->exit_code) {
                        child->state = ORTE_PROC_STATE_TERM_NON_ZERO;
                        OPAL_OUTPUT_VERBOSE((5, orte_odls_globals.output,
                                             "%s odls:waitpid_fired child process %s terminated normally "
                                             "but with a non-zero exit status - it "
                                             "will be treated as an abnormal termination",
                                             ORTE_NAME_PRINT(ORTE_PROC_MY_NAME),
                                             ORTE_NAME_PRINT(child->name)));
                    } else {
                        child->state = ORTE_PROC_STATE_TERM_WO_SYNC;
                        OPAL_OUTPUT_VERBOSE((5, orte_odls_globals.output,
                                             "%s odls:waitpid_fired child process %s terminated normally "
                                             "but did not provide a required init sync - it "
                                             "will be treated as an abnormal termination",
                                             ORTE_NAME_PRINT(ORTE_PROC_MY_NAME),
                                             ORTE_NAME_PRINT(child->name)));
                    }
                    
                    goto MOVEON;
                }
            }
            /* if no child has registered, then it is possible that
             * none of them will. This is considered acceptable. Still
             * flag it as abnormal if the exit code was non-zero
             */
            if (0 != child->exit_code) {
                child->state = ORTE_PROC_STATE_TERM_NON_ZERO;
            } else {
                child->state = ORTE_PROC_STATE_TERMINATED;
            }
        }
        
        OPAL_OUTPUT_VERBOSE((5, orte_odls_globals.output,
                             "%s odls:waitpid_fired child process %s terminated %s",
                             ORTE_NAME_PRINT(ORTE_PROC_MY_NAME),
                             ORTE_NAME_PRINT(child->name),
                             (0 == child->exit_code) ? "normally" : "with non-zero status"));
    } else {
        /* the process was terminated with a signal! That's definitely
         * abnormal, so indicate that condition
         */
        child->state = ORTE_PROC_STATE_ABORTED_BY_SIG;
        /* If a process was killed by a signal, then make the
         * exit code of orterun be "signo + 128" so that "prog"
         * and "orterun prog" will both yield the same exit code.
         *
         * This is actually what the shell does for you when
         * a process dies by signal, so this makes orterun treat
         * the termination code to exit status translation the
         * same way
         */
        child->exit_code = WTERMSIG(status) + 128;

        OPAL_OUTPUT_VERBOSE((5, orte_odls_globals.output,
                             "%s odls:waitpid_fired child process %s terminated with signal",
                             ORTE_NAME_PRINT(ORTE_PROC_MY_NAME),
                             ORTE_NAME_PRINT(child->name) ));
        /* Do not decrement the number of local procs here. That is handled in the errmgr */
    }
    
 MOVEON:
    /* indicate the waitpid fired */
    child->waitpid_recvd = true;
    
    /* now check to see if the proc is truly done */
    if (child->iof_complete) {
        /* CHILD IS COMPLETE */
        child->alive = false;
        
        /* Release only the stdin IOF file descriptor for this child, if one
         * was defined. File descriptors for the other IOF channels - stdout,
         * stderr, and stddiag - were released when their associated pipes
         * were cleared and closed due to termination of the process
         */
        if (NULL != orte_iof.close) {
            orte_iof.close(proc, ORTE_IOF_STDIN);
        }
        
        /* Clean up the session directory as if we were the process
         * itself.  This covers the case where the process died abnormally
         * and didn't cleanup its own session directory.
         */
        orte_session_dir_finalize(proc);
        /* alert the errmgr */
        if (ORTE_SUCCESS != (rc = orte_errmgr.update_state(ORTE_JOBID_INVALID, ORTE_JOB_STATE_UNDEF,
                                                           proc, child->state, child->pid,
                                                           child->exit_code))) {
            ORTE_ERROR_LOG(rc);
        }
    }
    
    /* done */
    opal_condition_signal(&orte_odls_globals.cond);
    OPAL_THREAD_UNLOCK(&orte_odls_globals.mutex);
}

/*
 *  Wait for a callback indicating the child has completed.
 */

void odls_base_default_wait_local_proc(pid_t pid, int status, void* cbdata)
{
    orte_odls_child_t *child;
    opal_list_item_t *item, *next;
    int rc;
    opal_buffer_t cmdbuf;
    orte_daemon_cmd_flag_t command;
    int32_t istatus;
    
    OPAL_OUTPUT_VERBOSE((5, orte_odls_globals.output,
                         "%s odls:wait_local_proc child process %ld terminated",
                         ORTE_NAME_PRINT(ORTE_PROC_MY_NAME),
                         (long)pid));
    
    /* since we are going to be working with the global list of
     * children, we need to protect that list from modification
     * by other threads. This will also be used to protect us
     * from race conditions on any abort situation
     */
    OPAL_THREAD_LOCK(&orte_odls_globals.mutex);
    
    /* find this child */
    for (item = opal_list_get_first(&orte_local_children);
         item != opal_list_get_end(&orte_local_children);
         item = next) {
        child = (orte_odls_child_t*)item;
        next = opal_list_get_next(item);
        
        if (pid == child->pid) { /* found it */
            /* this is an independent entry point from the event library. To avoid
             * race conditions, we need to get back into the progression of messages
             * and commands to be processed by the daemon. We do this by re-posting
             * the event into the daemon cmd processor
             */
            OBJ_CONSTRUCT(&cmdbuf, opal_buffer_t);
            command = ORTE_DAEMON_WAITPID_FIRED;
            if (ORTE_SUCCESS != (rc = opal_dss.pack(&cmdbuf, &command, 1, ORTE_DAEMON_CMD))) {
                ORTE_ERROR_LOG(rc);
                goto CLEANUP;
            }
            if (ORTE_SUCCESS != (rc = opal_dss.pack(&cmdbuf, child->name, 1, ORTE_NAME))) {
                ORTE_ERROR_LOG(rc);
                goto CLEANUP;
            }
            istatus = status;
            if (ORTE_SUCCESS != (rc = opal_dss.pack(&cmdbuf, &istatus, 1, OPAL_INT32))) {
                ORTE_ERROR_LOG(rc);
                goto CLEANUP;
            }
            ORTE_MESSAGE_EVENT(ORTE_PROC_MY_NAME, &cmdbuf, ORTE_RML_TAG_DAEMON, orte_daemon_cmd_processor);
            /* done */
            opal_condition_signal(&orte_odls_globals.cond);
            OPAL_THREAD_UNLOCK(&orte_odls_globals.mutex);
            return;
        }
    }
    /* get here if we didn't find the child, or if the specified child
     * is already dead. If the latter, then we have a problem as it
     * means we are detecting it exiting multiple times
     */
    
    OPAL_OUTPUT_VERBOSE((5, orte_odls_globals.output,
                         "%s odls:wait_local_proc did not find pid %ld in table!",
                         ORTE_NAME_PRINT(ORTE_PROC_MY_NAME),
                         (long)pid));
    
    /* it's just a race condition - don't error log it */
CLEANUP:
    opal_condition_signal(&orte_odls_globals.cond);
    OPAL_THREAD_UNLOCK(&orte_odls_globals.mutex);
    return;
}

int orte_odls_base_default_kill_local_procs(opal_pointer_array_t *procs,
                                            orte_odls_base_kill_local_fn_t kill_local,
                                            orte_odls_base_child_died_fn_t child_died)
{
    orte_odls_child_t *child;
    opal_list_item_t *item = NULL, *next = NULL;
    int rc = ORTE_SUCCESS;
    opal_list_t procs_killed;
    orte_proc_t *proc, proctmp;
    int i;
    opal_pointer_array_t procarray, *procptr;
    bool do_cleanup;
    
    OBJ_CONSTRUCT(&procs_killed, opal_list_t);

    /* since we are going to be working with the global list of
     * children, we need to protect that list from modification
     * by other threads
     */
    OPAL_THREAD_LOCK(&orte_odls_globals.mutex);
    
    /* if the pointer array is NULL, then just kill everything */
    if (NULL == procs) {
        OPAL_OUTPUT_VERBOSE((5, orte_odls_globals.output,
                             "%s odls:kill_local_proc working on WILDCARD",
                             ORTE_NAME_PRINT(ORTE_PROC_MY_NAME)));
        OBJ_CONSTRUCT(&procarray, opal_pointer_array_t);
        opal_pointer_array_init(&procarray, 1, 1, 1);
        OBJ_CONSTRUCT(&proctmp, orte_proc_t);
        proctmp.name.jobid = ORTE_JOBID_WILDCARD;
        proctmp.name.vpid = ORTE_VPID_WILDCARD;
        ORTE_EPOCH_SET(proctmp.name.epoch,ORTE_EPOCH_WILDCARD);
        opal_pointer_array_add(&procarray, &proctmp);
        procptr = &procarray;
        do_cleanup = true;
    } else {
        OPAL_OUTPUT_VERBOSE((5, orte_odls_globals.output,
                             "%s odls:kill_local_proc working on provided array",
                             ORTE_NAME_PRINT(ORTE_PROC_MY_NAME)));
        procptr = procs;
        do_cleanup = false;
    }
    
    /* cycle through the provided array of processes to kill */
    for (i=0; i < procptr->size; i++) {
        if (NULL == (proc = (orte_proc_t*)opal_pointer_array_get_item(procptr, i))) {
            continue;
        }
        for (item = opal_list_get_first(&orte_local_children);
             item != opal_list_get_end(&orte_local_children);
             item = next ) {
            child = (orte_odls_child_t*)item;
            /*
             * The errmgr might try to mutate the list of local children
             * So protect ourselves by picking the next item here
             */
            next = opal_list_get_next(item);
            
            OPAL_OUTPUT_VERBOSE((5, orte_odls_globals.output,
                                 "%s odls:kill_local_proc checking child process %s",
                                 ORTE_NAME_PRINT(ORTE_PROC_MY_NAME),
                                 ORTE_NAME_PRINT(child->name)));
            
            /* do we have a child from the specified job? Because the
             *  job could be given as a WILDCARD value, we must
             *  check for that as well as for equality.
             */
            if (ORTE_JOBID_WILDCARD != proc->name.jobid &&
                proc->name.jobid != child->name->jobid) {
                
                OPAL_OUTPUT_VERBOSE((5, orte_odls_globals.output,
                                     "%s odls:kill_local_proc child %s is not part of job %s",
                                     ORTE_NAME_PRINT(ORTE_PROC_MY_NAME),
                                     ORTE_NAME_PRINT(child->name),
                                     ORTE_JOBID_PRINT(proc->name.jobid)));
                
                continue;
            }
            
            /* see if this is the specified proc - could be a WILDCARD again, so check
             * appropriately
             */
            if (ORTE_VPID_WILDCARD != proc->name.vpid &&
                proc->name.vpid != child->name->vpid) {
                
                OPAL_OUTPUT_VERBOSE((5, orte_odls_globals.output,
                                     "%s odls:kill_local_proc child %s is not covered by rank %s",
                                     ORTE_NAME_PRINT(ORTE_PROC_MY_NAME),
                                     ORTE_NAME_PRINT(child->name),
                                     ORTE_VPID_PRINT(proc->name.vpid)));
                
                continue;
            }
            
            /* is this process alive? if not, then nothing for us
             * to do to it
             */
            if (!child->alive || 0 == child->pid) {
                
                OPAL_OUTPUT_VERBOSE((5, orte_odls_globals.output,
                                     "%s odls:kill_local_proc child %s is not alive",
                                     ORTE_NAME_PRINT(ORTE_PROC_MY_NAME),
                                     ORTE_NAME_PRINT(child->name)));
                
                /* ensure, though, that the state is terminated so we don't lockup if
                 * the proc never started
                 */
                if (ORTE_PROC_STATE_UNDEF == child->state ||
                    ORTE_PROC_STATE_INIT == child->state ||
                    ORTE_PROC_STATE_LAUNCHED == child->state ||
                    ORTE_PROC_STATE_RUNNING == child->state) {
                    /* we can't be sure what happened, but make sure we
                     * at least have a value that will let us eventually wakeup
                     */
                    child->state = ORTE_PROC_STATE_TERMINATED;
                    /* ensure we realize that the waitpid will never come, if
                     * it already hasn't
                     */
                    child->waitpid_recvd = true;
                    child->pid = 0;
                    goto CLEANUP;
                } else {
                    continue;
                }
            }

            /* mark the child as "killed" since the waitpid will
             * fire as soon as we kill it
             */
            child->state = ORTE_PROC_STATE_KILLED_BY_CMD;  /* we ordered it to die */

            /* ensure the stdin IOF channel for this child is closed. The other
             * channels will automatically close when the proc is killed
             */
            if (NULL != orte_iof.close) {
                orte_iof.close(child->name, ORTE_IOF_STDIN);
            }
            
            /* cancel the waitpid callback as this induces unmanageable race
             * conditions when we are deliberately killing the process
             */
            orte_wait_cb_cancel(child->pid);
            
            /* First send a SIGCONT in case the process is in stopped state.
               If it is in a stopped state and we do not first change it to
               running, then SIGTERM will not get delivered.  Ignore return
               value. */
            OPAL_OUTPUT_VERBOSE((5, orte_odls_globals.output,
                                 "%s SENDING SIGCONT TO %s",
                                 ORTE_NAME_PRINT(ORTE_PROC_MY_NAME),
                                 ORTE_NAME_PRINT(child->name)));
            kill_local(child->pid, SIGCONT);

            /* Send a sigterm to the process before sigkill to be nice */
            OPAL_OUTPUT_VERBOSE((5, orte_odls_globals.output,
                                 "%s SENDING SIGTERM TO %s",
                                 ORTE_NAME_PRINT(ORTE_PROC_MY_NAME),
                                 ORTE_NAME_PRINT(child->name)));
            kill_local(child->pid, SIGTERM);

            /* check to see if it died - the child_died function will continue
             * to check until we reach the timeout
	     *
	     * In practice, it doesn't matter what child_died reports
	     * - we KILL the process anyway, to be sure it's dead.
	     * However, what it does do is delay the KILL until either
	     * the process is verified dead or the timeout elapsed,
	     * which gives it time enough to shut down.
             */
            if (!child_died(child)) {
                /* if it still isn't dead, try killing it one more time */
                OPAL_OUTPUT_VERBOSE((5, orte_odls_globals.output,
                                     "%s SENDING SIGKILL TO %s",
                                     ORTE_NAME_PRINT(ORTE_PROC_MY_NAME),
                                     ORTE_NAME_PRINT(child->name)));
                kill_local(child->pid, SIGKILL);
                /* Double check that it actually died this time */
                if (!child_died(child)) {
                    orte_show_help("help-odls-default.txt",
                                   "odls-default:could-not-kill",
                                   true, orte_process_info.nodename, child->pid);
                }
            } else {
                /* Force the SIGKILL just to make sure things are dead
                 * This fixes an issue that, if the application is masking
                 * SIGTERM, then the child_died()
                 * may return 'true' even though waipid returns with 0.
                 * It does this to avoid a race condition, per documentation
                 * in odls_default_module.c.
                 */
                OPAL_OUTPUT_VERBOSE((5, orte_odls_globals.output,
                                     "%s SENDING FORCE SIGKILL TO %s",
                                     ORTE_NAME_PRINT(ORTE_PROC_MY_NAME),
                                     ORTE_NAME_PRINT(child->name)));
                kill_local(child->pid, SIGKILL);
                /* Double check that it actually died this time */
                if (!child_died(child)) {
                    orte_show_help("help-odls-default.txt",
                                   "odls-default:could-not-kill",
                                   true, orte_process_info.nodename, child->pid);
                }
            }

            OPAL_OUTPUT_VERBOSE((5, orte_odls_globals.output,
                                 "%s odls:kill_local_proc child %s killed",
                                 ORTE_NAME_PRINT(ORTE_PROC_MY_NAME),
                                 ORTE_NAME_PRINT(child->name)));
            
            /* indicate the waitpid fired as this is effectively what
             * has happened
             */
            child->waitpid_recvd = true;
            /* ensure the process is flagged as "not alive" */
            child->alive = false;
            child->pid = 0;
            
        CLEANUP:
            /* ensure the child's session directory is cleaned up */
            orte_session_dir_finalize(child->name);
            /* check for everything complete - this will remove
             * the child object from our local list
             */
            if (child->iof_complete && child->waitpid_recvd) {
                rc = orte_errmgr.update_state(ORTE_JOBID_INVALID, ORTE_JOB_STATE_UNDEF,
                                              child->name, child->state, child->pid,
                                              child->exit_code);
                if (ORTE_ERR_SILENT == rc) {
                    /* all procs are complete - we are done */
                    break;
                }
            }
        }
    }
    
    /* cleanup, if required */
    if (do_cleanup) {
        OBJ_DESTRUCT(&procarray);
        OBJ_DESTRUCT(&proctmp);
    }
    
    /* we are done with the global list, so we can now release
     * any waiting threads - this also allows any callbacks to work
     */
    opal_condition_signal(&orte_odls_globals.cond);
    OPAL_THREAD_UNLOCK(&orte_odls_globals.mutex);
    
    return ORTE_SUCCESS;
}

int orte_odls_base_get_proc_stats(opal_buffer_t *answer,
                                  orte_process_name_t *proc)
{
    int rc;
    orte_odls_child_t *child;
    opal_list_item_t *item, *next;
    opal_pstats_t stats, *statsptr;
    int j;
    
    OPAL_OUTPUT_VERBOSE((5, orte_odls_globals.output,
                         "%s odls:get_proc_stats for proc %s",
                         ORTE_NAME_PRINT(ORTE_PROC_MY_NAME),
                         ORTE_NAME_PRINT(proc)));
    
    /* find this child */
    for (item = opal_list_get_first(&orte_local_children);
         item != opal_list_get_end(&orte_local_children);
         item = next) {
        child = (orte_odls_child_t*)item;
        next = opal_list_get_next(item);
        
        if (proc->jobid == child->name->jobid &&
            (proc->vpid == child->name->vpid ||
             ORTE_VPID_WILDCARD == proc->vpid)) { /* found it */

            OBJ_CONSTRUCT(&stats, opal_pstats_t);
            /* record node up to first '.' */
            for (j=0; j < (int)strlen(orte_process_info.nodename) &&
                 j < OPAL_PSTAT_MAX_STRING_LEN-1 &&
                 orte_process_info.nodename[j] != '.'; j++) {
                stats.node[j] = orte_process_info.nodename[j];
            }
            /* record rank */
            stats.rank = child->name->vpid;
            /* get stats */
            rc = opal_pstat.query(child->pid, &stats, NULL);
            if (ORTE_SUCCESS != rc) {
                OBJ_DESTRUCT(&stats);
                return rc;
            }
            if (ORTE_SUCCESS != (rc = opal_dss.pack(answer, proc, 1, ORTE_NAME))) {
                ORTE_ERROR_LOG(rc);
                OBJ_DESTRUCT(&stats);
                return rc;
            }
            statsptr = &stats;
            if (ORTE_SUCCESS != (rc = opal_dss.pack(answer, &statsptr, 1, OPAL_PSTAT))) {
                ORTE_ERROR_LOG(rc);
                OBJ_DESTRUCT(&stats);
                return rc;
            }
            OBJ_DESTRUCT(&stats);
        }
    }

    return ORTE_SUCCESS;
}

int orte_odls_base_default_restart_proc(orte_odls_child_t *child,
                                        orte_odls_base_fork_local_proc_fn_t fork_local)
{
    int rc;
    orte_app_context_t *app;
    opal_list_item_t *item;
    orte_odls_job_t *jobdat;
    char basedir[MAXPATHLEN];

    /* protect operations involving the global list of children */
    OPAL_THREAD_LOCK(&orte_odls_globals.mutex);

    OPAL_OUTPUT_VERBOSE((5, orte_odls_globals.output,
                         "%s odls:restart_proc for proc %s",
                         ORTE_NAME_PRINT(ORTE_PROC_MY_NAME),
                         ORTE_NAME_PRINT(child->name)));
    
    /* establish our baseline working directory - we will be potentially
     * bouncing around as we execute this app, but we will always return
     * to this place as our default directory
     */
    getcwd(basedir, sizeof(basedir));

    /* find this child's jobdat */
    jobdat = NULL;
    for (item = opal_list_get_first(&orte_local_jobdata);
         item != opal_list_get_end(&orte_local_jobdata);
         item = opal_list_get_next(item)) {
        jobdat = (orte_odls_job_t*)item;
        if (jobdat->jobid == child->name->jobid) {
            break;
        }
    }
    if (NULL == jobdat) {
        /* not found */
        ORTE_ERROR_LOG(ORTE_ERR_NOT_FOUND);
        return ORTE_ERR_NOT_FOUND;
    }
    
    child->state = ORTE_PROC_STATE_FAILED_TO_START;
    child->exit_code = 0;
    child->waitpid_recvd = false;
    child->iof_complete = false;
    child->coll_recvd = false;
    child->pid = 0;
    child->init_recvd = false;
    child->fini_recvd = false;
    if (NULL != child->rml_uri) {
        free(child->rml_uri);
        child->rml_uri = NULL;
    }
    app = (orte_app_context_t*)opal_pointer_array_get_item(&jobdat->apps, child->app_idx);

    /* reset envars to match this child */    
    if (ORTE_SUCCESS != (rc = setup_child(child, jobdat, &app->env))) {
        ORTE_ERROR_LOG(rc);
        opal_condition_signal(&orte_odls_globals.cond);
        OPAL_THREAD_UNLOCK(&orte_odls_globals.mutex);
        goto CLEANUP;
    }

    /* setup the path */
    if (ORTE_SUCCESS != (rc = setup_path(app))) {
        ORTE_ERROR_LOG(rc);
        opal_condition_signal(&orte_odls_globals.cond);
        OPAL_THREAD_UNLOCK(&orte_odls_globals.mutex);
        goto CLEANUP;
    }

    OPAL_OUTPUT_VERBOSE((5, orte_odls_globals.output,
                         "%s restarting app %s",
                         ORTE_NAME_PRINT(ORTE_PROC_MY_NAME), app->app));

    /* must unlock prior to fork to keep things clean in the
     * event library
     */
    opal_condition_signal(&orte_odls_globals.cond);
    OPAL_THREAD_UNLOCK(&orte_odls_globals.mutex);

    rc = fork_local(app, child, app->env, jobdat);
    if (ORTE_SUCCESS == rc) {
        orte_wait_cb(child->pid, odls_base_default_wait_local_proc, NULL);
    }
    
 CLEANUP:
    OPAL_OUTPUT_VERBOSE((5, orte_odls_globals.output,
                         "%s odls:restart of proc %s %s",
                         ORTE_NAME_PRINT(ORTE_PROC_MY_NAME),
                         ORTE_NAME_PRINT(child->name),
                         (ORTE_SUCCESS == rc) ? "succeeded" : "failed"));

    /* reset our working directory back to our default location - if we
     * don't do this, then we will be looking for relative paths starting
     * from the last wdir option specified by the user. Thus, we would
     * be requiring that the user keep track on the cmd line of where
     * each app was located relative to the prior app, instead of relative
     * to their current location
     */
    chdir(basedir);

    return rc;
}

bool orte_odls_base_default_check_finished(orte_process_name_t *proc) {
    orte_odls_child_t *child;
    opal_list_item_t *item;
    orte_ns_cmp_bitmask_t mask;

    OPAL_THREAD_LOCK(&orte_odls_globals.mutex);
    
    /* find this child */
    for (item = opal_list_get_first(&orte_local_children);
         item != opal_list_get_end(&orte_local_children);
         item = opal_list_get_next(item)) {
        child = (orte_odls_child_t*)item;
        
        mask = ORTE_NS_CMP_ALL;
        
        if (OPAL_EQUAL == orte_util_compare_name_fields(mask, proc, child->name)) { /* found it */
            return child->fini_recvd;
        }
    }

    return false;
}
