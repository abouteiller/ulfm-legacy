/*
 * Copyright (c) 2004-2008 The Trustees of Indiana University and Indiana
 *                         University Research and Technology
 *                         Corporation.  All rights reserved.
 * Copyright (c) 2004-2011 The University of Tennessee and The University
 *                         of Tennessee Research Foundation.  All rights
 *                         reserved.
 * Copyright (c) 2004-2005 High Performance Computing Center Stuttgart, 
 *                         University of Stuttgart.  All rights reserved.
 * Copyright (c) 2004-2005 The Regents of the University of California.
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

#include "opal/class/opal_list.h"
#include "opal/mca/mca.h"
#include "opal/mca/base/base.h"
#include "opal/mca/base/mca_base_component_repository.h"
#if !ORTE_DISABLE_FULL_SUPPORT
#include "orte/util/nidmap.h"
#endif
#include "orte/runtime/orte_globals.h"

#include "orte/mca/ess/base/base.h"

extern opal_list_t orte_ess_base_components_available;

/**
 * Generic function to retrieve the epoch of a specific process 
 * from the job data.
 */
#if !ORTE_ENABLE_EPOCH
int orte_ess_base_proc_get_epoch(orte_process_name_t *proc) {
    return 0;
}
#else
orte_epoch_t orte_ess_base_proc_get_epoch(orte_process_name_t *proc) {
    orte_epoch_t epoch = ORTE_EPOCH_INVALID;

    epoch = orte_util_lookup_epoch(proc);

    return epoch;
}
#endif

int 
orte_ess_base_select(void)
{
    orte_ess_base_component_t *best_component = NULL;
    orte_ess_base_module_t *best_module = NULL;

    /*
     * Select the best component
     */
    if( OPAL_SUCCESS != mca_base_select("ess", orte_ess_base_output,
                                        &orte_ess_base_components_available,
                                        (mca_base_module_t **) &best_module,
                                        (mca_base_component_t **) &best_component) ) {
        /* error message emitted by fn above */
        return ORTE_ERR_SILENT;
    }

    /* Save the winner */
    /* No global component structure */
    orte_ess = *best_module;

    return ORTE_SUCCESS;
}
