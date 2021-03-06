/* -*- Mode: C; c-basic-offset:4 ; -*- */
/*
 * Copyright (c) 2004-2010 The Trustees of Indiana University and Indiana
 *                         University Research and Technology
 *                         Corporation.  All rights reserved.
 * Copyright (c) 2004-2008 The University of Tennessee and The University
 *                         of Tennessee Research Foundation.  All rights
 *                         reserved.
 * Copyright (c) 2004-2005 High Performance Computing Center Stuttgart,
 *                         University of Stuttgart.  All rights reserved.
 * Copyright (c) 2004-2005 The Regents of the University of California.
 *                         All rights reserved.
 * Copyright (c) 2007-2010 Cisco Systems, Inc.  All rights reserved.
 * Copyright (c) 2006-2009 Mellanox Technologies. All rights reserved.
 * Copyright (c) 2006-2007 Los Alamos National Security, LLC.  All rights
 *                         reserved.
 * Copyright (c) 2006-2007 Voltaire All rights reserved.
 * Copyright (c) 2008-2010 Oracle and/or its affiliates.  All rights reserved.
 * Copyright (c) 2009      IBM Corporation.  All rights reserved.
 * $COPYRIGHT$
 *
 * Additional copyrights may follow
 *
 * $HEADER$
 */

#include "ompi_config.h"
#include <string.h>

#include "orte/util/show_help.h"
#include "orte/runtime/orte_globals.h"
#include "opal/class/opal_bitmap.h"
#include "opal/util/output.h"
#include "opal/util/arch.h"
#include "opal/util/opal_sos.h"

#include "ompi/mca/btl/btl.h"
#include "ompi/mca/btl/base/btl_base_error.h"

#include "btl_wv_ini.h"

#include "btl_wv.h"
#include "btl_wv_frag.h"
#include "btl_wv_proc.h"
#include "btl_wv_endpoint.h"
#include "opal/datatype/opal_convertor.h"
#include "ompi/mca/mpool/base/base.h"
#include "ompi/mca/mpool/mpool.h"
#include "ompi/mca/mpool/rdma/mpool_rdma.h"
#include "orte/util/proc_info.h"
#include <errno.h>
#include <string.h>
#include <math.h>
#include <rdma/winverbs.h>

#ifdef HAVE_SYS_TYPES_H
#include <sys/types.h>
#endif
#ifdef HAVE_SYS_TIME_H
#include <sys/time.h>
#endif
#ifdef HAVE_SYS_RESOURCE_H
#include <sys/resource.h>
#endif
#ifdef HAVE_UNISTD_H
#include <unistd.h>
#endif
#ifndef MIN
#define MIN(a,b) ((a)<(b)?(a):(b))
#endif 

mca_btl_wv_module_t mca_btl_wv_module = {
    {
        &mca_btl_wv_component.super,
        0, /* max size of first fragment */
        0, /* min send fragment size */
        0, /* max send fragment size */
        0, /* btl_rdma_pipeline_send_length */
        0, /* btl_rdma_pipeline_frag_size */
        0, /* btl_min_rdma_pipeline_size */
        0, /* exclusivity */
        0, /* latency */
        0, /* bandwidth */
        0, /* TODO this should be PUT btl flags */
        mca_btl_wv_add_procs,
        mca_btl_wv_del_procs,
        NULL,
        mca_btl_wv_finalize,
        /* we need alloc free, pack */
        mca_btl_wv_alloc,
        mca_btl_wv_free,
        mca_btl_wv_prepare_src,
        mca_btl_wv_prepare_dst,
        mca_btl_wv_send,
        mca_btl_wv_sendi, /* send immediate */
        mca_btl_wv_put,
        mca_btl_wv_get,
        mca_btl_base_dump,
        NULL, /* mpool */
        mca_btl_wv_register_error_cb, /* error call back registration */
        mca_btl_wv_ft_event
    }
};

char* const mca_btl_wv_transport_name_strings[MCA_BTL_WV_TRANSPORT_SIZE] = {
    "MCA_BTL_WV_TRANSPORT_IB",
    "MCA_BTL_WV_TRANSPORT_IWARP",
    "MCA_BTL_WV_TRANSPORT_RDMAOE",
    "MCA_BTL_WV_TRANSPORT_UNKNOWN"
};

static int mca_btl_wv_finalize_resources(struct mca_btl_base_module_t* btl);

void mca_btl_wv_show_init_error(const char *file, int line,
                                const char *func, const char *dev)
{
    if (ENOMEM == errno) {char *str_limit = NULL;

        orte_show_help("help-mpi-btl-wv.txt", "init-fail-no-mem",
                       true, orte_process_info.nodename,
                       file, line, func, dev, str_limit);

        if (NULL != str_limit) free(str_limit);
    } else {
        orte_show_help("help-mpi-btl-wv.txt", "init-fail-create-q",
                       true, orte_process_info.nodename,
                       file, line, func, strerror(errno), errno, dev);
    }
}

static inline struct wv_cq *create_cq_compat(struct wv_context *context,
        int cqe, void *cq_context, struct wv_comp_channel *channel,
        int comp_vector)
{
    struct wv_cq *cq;
    HRESULT hr;
    SIZE_T entries;
    cq = (struct wv_cq*)malloc(sizeof(struct wv_cq));
    cq->context = context;
    cq->channel = channel;
    cq->cq_context = cq_context;
    cq->notify_cnt = 0;
    cq->ack_cnt = 0;
    entries = cqe;
    hr = context->device_if->CreateCompletionQueue(&entries, &cq->handle);
    if (FAILED(hr)) {
        goto err;
    }
    memset(&cq->comp_entry, 0, sizeof cq->comp_entry);
    cq->cqe = (uint32_t) entries;
    return cq;
err:
    delete cq;
    return NULL;
}

static int adjust_cq(mca_btl_wv_device_t *device, const int cq)
{
    uint32_t cq_size = device->cq_size[cq];

    /* make sure we don't exceed the maximum CQ size and that we
     * don't size the queue smaller than otherwise requested
     */
     if(cq_size < mca_btl_wv_component.ib_cq_size[cq])
        cq_size = mca_btl_wv_component.ib_cq_size[cq];

    if(cq_size > (uint32_t)device->ib_dev_attr.MaxCqEntries)
        cq_size = (uint32_t)device->ib_dev_attr.MaxCqEntries;

    if(NULL == device->ib_cq[cq]) {
        device->ib_cq[cq] = create_cq_compat(device->ib_dev_context, cq_size,
                NULL, NULL, 0);
        if (NULL == device->ib_cq[cq]) {
            mca_btl_wv_show_init_error(__FILE__, __LINE__, "create_cq",
                                           device->ib_dev->name);
            return OMPI_ERROR;
        }
    }
    return OMPI_SUCCESS;
}

/*
 * create both the high and low priority completion queues
 * and the shared receive queue (if requested)
 */
