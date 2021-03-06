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

#ifndef MCA_BTL_UGNI_ENDPOINT_H 
#define MCA_BTL_UGNI_ENDPOINT_H

#include "btl_ugni.h"

typedef struct mca_btl_ugni_smsg_mbox_t {
    ompi_free_list_item_t super;
    gni_smsg_attr_t  smsg_attrib;
} mca_btl_ugni_smsg_mbox_t;

OBJ_CLASS_DECLARATION(mca_btl_ugni_smsg_mbox_t);

typedef struct mca_btl_base_endpoint_t {
    opal_object_t super;

    ompi_common_ugni_endpoint_t *common;

    mca_btl_ugni_module_t *btl;

    gni_smsg_attr_t remote_smsg_attrib;

    mca_btl_ugni_smsg_mbox_t *mailbox;

    opal_list_t pending_list;
    opal_list_t pending_smsg_sends;
} mca_btl_base_endpoint_t;

OBJ_CLASS_DECLARATION(mca_btl_base_endpoint_t);

#define MCA_BTL_UGNI_EP_STATE(ep) ((ep)->common->state)


int mca_btl_ugni_ep_connect_progress (mca_btl_base_endpoint_t *ep);
int mca_btl_ugni_ep_disconnect (mca_btl_base_endpoint_t *ep, bool send_disconnect);

static inline int mca_btl_ugni_init_ep (mca_btl_base_endpoint_t **ep,
                                        mca_btl_ugni_module_t *btl,
                                        ompi_proc_t *peer_proc) {
    mca_btl_base_endpoint_t *endpoint;
    int rc;

    endpoint = OBJ_NEW(mca_btl_base_endpoint_t);
    assert (endpoint != NULL);

    rc = ompi_common_ugni_endpoint_for_proc (btl->device, peer_proc, &endpoint->common);
    if (OMPI_SUCCESS != rc) {
        assert (0);
        return rc;
    }

    endpoint->btl = btl;
    endpoint->common->btl_ctx = (void *) endpoint;

    *ep = endpoint;

    return OMPI_SUCCESS;        
}

static inline void mca_btl_ugni_release_ep (mca_btl_base_endpoint_t *ep) {
    int rc;

    rc = mca_btl_ugni_ep_disconnect (ep, false);
    if (OMPI_SUCCESS == rc) {
        BTL_VERBOSE(("btl/ugni error disconnecting endpoint"));
    }

    ompi_common_ugni_endpoint_return (ep->common);

    OBJ_RELEASE(ep);
}

static inline int mca_btl_ugni_check_endpoint_state (mca_btl_base_endpoint_t *ep) {
    int rc;

    if (OPAL_LIKELY(OMPI_COMMON_UGNI_CONNECTED == ep->common->state)) {
        return OMPI_SUCCESS;
    }

    OPAL_THREAD_LOCK(&ep->common->lock);

    switch (ep->common->state) {
    case OMPI_COMMON_UGNI_INIT:
        rc = mca_btl_ugni_ep_connect_progress (ep);
        if (OMPI_SUCCESS != rc) {
            break;
        }
    case OMPI_COMMON_UGNI_CONNECTING:
        rc = OMPI_ERR_RESOURCE_BUSY;
        break;
    default:
        rc = OMPI_SUCCESS;
    }

    OPAL_THREAD_UNLOCK(&ep->common->lock);

    return rc;
}

static inline int mca_btl_ugni_wildcard_ep_post (mca_btl_ugni_module_t *ugni_module) {
    int rc;

    memset (&ugni_module->wc_local_attr, 0, sizeof (ugni_module->wc_local_attr));
    rc = GNI_EpPostDataWId (ugni_module->wildcard_ep, &ugni_module->wc_local_attr, sizeof (ugni_module->wc_local_attr),
                            &ugni_module->wc_remote_attr, sizeof (ugni_module->wc_remote_attr),
                            MCA_BTL_UGNI_CONNECT_WILDCARD_ID | ORTE_PROC_MY_NAME->vpid);

    return ompi_common_rc_ugni_to_ompi (rc);
}

static inline int mca_btl_ugni_directed_ep_post (mca_btl_base_endpoint_t *ep) {
    int rc;
    rc = GNI_EpPostDataWId (ep->common->ep_handle, &ep->mailbox->smsg_attrib, sizeof (ep->mailbox->smsg_attrib),
                            &ep->remote_smsg_attrib, sizeof (ep->remote_smsg_attrib),
                            MCA_BTL_UGNI_CONNECT_DIRECTED_ID | ep->common->ep_rem_id);

    return ompi_common_rc_ugni_to_ompi (rc);
}

#endif /* MCA_BTL_UGNI_ENDPOINT_H */
