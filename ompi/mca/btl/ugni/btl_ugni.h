/* -*- Mode: C; c-basic-offset:4 ; indent-tabs-mode:nil -*- */
/*
 * Copyright (c) 2011-2012 Los Alamos National Security, LLC. All rights
 *                         reserved.
 * Copyright (c) 2011      UT-Battelle, LLC. All rights reserved.
 * $COPYRIGHT$
 *
 * Additional copyrights may follow
 *
 * $HEADER$
 */
/*
 * The ugni btl is implemented with native Cray Gemini.
 *
 * Known issues with ugni:
 *  -
 */

#ifndef MCA_BTL_UGNI_H
#define MCA_BTL_UGNI_H

#include "ompi_config.h"

#include "ompi/mca/mpool/mpool.h"
#include "ompi/mca/mpool/base/base.h"
#include "ompi/mca/mpool/rdma/mpool_rdma.h"
#include "ompi/runtime/ompi_module_exchange.h"
#include "opal/util/output.h"
#include "opal_stdint.h"
#include "opal/class/opal_hash_table.h"

#include "ompi/mca/btl/btl.h"
#include "ompi/mca/btl/base/base.h"
#include "ompi/mca/btl/base/btl_base_error.h"
#include "ompi/class/ompi_free_list.h"

#include "ompi/mca/common/ugni/common_ugni.h"

#include <errno.h>
#include <stdint.h>
#include <sys/types.h>
#include <assert.h>
#include <sys/time.h>
#include <gni_pub.h>

/* datagram message ids */
#define MCA_BTL_UGNI_CONNECT_WILDCARD_ID 0x6b69726b00000000ull
#define MCA_BTL_UGNI_CONNECT_DIRECTED_ID 0x6b61686e00000000ull
#define MCA_BTL_UGNI_DATAGRAM_MASK       0xffffffff00000000ull

typedef struct mca_btl_ugni_module_t {
    mca_btl_base_module_t super;

    ompi_common_ugni_device_t *device;

    size_t endpoint_count;
    struct mca_btl_base_endpoint_t **endpoints;

    opal_list_t failed_frags;

    mca_mpool_base_module_t *smsg_mpool;
    ompi_free_list_t         smsg_mboxes;

    gni_ep_handle_t wildcard_ep;
    gni_smsg_attr_t wc_remote_attr, wc_local_attr;

    gni_cq_handle_t bte_local_cq;
    gni_cq_handle_t smsg_remote_cq;

    /* eager (registered) fragment list */
    ompi_free_list_t eager_frags_send;
    ompi_free_list_t eager_frags_recv;

    /* SMSG fragment list (unregistered) */
    ompi_free_list_t smsg_frags;

    /* RDMA fragment list */
    ompi_free_list_t rdma_frags;
    ompi_free_list_t rdma_int_frags;

    /* fragment buffer (for message if lookup) */
    opal_hash_table_t pending_smsg_frags;
    int32_t next_frag_id;
} mca_btl_ugni_module_t;

typedef struct mca_btl_ugni_component_t {
    /* base BTL component */
    mca_btl_base_component_2_0_0_t super;

    /* maximum supported btls. hardcoded to 1 for now */
    uint32_t ugni_max_btls;
    /* Maximum number of entries a completion queue can hold */
    uint32_t cq_size;

    /* number of ugni modules */
    uint32_t ugni_num_btls;
    /* ugni modules */
    mca_btl_ugni_module_t *modules;

    size_t smsg_max_data;

    /* After this message size switch to BTE protocols */
    size_t ugni_fma_limit;
    /* Switch to put when trying to GET at or above this size */
    size_t ugni_get_limit;
    /* Switch to get when sending above this size */
    size_t ugni_smsg_limit;

    /* RDMA/SMSG free list settings */
    int ugni_free_list_num;
    int ugni_free_list_max;
    int ugni_free_list_inc;

    /* eager free list settings */
    int ugni_eager_num;
    int ugni_eager_max;
    int ugni_eager_inc;

    /* number of times to retry a post */
    int rdma_max_retries;

    /* Maximum number of outstanding eager messages */
    int smsg_max_credits;
    /* mailbox size (computed) */
    int smsg_mbox_size;
} mca_btl_ugni_component_t;

int mca_btl_ugni_module_init (mca_btl_ugni_module_t *ugni_module,
                              ompi_common_ugni_device_t *device);