static int create_srq(mca_btl_wv_module_t *wv_btl)
{
    int qp, rc = 0;
    int32_t rd_num, rd_curr_num;

    bool device_support_modify_srq = true;

    /* create the SRQ's */
    for(qp = 0; qp < mca_btl_wv_component.num_qps; qp++) {
        struct wv_srq_init_attr attr;
        memset(&attr, 0, sizeof(struct wv_srq_init_attr));

        if(!BTL_WV_QP_TYPE_PP(qp)) {
            attr.attr.max_wr = mca_btl_wv_component.qp_infos[qp].rd_num +
                mca_btl_wv_component.qp_infos[qp].u.srq_qp.sd_max;
            attr.attr.max_sge = 1;
            wv_btl->qps[qp].u.srq_qp.rd_posted = 0;
            wv_btl->qps[qp].u.srq_qp.srq = (struct wv_srq*) malloc(sizeof(wv_srq));
            wv_btl->qps[qp].u.srq_qp.srq->context = wv_btl->device->ib_pd->context;
            wv_btl->qps[qp].u.srq_qp.srq->srq_context = attr.srq_context;
            wv_btl->qps[qp].u.srq_qp.srq->pd = wv_btl->device->ib_pd;
            wv_btl->device->ib_pd->handle->CreateSharedReceiveQueue(attr.attr.max_wr,
                                               attr.attr.max_sge, attr.attr.srq_limit,
                                               &wv_btl->qps[qp].u.srq_qp.srq->handle);
            if (NULL == wv_btl->qps[qp].u.srq_qp.srq) {
                mca_btl_wv_show_init_error(__FILE__, __LINE__,
                                           "create_srq",
                                           wv_btl->device->ib_dev->name);
                return OMPI_ERROR;
            }

            rd_num = mca_btl_wv_component.qp_infos[qp].rd_num;
            rd_curr_num = wv_btl->qps[qp].u.srq_qp.rd_curr_num = mca_btl_wv_component.qp_infos[qp].u.srq_qp.rd_init;

            if(true == mca_btl_wv_component.enable_srq_resize &&
                                    true == device_support_modify_srq) {
                if(0 == rd_curr_num) {
                    wv_btl->qps[qp].u.srq_qp.rd_curr_num = 1;
                }
                wv_btl->qps[qp].u.srq_qp.rd_low_local = rd_curr_num - (rd_curr_num >> 2);
                wv_btl->qps[qp].u.srq_qp.srq_limit_event_flag = true;
            } else {
                wv_btl->qps[qp].u.srq_qp.rd_curr_num = rd_num;
                wv_btl->qps[qp].u.srq_qp.rd_low_local = mca_btl_wv_component.qp_infos[qp].rd_low;
                mca_btl_wv_component.qp_infos[qp].u.srq_qp.srq_limit = 0;
                wv_btl->qps[qp].u.srq_qp.srq_limit_event_flag = false;
            }
        }
    }
    return OMPI_SUCCESS;
}

static int mca_btl_wv_size_queues(struct mca_btl_wv_module_t* wv_btl, size_t nprocs)
{
    uint32_t send_cqes, recv_cqes;
    int rc = OMPI_SUCCESS, qp;
    mca_btl_wv_device_t *device = wv_btl->device;

    /* figure out reasonable sizes for completion queues */
    for(qp = 0; qp < mca_btl_wv_component.num_qps; qp++) {
        if(BTL_WV_QP_TYPE_SRQ(qp)) {
            send_cqes = mca_btl_wv_component.qp_infos[qp].u.srq_qp.sd_max;
            recv_cqes = mca_btl_wv_component.qp_infos[qp].rd_num;
        } else {
            send_cqes = (mca_btl_wv_component.qp_infos[qp].rd_num +
                mca_btl_wv_component.qp_infos[qp].u.pp_qp.rd_rsv) * nprocs;
            recv_cqes = send_cqes;
        }
        wv_btl->device->cq_size[qp_cq_prio(qp)] += recv_cqes;
        wv_btl->device->cq_size[BTL_WV_LP_CQ] += send_cqes;
    }

    rc = adjust_cq(device, BTL_WV_HP_CQ);
    if (OMPI_SUCCESS != rc) {
        goto out;
    }

    rc = adjust_cq(device, BTL_WV_LP_CQ);
    if (OMPI_SUCCESS != rc) {
        goto out;
    }

    if (0 == wv_btl->num_peers && mca_btl_wv_component.num_srq_qps > 0 ) {
        rc = create_srq(wv_btl);
    }

    wv_btl->num_peers += nprocs;
out:
    return rc;
}

mca_btl_wv_transport_type_t mca_btl_wv_get_transport_type(mca_btl_wv_module_t* wv_btl)
{
    return MCA_BTL_WV_TRANSPORT_IB;
}

static int mca_btl_wv_tune_endpoint(mca_btl_wv_module_t* wv_btl, 
                                    mca_btl_base_endpoint_t* endpoint)
{
    int ret = OMPI_SUCCESS;

    char* recv_qps = NULL;

    ompi_btl_wv_ini_values_t values;

    if(mca_btl_wv_get_transport_type(wv_btl) != endpoint->rem_info.rem_transport_type) {
        orte_show_help("help-mpi-btl-wv.txt",
                "conflicting transport types", true,
                orte_process_info.nodename,
                        wv_btl->device->ib_dev->name,
                        (wv_btl->device->ib_dev_attr).VendorId,
                        (wv_btl->device->ib_dev_attr).VendorPartId,
                        mca_btl_wv_transport_name_strings[mca_btl_wv_get_transport_type(wv_btl)],
                        endpoint->endpoint_proc->proc_ompi->proc_hostname,
                        endpoint->rem_info.rem_vendor_id,
                        endpoint->rem_info.rem_vendor_part_id,
                        mca_btl_wv_transport_name_strings[endpoint->rem_info.rem_transport_type]);
    
        return OMPI_ERROR;
    }

    memset(&values, 0, sizeof(ompi_btl_wv_ini_values_t));
    ret = ompi_btl_wv_ini_query(endpoint->rem_info.rem_vendor_id,
                          endpoint->rem_info.rem_vendor_part_id, &values);

    if (OMPI_SUCCESS != ret &&
        OMPI_ERR_NOT_FOUND != OPAL_SOS_GET_ERROR_CODE(ret)) {
        orte_show_help("help-mpi-btl-wv.txt",
                       "error in device init", true,
                       orte_process_info.nodename,
                       wv_btl->device->ib_dev->name);
        return ret;
    }

    if(wv_btl->device->mtu < endpoint->rem_info.rem_mtu) {
        endpoint->rem_info.rem_mtu = wv_btl->device->mtu; 
    }

    endpoint->use_eager_rdma = wv_btl->device->use_eager_rdma &
                               endpoint->use_eager_rdma;

    /* Receive queues checking */

    /* In this check we assume that the command line or INI file parameters are the same
       for all processes on all machines. The assumption is correct for 99.9999% of users,
       if a user distributes different INI files or parameters for different node/procs,
       it is on his own responsibility */
    switch(mca_btl_wv_component.receive_queues_source) {
        case BTL_WV_RQ_SOURCE_MCA:
        case BTL_WV_RQ_SOURCE_MAX:
            break;

        /* If the queues configuration was set from command line 
           (with --mca btl_wv_receive_queues parameter) => both sides have a same configuration */

        /* In this case the local queues configuration was gotten from INI file =>
           not possible that remote side got its queues configuration from command line => 
           (by prio) the configuration was set from INI file or (if not configure)
           by default queues configuration */
        case BTL_WV_RQ_SOURCE_DEVICE_INI:
            if(NULL != values.receive_queues) {
                recv_qps = values.receive_queues;
            } else {
                recv_qps = mca_btl_wv_component.default_recv_qps;
            }

            if(0 != strcmp(mca_btl_wv_component.receive_queues,
                                                         recv_qps)) {
                orte_show_help("help-mpi-btl-wv.txt",
                               "unsupported queues configuration", true,
                               orte_process_info.nodename,
                               wv_btl->device->ib_dev->name,
                               (wv_btl->device->ib_dev_attr).VendorId,
                               (wv_btl->device->ib_dev_attr).VendorPartId,
                               mca_btl_wv_component.receive_queues,
                               endpoint->endpoint_proc->proc_ompi->proc_hostname,
                               endpoint->rem_info.rem_vendor_id,
                               endpoint->rem_info.rem_vendor_part_id,
                               recv_qps);

                return OMPI_ERROR;
            }
            break;

        /* If the local queues configuration was set 
           by default queues => check all possible cases for remote side and compare */
        case  BTL_WV_RQ_SOURCE_DEFAULT:
            if(NULL != values.receive_queues) {
                if(0 != strcmp(mca_btl_wv_component.receive_queues,
                                                values.receive_queues)) {
                     orte_show_help("help-mpi-btl-wv.txt",
                               "unsupported queues configuration", true,
                               orte_process_info.nodename,
                               wv_btl->device->ib_dev->name,
                               (wv_btl->device->ib_dev_attr).VendorId,
                               (wv_btl->device->ib_dev_attr).VendorPartId,
                               mca_btl_wv_component.receive_queues,
                               endpoint->endpoint_proc->proc_ompi->proc_hostname,
                               endpoint->rem_info.rem_vendor_id,
                               endpoint->rem_info.rem_vendor_part_id,
                               values.receive_queues);

                    return OMPI_ERROR;
                }
            }
            break;
    }

    return OMPI_SUCCESS;
}

/*
 *  add a proc to this btl module
 *    creates an endpoint that is setup on the
 *    first send to the endpoint
 */
int mca_btl_wv_add_procs(struct mca_btl_base_module_t* btl,
                         size_t nprocs,
                         struct ompi_proc_t **ompi_procs,
                         struct mca_btl_base_endpoint_t** peers,
                         opal_bitmap_t* reachable)
{
    mca_btl_wv_module_t* wv_btl = (mca_btl_wv_module_t*)btl;
    int i,j, rc;
    int rem_subnet_id_port_cnt;
    int lcl_subnet_id_port_cnt = 0;
    int btl_rank = 0;
    mca_btl_base_endpoint_t* endpoint;
    ompi_btl_wv_connect_base_module_t *local_cpc;
    ompi_btl_wv_connect_base_module_data_t *remote_cpc_data;

    for(j=0; j < mca_btl_wv_component.ib_num_btls; j++){
        if(mca_btl_wv_component.wv_btls[j]->port_info.subnet_id
           == wv_btl->port_info.subnet_id) {
            if(wv_btl == mca_btl_wv_component.wv_btls[j]) {
                btl_rank = lcl_subnet_id_port_cnt;
            }
            lcl_subnet_id_port_cnt++;
        }
    }
    for (i = 0; i < (int) nprocs; i++) {
        struct ompi_proc_t* ompi_proc = ompi_procs[i];
        mca_btl_wv_proc_t* ib_proc;
        int remote_matching_port;

        opal_output(-1, "add procs: adding proc %d", i);

        /* OOB, XOOB, RDMACM, IBCM does not support SELF comunication, so 
         * mark the prco as unreachable by wv btl  */
        if (OPAL_EQUAL == orte_util_compare_name_fields
                (ORTE_NS_CMP_ALL, ORTE_PROC_MY_NAME, &ompi_proc->proc_name)) {
            continue;
        }

        if(NULL == (ib_proc = mca_btl_wv_proc_create(ompi_proc))) {
            return OMPI_ERR_OUT_OF_RESOURCE;
        }

        /* check if the remote proc has any ports that:
           - on the same subnet as the local proc, and
           - on that subnet, has a CPC in common with the local proc
        */
        remote_matching_port = -1;
        rem_subnet_id_port_cnt = 0;
        BTL_VERBOSE(("got %d port_infos ", ib_proc->proc_port_count));
        for (j = 0; j < (int) ib_proc->proc_port_count; j++){
            BTL_VERBOSE(("got a subnet %016" PRIx64,
                         ib_proc->proc_ports[j].pm_port_info.subnet_id));
            if (ib_proc->proc_ports[j].pm_port_info.subnet_id ==
                wv_btl->port_info.subnet_id) {
                BTL_VERBOSE(("Got a matching subnet!"));
                if (rem_subnet_id_port_cnt == btl_rank) {
                    remote_matching_port = j;
                }
                rem_subnet_id_port_cnt++;
            }
        }

        if (0 == rem_subnet_id_port_cnt) {
            /* no use trying to communicate with this endpoint */
            BTL_VERBOSE(("No matching subnet id/CPC was found, moving on.. "));
            continue;
        }

        /* If this process has multiple ports on a single subnet ID,
           and the report proc also has multiple ports on this same
           subnet ID, the default connection pattern is:

                      LOCAL                   REMOTE PEER
                 1st port on subnet X <--> 1st port on subnet X
                 2nd port on subnet X <--> 2nd port on subnet X
                 3nd port on subnet X <--> 3nd port on subnet X
                 ...etc.

           Note that the port numbers may not be contiguous, and they
           may not be the same on either side.  Hence the "1st", "2nd",
           "3rd, etc. notation, above.

           Hence, if the local "rank" of this module's port on the
           subnet ID is greater than the total number of ports on the
           peer on this same subnet, then we have no match.  So skip
           this connection.  */
        if (rem_subnet_id_port_cnt < lcl_subnet_id_port_cnt &&
            btl_rank >= rem_subnet_id_port_cnt) {
            BTL_VERBOSE(("Not enough remote ports on this subnet id, moving on.. "));
            continue;
        }

        /* Now that we have verified that we're on the same subnet and
           the remote peer has enough ports, see if that specific port
           on the peer has a matching CPC. */
        assert(btl_rank <= ib_proc->proc_port_count);
        assert(remote_matching_port != -1);
        if (OMPI_SUCCESS != 
            ompi_btl_wv_connect_base_find_match(wv_btl,
                                                    &(ib_proc->proc_ports[remote_matching_port]),
                                                    &local_cpc,
                                                    &remote_cpc_data)) {
            continue;
        }

        OPAL_THREAD_LOCK(&ib_proc->proc_lock);

        /* The btl_proc datastructure is shared by all IB BTL
         * instances that are trying to reach this destination.
         * Cache the peer instance on the btl_proc.
         */
        endpoint = OBJ_NEW(mca_btl_wv_endpoint_t);
        assert(((opal_object_t*)endpoint)->obj_reference_count == 1);
        if(NULL == endpoint) {
            OPAL_THREAD_UNLOCK(&ib_proc->proc_lock);
            return OMPI_ERR_OUT_OF_RESOURCE;
        }

        mca_btl_wv_endpoint_init(wv_btl, endpoint, 
                                     local_cpc, 
                                     &(ib_proc->proc_ports[remote_matching_port]),
                                     remote_cpc_data);

        rc = mca_btl_wv_proc_insert(ib_proc, endpoint);
        if (OMPI_SUCCESS != rc) {
            OBJ_RELEASE(endpoint);
            OPAL_THREAD_UNLOCK(&ib_proc->proc_lock);
            continue;
        }

         if(OMPI_SUCCESS != mca_btl_wv_tune_endpoint(wv_btl, endpoint)) {
            OBJ_RELEASE(endpoint);
            OPAL_THREAD_UNLOCK(&ib_proc->proc_lock);
            return OMPI_ERROR;
        }

        endpoint->index = opal_pointer_array_add(wv_btl->device->endpoints, (void*)endpoint);
        if( 0 > endpoint->index ) {
            OBJ_RELEASE(endpoint);
            OPAL_THREAD_UNLOCK(&ib_proc->proc_lock);
            continue;
        }

        /* Tell the selected CPC that it won.  NOTE: This call is
           outside of / separate from mca_btl_wv_endpoint_init()
           because this function likely needs the endpoint->index. */
        if (NULL != local_cpc->cbm_endpoint_init) {
            rc = local_cpc->cbm_endpoint_init(endpoint);
            if (OMPI_SUCCESS != rc) {
                OBJ_RELEASE(endpoint);
                OPAL_THREAD_UNLOCK(&ib_proc->proc_lock);
                continue;
            }
        }

        opal_bitmap_set_bit(reachable, i);
        OPAL_THREAD_UNLOCK(&ib_proc->proc_lock);

        peers[i] = endpoint;
    }

    return mca_btl_wv_size_queues(wv_btl, nprocs);
}