/**
 * BML->BTL notification of change in the process list. 
 *
 * location: btl_ugni_add_procs.c
 *
 * @param btl (IN)            BTL module
 * @param nprocs (IN)         Number of processes
 * @param procs (IN)          Array of processes
 * @param endpoint (OUT)      Array of mca_btl_base_endpoint_t structures by BTL.
 * @param reachable (OUT)     Bitmask indicating set of peer processes that are reachable by this BTL.
 * @return                    OMPI_SUCCESS or error status on failure.
 */
int
mca_btl_ugni_add_procs (struct mca_btl_base_module_t* btl,
                        size_t nprocs,
                        struct ompi_proc_t **procs,
                        struct mca_btl_base_endpoint_t **peers,
                        opal_bitmap_t *reachable);

/**
 * Notification of change to the process list.
 *
 * location: btl_ugni_add_procs.c
 *
 * @param btl (IN)     BTL module
 * @param nprocs (IN)  Number of processes
 * @param proc (IN)    Set of processes
 * @param peer (IN)    Set of peer addressing information.
 * @return             Status indicating if cleanup was successful
 */
int
mca_btl_ugni_del_procs (struct mca_btl_base_module_t *btl,
                        size_t nprocs,
                        struct ompi_proc_t **procs,
                        struct mca_btl_base_endpoint_t **peers);

/**
 * Initiate an asynchronous send.
 *
 * location: btl_ugni_send.c
 *
 * @param btl (IN)         BTL module
 * @param endpoint (IN)    BTL addressing information
 * @param descriptor (IN)  Description of the data to be transfered
 * @param tag (IN)         The tag value used to notify the peer.
 */
int
mca_btl_ugni_send (struct mca_btl_base_module_t *btl,
                   struct mca_btl_base_endpoint_t *btl_peer,
                   struct mca_btl_base_descriptor_t *descriptor,
                   mca_btl_base_tag_t tag);

/**
 * Initiate an immediate blocking send. 
 *
 * location: btl_ugni_sendi.c
 *
 * @param btl (IN)             BTL module
 * @param endpoint (IN)        BTL addressing information
 * @param convertor (IN)       Data type convertor
 * @param header (IN)          Pointer to header.
 * @param header_size (IN)     Size of header.
 * @param payload_size (IN)    Size of payload (from convertor).
 * @param order (IN)           The ordering tag (may be MCA_BTL_NO_ORDER)
 * @param flags (IN)           Flags.
 * @param tag (IN)             The tag value used to notify the peer.
 * @param descriptor (OUT)     The descriptor to be returned unable to be sent immediately
 */
int
mca_btl_ugni_sendi (struct mca_btl_base_module_t *btl,
                    struct mca_btl_base_endpoint_t *endpoint,
                    struct opal_convertor_t *convertor,
                    void *header, size_t header_size,
                    size_t payload_size, uint8_t order,
                    uint32_t flags, mca_btl_base_tag_t tag,
                    mca_btl_base_descriptor_t **descriptor);

/**
 * Initiate a get operation.
 *
 * location: btl_ugni_get.c
 *
 * @param btl (IN)         BTL module
 * @param endpoint (IN)    BTL addressing information
 * @param descriptor (IN)  Description of the data to be transferred
 */
int
mca_btl_ugni_get (struct mca_btl_base_module_t *btl,
                  struct mca_btl_base_endpoint_t *endpoint,
                  struct mca_btl_base_descriptor_t *des);

/**
 * Initiate a put operation.
 *
 * location: btl_ugni_put.c
 *
 * @param btl (IN)         BTL module
 * @param endpoint (IN)    BTL addressing information
 * @param descriptor (IN)  Description of the data to be transferred
 */
int
mca_btl_ugni_put (struct mca_btl_base_module_t *btl,
                  struct mca_btl_base_endpoint_t *endpoint,
                  struct mca_btl_base_descriptor_t *des);

mca_btl_base_descriptor_t *
mca_btl_ugni_alloc(struct mca_btl_base_module_t *btl,
                   struct mca_btl_base_endpoint_t *endpoint,
                   uint8_t order, size_t size, uint32_t flags);

typedef struct mca_btl_ugni_reg_t {
    mca_mpool_base_registration_t  base;
    gni_mem_handle_t                memory_hdl;
} mca_btl_ugni_reg_t;

/* Global structures */ 

OMPI_MODULE_DECLSPEC extern mca_btl_ugni_component_t mca_btl_ugni_component;
OMPI_MODULE_DECLSPEC extern mca_btl_ugni_module_t mca_btl_ugni_module;

#endif