/*
 * delete the proc as reachable from this btl module
 */
int mca_btl_wv_del_procs(struct mca_btl_base_module_t* btl,
                         size_t nprocs,
                         struct ompi_proc_t **procs,
                         struct mca_btl_base_endpoint_t ** peers)
{
    int i,ep_index;
    mca_btl_wv_module_t* wv_btl = (mca_btl_wv_module_t*) btl;
    mca_btl_wv_endpoint_t* endpoint;

    for (i=0 ; i < (int) nprocs ; i++) {
        mca_btl_base_endpoint_t* del_endpoint = peers[i];
        for(ep_index=0;
            ep_index < opal_pointer_array_get_size(wv_btl->device->endpoints);
            ep_index++) {
            endpoint = (mca_btl_wv_endpoint_t *)
                opal_pointer_array_get_item(wv_btl->device->endpoints,
                        ep_index);
            if(!endpoint || endpoint->endpoint_btl != wv_btl) {
                continue;
            }
            if (endpoint == del_endpoint) {
                BTL_VERBOSE(("in del_procs %d, setting another endpoint to null",
                             ep_index));
                opal_pointer_array_set_item(wv_btl->device->endpoints,
                        ep_index, NULL);
                assert(((opal_object_t*)endpoint)->obj_reference_count == 1);
                mca_btl_wv_proc_remove(procs[i], endpoint);
                OBJ_RELEASE(endpoint);
            }
        }
    }

    return OMPI_SUCCESS;
}

/*
 *Register callback function for error handling..
 */
int mca_btl_wv_register_error_cb(struct mca_btl_base_module_t* btl,
                                 mca_btl_base_module_error_cb_fn_t cbfunc)
{

    mca_btl_wv_module_t* wv_btl = (mca_btl_wv_module_t*) btl;
    wv_btl->error_cb = cbfunc; /* stash for later */
    return OMPI_SUCCESS;
}

static inline mca_btl_base_descriptor_t *
ib_frag_alloc(mca_btl_wv_module_t *btl, size_t size, uint8_t order,
        uint32_t flags)
{
    int qp, rc;
    ompi_free_list_item_t* item = NULL;

    for(qp = 0; qp < mca_btl_wv_component.num_qps; qp++) {
         if(mca_btl_wv_component.qp_infos[qp].size >= size) {
             OMPI_FREE_LIST_GET(&btl->device->qps[qp].send_free, item, rc);
             if(item)
                 break;
         }
    }
    if(NULL == item)
        return NULL;

    /* not all upper layer users set this */
    to_base_frag(item)->segment.seg_len = size;
    to_base_frag(item)->base.order = order;
    to_base_frag(item)->base.des_flags = flags;

    assert(to_send_frag(item)->qp_idx <= order);
    return &to_base_frag(item)->base;
}

/* check if pending fragment has enough space for coalescing */
static mca_btl_wv_send_frag_t *check_coalescing(opal_list_t *frag_list,
                                                opal_mutex_t *lock,
                                                mca_btl_base_endpoint_t 
                                                *ep, size_t size)
{
    mca_btl_wv_send_frag_t *frag = NULL;

    if(opal_list_is_empty(frag_list))
        return NULL;

    OPAL_THREAD_LOCK(lock);
    if(!opal_list_is_empty(frag_list)) {
        int qp;
        size_t total_length;
        opal_list_item_t *i = opal_list_get_first(frag_list);
        frag = to_send_frag(i);
        if(to_com_frag(frag)->endpoint != ep ||
                MCA_BTL_WV_FRAG_CONTROL == wv_frag_type(frag)) {
            OPAL_THREAD_UNLOCK(lock);
            return NULL;
        }

        total_length = size + frag->coalesced_length +
            to_base_frag(frag)->segment.seg_len +
            sizeof(mca_btl_wv_header_coalesced_t);

        qp = to_base_frag(frag)->base.order;

        if(total_length <= mca_btl_wv_component.qp_infos[qp].size)
            opal_list_remove_first(frag_list);
        else
            frag = NULL;
    }
    OPAL_THREAD_UNLOCK(lock);

    return frag;
}

/**
 * Allocate a segment.
 *
 * @param btl (IN)      BTL module
 * @param size (IN)     Request segment size.
 * @param size (IN) Size of segment to allocate
 *
 * When allocating a segment we pull a pre-alllocated segment
 * from one of two free lists, an eager list and a max list
 */
mca_btl_base_descriptor_t* mca_btl_wv_alloc(struct mca_btl_base_module_t* btl,
                                            struct mca_btl_base_endpoint_t* ep,
                                            uint8_t order,
                                            size_t size,
                                            uint32_t flags)
{
    mca_btl_wv_module_t *obtl = (mca_btl_wv_module_t*)btl;
    int qp = frag_size_to_order(obtl, size);
    mca_btl_wv_send_frag_t *sfrag = NULL;
    mca_btl_wv_coalesced_frag_t *cfrag;

    assert(qp != MCA_BTL_NO_ORDER);

    if(mca_btl_wv_component.use_message_coalescing &&
       (flags & MCA_BTL_DES_FLAGS_BTL_OWNERSHIP)) {
        int prio = !(flags & MCA_BTL_DES_FLAGS_PRIORITY);
        sfrag = check_coalescing(&ep->qps[qp].no_wqe_pending_frags[prio],
                &ep->endpoint_lock, ep, size);

        if(NULL == sfrag) {
            if(BTL_WV_QP_TYPE_PP(qp)) {
                sfrag = check_coalescing(&ep->qps[qp].no_credits_pending_frags[prio],
                        &ep->endpoint_lock, ep, size);
            } else {
                sfrag = check_coalescing(
                        &obtl->qps[qp].u.srq_qp.pending_frags[prio],
                        &obtl->ib_lock, ep, size);
            }
        }
    }

    if(NULL == sfrag)
        return ib_frag_alloc((mca_btl_wv_module_t*)btl, size, order, flags);

    /* begin coalescing message */
    cfrag = alloc_coalesced_frag();
    cfrag->send_frag = sfrag;

    /* fix up new coalescing header if this is the first coalesced frag */
    if(sfrag->hdr != sfrag->chdr) {
        mca_btl_wv_control_header_t *ctrl_hdr;
        mca_btl_wv_header_coalesced_t *clsc_hdr;
        uint8_t org_tag;

        org_tag = sfrag->hdr->tag;
        sfrag->hdr = sfrag->chdr;
        ctrl_hdr = (mca_btl_wv_control_header_t*)(sfrag->hdr + 1);
        clsc_hdr = (mca_btl_wv_header_coalesced_t*)(ctrl_hdr + 1);
        sfrag->hdr->tag = MCA_BTL_TAG_BTL;
        ctrl_hdr->type = MCA_BTL_WV_CONTROL_COALESCED;
        clsc_hdr->tag = org_tag;
        clsc_hdr->size = to_base_frag(sfrag)->segment.seg_len;
        clsc_hdr->alloc_size = to_base_frag(sfrag)->segment.seg_len;
        if(ep->nbo)
            BTL_WV_HEADER_COALESCED_HTON(*clsc_hdr);
        sfrag->coalesced_length = sizeof(mca_btl_wv_control_header_t) +
            sizeof(mca_btl_wv_header_coalesced_t);
        to_com_frag(sfrag)->sg_entry.pAddress = (void*)(uintptr_t)sfrag->hdr; 
    }

    cfrag->hdr = (mca_btl_wv_header_coalesced_t*)
        (((unsigned char*)(sfrag->hdr + 1)) + sfrag->coalesced_length +
        to_base_frag(sfrag)->segment.seg_len);
    cfrag->hdr->alloc_size = size;

    /* point coalesced frag pointer into a data buffer */
    to_base_frag(cfrag)->segment.seg_addr.pval = cfrag->hdr + 1;
    to_base_frag(cfrag)->segment.seg_len = size;

    /* save coalesced fragment on a main fragment; we will need it after send
     * completion to free it and to call upper layer callback */
    opal_list_append(&sfrag->coalesced_frags, (opal_list_item_t*)cfrag);
    sfrag->coalesced_length += (size+sizeof(mca_btl_wv_header_coalesced_t));

    to_base_frag(cfrag)->base.des_flags = flags;

    return &to_base_frag(cfrag)->base;
}

/**
 * Return a segment
 *
 * Return the segment to the appropriate
 *  preallocated segment list
 */
int mca_btl_wv_free(struct mca_btl_base_module_t* btl,
                    mca_btl_base_descriptor_t* des)
{
    /* is this fragment pointing at user memory? */
    if(MCA_BTL_WV_FRAG_SEND_USER == wv_frag_type(des) ||
            MCA_BTL_WV_FRAG_RECV_USER == wv_frag_type(des)) {
        mca_btl_wv_com_frag_t* frag = to_com_frag(des);

        if(frag->registration != NULL) {
            btl->btl_mpool->mpool_deregister(btl->btl_mpool,
                    (mca_mpool_base_registration_t*)frag->registration);
            frag->registration = NULL;
        }
    }

    /* reset those field on free so we will not have to do it on alloc */
    to_base_frag(des)->base.des_flags = 0;
    switch(wv_frag_type(des)) {
        case MCA_BTL_WV_FRAG_RECV:
        case MCA_BTL_WV_FRAG_RECV_USER:
            to_base_frag(des)->base.des_src = NULL;
            to_base_frag(des)->base.des_src_cnt = 0;
            break;
        case MCA_BTL_WV_FRAG_SEND:
            to_send_frag(des)->hdr = (mca_btl_wv_header_t*)
                (((unsigned char*)to_send_frag(des)->chdr) +
                sizeof(mca_btl_wv_header_coalesced_t) +
                sizeof(mca_btl_wv_control_header_t));
            to_com_frag(des)->sg_entry.pAddress =
                (void*)(uintptr_t)to_send_frag(des)->hdr; 
            to_send_frag(des)->coalesced_length = 0;
            assert(!opal_list_get_size(&to_send_frag(des)->coalesced_frags));
            /* fall throug */
        case MCA_BTL_WV_FRAG_SEND_USER:
            to_base_frag(des)->base.des_dst = NULL;
            to_base_frag(des)->base.des_dst_cnt = 0;
            break;
        default:
            break;
    }
    MCA_BTL_IB_FRAG_RETURN(des);

    return OMPI_SUCCESS;
}

/**
 * register user buffer or pack
 * data into pre-registered buffer and return a
 * descriptor that can be
 * used for send/put.
 *
 * @param btl (IN)      BTL module
 * @param peer (IN)     BTL peer addressing
 *
 * prepare source's behavior depends on the following:
 * Has a valid memory registration been passed to prepare_src?
 *    if so we attempt to use the pre-registered user-buffer, if the memory registration
 *    is too small (only a portion of the user buffer) then we must reregister the user buffer
 * Has the user requested the memory to be left pinned?
 *    if so we insert the memory registration into a memory tree for later lookup, we
 *    may also remove a previous registration if a MRU (most recently used) list of
 *    registrations is full, this prevents resources from being exhausted.
 * Is the requested size larger than the btl's max send size?
 *    if so and we aren't asked to leave the registration pinned, then we register the memory if
 *    the users buffer is contiguous
 * Otherwise we choose from two free lists of pre-registered memory in which to pack the data into.
 *
 */
mca_btl_base_descriptor_t* mca_btl_wv_prepare_src(struct mca_btl_base_module_t* btl,
                                                  struct mca_btl_base_endpoint_t* endpoint,
                                                  mca_mpool_base_registration_t* registration,
                                                  struct opal_convertor_t* convertor,
                                                  uint8_t order,
                                                  size_t reserve,
                                                  size_t* size,
                                                  uint32_t flags)
{
    mca_btl_wv_module_t *wv_btl;
    mca_btl_wv_reg_t *wv_reg;
    mca_btl_wv_com_frag_t *frag = NULL;
    struct iovec iov;
    uint32_t iov_count = 1;
    size_t max_data = *size;
    int rc;

    wv_btl = (mca_btl_wv_module_t*)btl;

    if(opal_convertor_need_buffers(convertor) == false && 0 == reserve) {
        /* GMS  bloody HACK! */
        if(registration != NULL || max_data > btl->btl_max_send_size) {
            frag = alloc_send_user_frag();
            if(NULL == frag) {
                return NULL;
            }

            iov.iov_len = max_data;
            iov.iov_base = NULL;

            opal_convertor_pack(convertor, &iov, &iov_count, &max_data);

            *size = max_data;

            if(NULL == registration) {
                rc = btl->btl_mpool->mpool_register(btl->btl_mpool,
                        iov.iov_base, max_data, 0, &registration);
                if(OMPI_SUCCESS != rc || NULL == registration) {
                    MCA_BTL_IB_FRAG_RETURN(frag);
                    return NULL;
                }
                /* keep track of the registration we did */
                to_com_frag(frag)->registration =
                    (mca_btl_wv_reg_t*)registration;
            }
            wv_reg = (mca_btl_wv_reg_t*)registration;
            frag->sg_entry.Length = max_data;
            frag->sg_entry.Lkey = wv_reg->mr->lkey;
            frag->sg_entry.pAddress = (void*)(uintptr_t)iov.iov_base;
            to_base_frag(frag)->base.order = order;
            to_base_frag(frag)->base.des_flags = flags;
            to_base_frag(frag)->segment.seg_len = max_data;
            to_base_frag(frag)->segment.seg_addr.pval = iov.iov_base;
            to_base_frag(frag)->segment.seg_key.key32[0] =
                (uint32_t)frag->sg_entry.Lkey;
            assert(MCA_BTL_NO_ORDER == order);
            BTL_VERBOSE(("frag->sg_entry.lkey = %" PRIu32 " .addr = %" PRIx64
                         " frag->segment.seg_key.key32[0] = %" PRIu32,
                         frag->sg_entry.lkey, 
                         frag->sg_entry.addr,
                         frag->sg_entry.lkey));
            return &to_base_frag(frag)->base;
        }
    }

    assert(MCA_BTL_NO_ORDER == order);

    if(max_data + reserve > btl->btl_max_send_size) {
        max_data = btl->btl_max_send_size - reserve;
    }

    frag = (mca_btl_wv_com_frag_t*)(reserve ?
            mca_btl_wv_alloc(btl, endpoint, order, max_data + reserve,
                flags) :
            ib_frag_alloc(wv_btl, max_data, order, flags));

    if(NULL == frag)
        return NULL;

    iov.iov_len = max_data;
    iov.iov_base = (IOVBASE_TYPE *) ( (unsigned char*)to_base_frag(frag)->segment.seg_addr.pval +
        reserve );
    rc = opal_convertor_pack(convertor, &iov, &iov_count, &max_data);
    *size = max_data;
    /* not all upper layer users set this */
    to_base_frag(frag)->segment.seg_len = max_data + reserve;

    return &to_base_frag(frag)->base;
}

/**
 * Prepare the dst buffer
 *
 * @param btl (IN)      BTL module
 * @param peer (IN)     BTL peer addressing
 * prepare dest's behavior depends on the following:
 * Has a valid memory registration been passed to prepare_src?
 *    if so we attempt to use the pre-registered user-buffer, if the memory registration
 *    is to small (only a portion of the user buffer) then we must reregister the user buffer
 * Has the user requested the memory to be left pinned?
 *    if so we insert the memory registration into a memory tree for later lookup, we
 *    may also remove a previous registration if a MRU (most recently used) list of
 *    registrations is full, this prevents resources from being exhausted.
 */
mca_btl_base_descriptor_t* mca_btl_wv_prepare_dst(struct mca_btl_base_module_t* btl,
                                                  struct mca_btl_base_endpoint_t* endpoint,
                                                  mca_mpool_base_registration_t* registration,
                                                  struct opal_convertor_t* convertor,
                                                  uint8_t order,
                                                  size_t reserve,
                                                  size_t* size,
                                                  uint32_t flags)
{
    mca_btl_wv_module_t *wv_btl;
    mca_btl_wv_component_t *wv_component;
    mca_btl_wv_com_frag_t *frag;
    mca_btl_wv_reg_t *wv_reg;
    size_t max_msg_sz;
    int rc;
    void *buffer;

    wv_btl = (mca_btl_wv_module_t*)btl;
    wv_component = (mca_btl_wv_component_t*)btl->btl_component;

    frag = alloc_recv_user_frag();
    if(NULL == frag) {
        return NULL;
    }
    
    /* max_msg_sz is the maximum message size of the HCA (hw limitation) 
       set the minimum between local max_msg_sz and the remote */
    max_msg_sz = MIN(wv_btl->ib_port_attr.MaxMessageSize,
        endpoint->endpoint_btl->ib_port_attr.MaxMessageSize);

    /* check if user has explicitly limited the max message size */
    if(wv_component->max_hw_msg_size > 0 &&
          max_msg_sz > wv_component->max_hw_msg_size) {
              max_msg_sz = wv_component->max_hw_msg_size;
    }

    /* limit the message so to max_msg_size */
    if(*size > max_msg_sz) {
        *size = max_msg_sz;
        BTL_VERBOSE("message size limited to %d",*size);
    }

    opal_convertor_get_current_pointer(convertor, &buffer);

    if(NULL == registration){
        /* we didn't get a memory registration passed in, so we have to
         * register the region ourselves
         */
        rc = btl->btl_mpool->mpool_register(btl->btl_mpool, buffer, *size, 0,
                &registration);
        if(OMPI_SUCCESS != rc || NULL == registration) {
            MCA_BTL_IB_FRAG_RETURN(frag);
            return NULL;
        }
        /* keep track of the registration we did */
        frag->registration = (mca_btl_wv_reg_t*)registration;
    }
    wv_reg = (mca_btl_wv_reg_t*)registration;
    frag->sg_entry.Length = *size;
    frag->sg_entry.Lkey = wv_reg->mr->lkey;
    frag->sg_entry.pAddress = (void*)(uintptr_t)buffer; 
    to_base_frag(frag)->segment.seg_addr.pval = buffer;
    to_base_frag(frag)->segment.seg_len = *size;
    to_base_frag(frag)->segment.seg_key.key32[0] = wv_reg->mr->rkey;
    to_base_frag(frag)->base.order = order;
    to_base_frag(frag)->base.des_flags = flags;
    BTL_VERBOSE(("frag->sg_entry.lkey = %" PRIu32 " .addr = %" PRIx64 " "
                 "frag->segment.seg_key.key32[0] = %" PRIu32,
                 frag->sg_entry.lkey, 
                 frag->sg_entry.addr,
                 wv_reg->mr->rkey));

    return &to_base_frag(frag)->base;
}

static int mca_btl_wv_finalize_resources(struct mca_btl_base_module_t* btl) {
    mca_btl_wv_module_t* wv_btl;
    mca_btl_wv_endpoint_t* endpoint;
    int ep_index, i;
    int qp, rc = OMPI_SUCCESS;
    wv_btl = (mca_btl_wv_module_t*) btl;
    /* Sanity check */
    if( mca_btl_wv_component.ib_num_btls <= 0 ) {
        return OMPI_SUCCESS;
    }
    /* Release all QPs */
    for (ep_index=0;
         ep_index < opal_pointer_array_get_size(wv_btl->device->endpoints);
         ep_index++) {
        endpoint=(mca_btl_wv_endpoint_t *)opal_pointer_array_get_item(wv_btl->device->endpoints,
                                                  ep_index);
        if(!endpoint) {
            BTL_VERBOSE(("In finalize, got another null endpoint"));
            continue;
        }
        if(endpoint->endpoint_btl != wv_btl) {
            continue;
        }
        for(i = 0; i < wv_btl->device->eager_rdma_buffers_count; i++) {
            if(wv_btl->device->eager_rdma_buffers[i] == endpoint) {
                wv_btl->device->eager_rdma_buffers[i] = NULL;
                OBJ_RELEASE(endpoint);
            }
        }
        OBJ_RELEASE(endpoint);
    }

    /* Release SRQ resources */
    for(qp = 0; qp < mca_btl_wv_component.num_qps; qp++) {
        if(!BTL_WV_QP_TYPE_PP(qp)) {
            MCA_BTL_WV_CLEAN_PENDING_FRAGS(
                    &wv_btl->qps[qp].u.srq_qp.pending_frags[0]);
            MCA_BTL_WV_CLEAN_PENDING_FRAGS(
                    &wv_btl->qps[qp].u.srq_qp.pending_frags[1]);
            if (NULL != wv_btl->qps[qp].u.srq_qp.srq) {
                wv_btl->qps[qp].u.srq_qp.srq->handle->Release();
                free(wv_btl->qps[qp].u.srq_qp.srq);
            }
            OBJ_DESTRUCT(&wv_btl->qps[qp].u.srq_qp.pending_frags[0]);
            OBJ_DESTRUCT(&wv_btl->qps[qp].u.srq_qp.pending_frags[1]);
        }
    }

    /* Finalize the CPC modules on this wv module */
    for (i = 0; i < wv_btl->num_cpcs; ++i) {
        if (NULL != wv_btl->cpcs[i]->cbm_finalize) {
            wv_btl->cpcs[i]->cbm_finalize(wv_btl, wv_btl->cpcs[i]);
        }
        free(wv_btl->cpcs[i]);
    }
    free(wv_btl->cpcs);

    /* Release device if there are no more users */
    if(!(--wv_btl->device->btls)) {
        OBJ_RELEASE(wv_btl->device);
    }

    if (NULL != wv_btl->qps) {
        free(wv_btl->qps); 
    }

    return rc;
}


int mca_btl_wv_finalize(struct mca_btl_base_module_t* btl)
{
    mca_btl_wv_module_t* wv_btl;
    int i, rc = OMPI_SUCCESS;

    wv_btl = (mca_btl_wv_module_t*) btl;

    /* Sanity check */
    if( mca_btl_wv_component.ib_num_btls <= 0 ) {
        return 0;
    }

    if( OMPI_SUCCESS != (rc = mca_btl_wv_finalize_resources(btl) ) ) {
        BTL_VERBOSE(("Failed to finalize resources"));
    }

    /* Remove the btl from component list */
    if ( mca_btl_wv_component.ib_num_btls > 1 ) {
        for(i = 0; i < mca_btl_wv_component.ib_num_btls; i++){
            if (mca_btl_wv_component.wv_btls[i] == wv_btl){
                mca_btl_wv_component.wv_btls[i] =
                    mca_btl_wv_component.wv_btls[mca_btl_wv_component.ib_num_btls-1];
                break;
            }
        }
    }

    mca_btl_wv_component.ib_num_btls--;

    OBJ_DESTRUCT(&wv_btl->ib_lock);
    free(wv_btl);

    BTL_VERBOSE(("Success in closing BTL resources"));

    return rc;
}

/*
 *  Send immediate - Minimum function calls minimum checks, send the data ASAP.
 *  If BTL can't to send the messages imidiate, it creates messages descriptor 
 *  returns it to PML.
 */
int mca_btl_wv_sendi(struct mca_btl_base_module_t* btl,
                     struct mca_btl_base_endpoint_t* ep,
                     struct opal_convertor_t* convertor,
                     void* header,
                     size_t header_size,
                     size_t payload_size,
                     uint8_t order,
                     uint32_t flags,
                     mca_btl_base_tag_t tag,
                     mca_btl_base_descriptor_t** descriptor) 
{
    mca_btl_wv_module_t *obtl = (mca_btl_wv_module_t*)btl;
    size_t size = payload_size + header_size;
    size_t eager_limit;
    int rc,  
        qp = frag_size_to_order(obtl, size), 
        prio = !(flags & MCA_BTL_DES_FLAGS_PRIORITY), 
        ib_rc;
    int32_t cm_return;
    bool do_rdma = false;
    ompi_free_list_item_t* item = NULL;
    mca_btl_wv_frag_t *frag;
    mca_btl_wv_header_t *hdr;

    OPAL_THREAD_LOCK(&ep->endpoint_lock);

    if (OPAL_UNLIKELY(MCA_BTL_IB_CONNECTED != ep->endpoint_state)) {
        goto cant_send;
    }

    /* If it is pending messages on the qp - we can not send */
    if(OPAL_UNLIKELY(!opal_list_is_empty(&ep->qps[qp].no_wqe_pending_frags[prio]))) {
        goto cant_send;
    }

    /* Allocate WQE */
    if(OPAL_UNLIKELY(qp_get_wqe(ep, qp) < 0)) {
        goto no_credits_or_wqe;
    }

    /* eager rdma or send ? Check eager rdma credits */
    /* Note: Maybe we want to implement isend only for eager rdma ?*/
    eager_limit = mca_btl_wv_component.eager_limit +
        sizeof(mca_btl_wv_header_coalesced_t) +
        sizeof(mca_btl_wv_control_header_t);

    if(OPAL_LIKELY(size <= eager_limit)) {
        if(acquire_eager_rdma_send_credit(ep) == OMPI_SUCCESS) {
            do_rdma = true;
        }
    }

    /* Check send credits if it is no rdma */
    if(!do_rdma) {
        if(BTL_WV_QP_TYPE_PP(qp)) {
            if(OPAL_UNLIKELY(OPAL_THREAD_ADD32(&ep->qps[qp].u.pp_qp.sd_credits, -1) < 0)){
                OPAL_THREAD_ADD32(&ep->qps[qp].u.pp_qp.sd_credits, 1);
                goto no_credits_or_wqe;
            }
        } else {
            if(OPAL_UNLIKELY(OPAL_THREAD_ADD32(&obtl->qps[qp].u.srq_qp.sd_credits, -1) < 0)){
                OPAL_THREAD_ADD32(&obtl->qps[qp].u.srq_qp.sd_credits, 1);
                goto no_credits_or_wqe;
            }
        }
    }

    /* Allocate fragment */
    OMPI_FREE_LIST_GET(&obtl->device->qps[qp].send_free, item, rc);
    if(OPAL_UNLIKELY(NULL == item)) {
        /* we don't return NULL because maybe later we will try to coalesce */
        goto no_frags;
    }
    frag = to_base_frag(item);
    hdr = to_send_frag(item)->hdr;
    frag->segment.seg_len = size;
    frag->base.order = qp;
    frag->base.des_flags = flags;
    hdr->tag = tag;
    to_com_frag(item)->endpoint = ep;

    /* put match header */
    memcpy(frag->segment.seg_addr.pval, header, header_size);

    /* Pack data */
    if(payload_size) {
        size_t max_data;
        struct iovec iov;
        uint32_t iov_count;
        /* pack the data into the supplied buffer */
        iov.iov_base = (IOVBASE_TYPE*)((unsigned char*)frag->segment.seg_addr.pval + header_size);
        iov.iov_len  = max_data = payload_size;
        iov_count    = 1;

        (void)opal_convertor_pack( convertor, &iov, &iov_count, &max_data);

        assert(max_data == payload_size);
    }

    /* Set all credits */
    BTL_WV_GET_CREDITS(ep->eager_rdma_local.credits, hdr->credits);
    if(hdr->credits)
        hdr->credits |= BTL_WV_RDMA_CREDITS_FLAG;

    if(!do_rdma) {
        if(BTL_WV_QP_TYPE_PP(qp) && 0 == hdr->credits) {
            BTL_WV_GET_CREDITS(ep->qps[qp].u.pp_qp.rd_credits, hdr->credits);
        }
    } else {
        hdr->credits |= (qp << 11);
    }

    BTL_WV_GET_CREDITS(ep->qps[qp].u.pp_qp.cm_return, cm_return);
    /* cm_seen is only 8 bytes, but cm_return is 32 bytes */
    if(cm_return > 255) {
        hdr->cm_seen = 255;
        cm_return -= 255;
        OPAL_THREAD_ADD32(&ep->qps[qp].u.pp_qp.cm_return, cm_return);
    } else {
        hdr->cm_seen = cm_return;
    }

    ib_rc = post_send(ep, to_send_frag(item), do_rdma);

    if(!ib_rc) {
        OPAL_THREAD_UNLOCK(&ep->endpoint_lock);
        return OMPI_SUCCESS;
    }

    /* Failed to send, do clean up all allocated resources */
    if(ep->nbo) {
        BTL_WV_HEADER_NTOH(*hdr);
    }
    if(BTL_WV_IS_RDMA_CREDITS(hdr->credits)) {
        OPAL_THREAD_ADD32(&ep->eager_rdma_local.credits,
                BTL_WV_CREDITS(hdr->credits));
    }
    if (!do_rdma && BTL_WV_QP_TYPE_PP(qp)) {
        OPAL_THREAD_ADD32(&ep->qps[qp].u.pp_qp.rd_credits,
                hdr->credits);
    }
no_frags:
    if(do_rdma) {
        OPAL_THREAD_ADD32(&ep->eager_rdma_remote.tokens, 1);
    } else {
        if(BTL_WV_QP_TYPE_PP(qp)) {
            OPAL_THREAD_ADD32(&ep->qps[qp].u.pp_qp.sd_credits, 1);
        } else if BTL_WV_QP_TYPE_SRQ(qp){
            OPAL_THREAD_ADD32(&obtl->qps[qp].u.srq_qp.sd_credits, 1);
        }
    }
no_credits_or_wqe:
    qp_put_wqe(ep, qp);
cant_send:
    OPAL_THREAD_UNLOCK(&ep->endpoint_lock);
    /* We can not send the data directly, so we just return descriptor */
    *descriptor = mca_btl_wv_alloc(btl, ep, order, size, flags);
    return OMPI_ERR_RESOURCE_BUSY;
}
/*
 *  Initiate a send.
 */

int mca_btl_wv_send(
    struct mca_btl_base_module_t* btl,
    struct mca_btl_base_endpoint_t* ep,
    struct mca_btl_base_descriptor_t* des,
    mca_btl_base_tag_t tag)

{
    mca_btl_wv_send_frag_t *frag;

    assert(wv_frag_type(des) == MCA_BTL_WV_FRAG_SEND ||
            wv_frag_type(des) == MCA_BTL_WV_FRAG_COALESCED);

    if(wv_frag_type(des) == MCA_BTL_WV_FRAG_COALESCED) {
        to_coalesced_frag(des)->hdr->tag = tag;
        to_coalesced_frag(des)->hdr->size = des->des_src->seg_len;
        if(ep->nbo)
            BTL_WV_HEADER_COALESCED_HTON(*to_coalesced_frag(des)->hdr);
        frag = to_coalesced_frag(des)->send_frag;
    } else {
        frag = to_send_frag(des);
        to_com_frag(des)->endpoint = ep;
        frag->hdr->tag = tag;
    }

    des->des_flags |= MCA_BTL_DES_SEND_ALWAYS_CALLBACK;

    return mca_btl_wv_endpoint_send(ep, frag);
}

/*
 * RDMA WRITE local buffer to remote buffer address.
 */

int mca_btl_wv_put(mca_btl_base_module_t* btl,
                   mca_btl_base_endpoint_t* ep,
                   mca_btl_base_descriptor_t* descriptor)
{
    WV_SEND_REQUEST *bad_wr;
    HRESULT hr = 0;
    mca_btl_wv_out_frag_t* frag = to_out_frag(descriptor);
    int qp = descriptor->order;
    uint64_t rem_addr = descriptor->des_dst->seg_addr.lval;
    uint32_t rkey = descriptor->des_dst->seg_key.key32[0];
    assert(wv_frag_type(frag) == MCA_BTL_WV_FRAG_SEND_USER ||
            wv_frag_type(frag) == MCA_BTL_WV_FRAG_SEND);
    descriptor->des_flags |= MCA_BTL_DES_SEND_ALWAYS_CALLBACK;
    
    if(ep->endpoint_state != MCA_BTL_IB_CONNECTED) {
        int rc;
        OPAL_THREAD_LOCK(&ep->endpoint_lock);
        rc = check_endpoint_state(ep, descriptor, &ep->pending_put_frags);
        OPAL_THREAD_UNLOCK(&ep->endpoint_lock);
        if(OMPI_ERR_RESOURCE_BUSY == OPAL_SOS_GET_ERROR_CODE(rc))
            return OMPI_SUCCESS;
        if(OMPI_SUCCESS != rc)
            return rc;
    }

    if(MCA_BTL_NO_ORDER == qp)
        qp = mca_btl_wv_component.rdma_qp;

    /* check for a send wqe */
    if (qp_get_wqe(ep, qp) < 0) {
        qp_put_wqe(ep, qp);
        OPAL_THREAD_LOCK(&ep->endpoint_lock);
        opal_list_append(&ep->pending_put_frags, (opal_list_item_t*)frag);
        OPAL_THREAD_UNLOCK(&ep->endpoint_lock);
        return OMPI_SUCCESS;
    }

    frag->sr_desc.Wr.Rdma.RemoteAddress = htonll(rem_addr); 
    frag->sr_desc.Wr.Rdma.Rkey = htonl(rkey);  
    to_com_frag(frag)->sg_entry.pAddress =
        (void*)(uintptr_t)descriptor->des_src->seg_addr.pval;
    to_com_frag(frag)->sg_entry.Length = descriptor->des_src->seg_len;
    to_com_frag(frag)->endpoint = ep;
    descriptor->order = qp;
    /* Setting opcode on a frag constructor isn't enough since prepare_src
     * may return send_frag instead of put_frag */
    frag->sr_desc.Opcode = WvRdmaWrite;
    frag->sr_desc.Flags = ib_send_flags(descriptor->des_src->seg_len, &(ep->qps[qp]));
    hr = ep->qps[qp].qp->lcl_qp->handle->PostSend((WV_SEND_REQUEST*)&frag->sr_desc,
                                                    (WV_SEND_REQUEST**)&bad_wr);
    if(FAILED(hr))
        return OMPI_ERROR;

    return OMPI_SUCCESS;
}

/*
 * RDMA READ remote buffer to local buffer address.
 */

int mca_btl_wv_get(mca_btl_base_module_t* btl,
                   mca_btl_base_endpoint_t* ep,
                   mca_btl_base_descriptor_t* descriptor)
{
    WV_SEND_REQUEST *bad_wr; 
    HRESULT hr = 0;
    mca_btl_wv_get_frag_t* frag = to_get_frag(descriptor);
    int qp = descriptor->order;
    uint64_t rem_addr = descriptor->des_src->seg_addr.lval;
    uint32_t rkey = descriptor->des_src->seg_key.key32[0];
    assert(wv_frag_type(frag) == MCA_BTL_WV_FRAG_RECV_USER);
    descriptor->des_flags |= MCA_BTL_DES_SEND_ALWAYS_CALLBACK;

    if(ep->endpoint_state != MCA_BTL_IB_CONNECTED) {
        int rc;
        OPAL_THREAD_LOCK(&ep->endpoint_lock);
        rc = check_endpoint_state(ep, descriptor, &ep->pending_get_frags);
        OPAL_THREAD_UNLOCK(&ep->endpoint_lock);
        if(OMPI_ERR_RESOURCE_BUSY == OPAL_SOS_GET_ERROR_CODE(rc))
            return OMPI_SUCCESS;
        if(OMPI_SUCCESS != rc)
            return rc;
    }

    if(MCA_BTL_NO_ORDER == qp)
        qp = mca_btl_wv_component.rdma_qp;

    /* check for a send wqe */
    if (qp_get_wqe(ep, qp) < 0) {
        qp_put_wqe(ep, qp);
        OPAL_THREAD_LOCK(&ep->endpoint_lock);
        opal_list_append(&ep->pending_get_frags, (opal_list_item_t*)frag);
        OPAL_THREAD_UNLOCK(&ep->endpoint_lock);
        return OMPI_SUCCESS;
    }

    /* check for a get token */
    if(OPAL_THREAD_ADD32(&ep->get_tokens,-1) < 0) {
        qp_put_wqe(ep, qp);
        OPAL_THREAD_ADD32(&ep->get_tokens,1);
        OPAL_THREAD_LOCK(&ep->endpoint_lock);
        opal_list_append(&ep->pending_get_frags, (opal_list_item_t*)frag);
        OPAL_THREAD_UNLOCK(&ep->endpoint_lock);
        return OMPI_SUCCESS;
    }

    frag->sr_desc.Wr.Rdma.RemoteAddress = htonll(rem_addr);
    frag->sr_desc.Wr.Rdma.Rkey = htonl(rkey);
    to_com_frag(frag)->sg_entry.pAddress =
        (void*)(uintptr_t)descriptor->des_dst->seg_addr.pval; 
    to_com_frag(frag)->sg_entry.Length  = descriptor->des_dst->seg_len;
    to_com_frag(frag)->endpoint = ep;
    descriptor->order = qp;
    hr = ep->qps[qp].qp->lcl_qp->handle->PostSend((WV_SEND_REQUEST*)&frag->sr_desc,
                                                    (WV_SEND_REQUEST**)&bad_wr);
    if(FAILED(hr))
        return OMPI_ERROR;

    return OMPI_SUCCESS;
}

int mca_btl_wv_ft_event(int state) {
    return OMPI_SUCCESS;
}
