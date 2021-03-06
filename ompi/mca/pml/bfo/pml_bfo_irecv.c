/*
 * Copyright (c) 2004-2005 The Trustees of Indiana University and Indiana
 *                         University Research and Technology
 *                         Corporation.  All rights reserved.
 * Copyright (c) 2004-2005 The University of Tennessee and The University
 *                         of Tennessee Research Foundation.  All rights
 *                         reserved.
 * Copyright (c) 2004-2005 High Performance Computing Center Stuttgart, 
 *                         University of Stuttgart.  All rights reserved.
 * Copyright (c) 2004-2005 The Regents of the University of California.
 *                         All rights reserved.
 * Copyright (c) 2007      Los Alamos National Security, LLC.  All rights
 *                         reserved. 
 * Copyright (c) 2010-2012 Oracle and/or its affiliates.  All rights reserved.
 * $COPYRIGHT$
 * 
 * Additional copyrights may follow
 * 
 * $HEADER$
 */

#include "ompi_config.h"
#include "ompi/request/request.h"
#include "pml_bfo_recvreq.h"
#include "pml_bfo_recvfrag.h"
#include "ompi/peruse/peruse-internal.h"
#include "ompi/message/message.h"

int mca_pml_bfo_irecv_init(void *addr,
                           size_t count,
                           ompi_datatype_t * datatype,
                           int src,
                           int tag,
                           struct ompi_communicator_t *comm,
                           struct ompi_request_t **request)
{
    int rc;
    mca_pml_bfo_recv_request_t *recvreq;
    MCA_PML_BFO_RECV_REQUEST_ALLOC(recvreq, rc);
    if (NULL == recvreq)
        return rc;

    MCA_PML_BFO_RECV_REQUEST_INIT(recvreq,
                                   addr,
                                   count, datatype, src, tag, comm, true);
    
    PERUSE_TRACE_COMM_EVENT (PERUSE_COMM_REQ_ACTIVATE,
                             &((recvreq)->req_recv.req_base),
                             PERUSE_RECV);                              

    *request = (ompi_request_t *) recvreq;
    return OMPI_SUCCESS;
}

int mca_pml_bfo_irecv(void *addr,
                      size_t count,
                      ompi_datatype_t * datatype,
                      int src,
                      int tag,
                      struct ompi_communicator_t *comm,
                      struct ompi_request_t **request)
{
    int rc;

    mca_pml_bfo_recv_request_t *recvreq;
    MCA_PML_BFO_RECV_REQUEST_ALLOC(recvreq, rc);
    if (NULL == recvreq)
        return rc;

    MCA_PML_BFO_RECV_REQUEST_INIT(recvreq,
                                   addr,
                                   count, datatype, src, tag, comm, false);

    PERUSE_TRACE_COMM_EVENT (PERUSE_COMM_REQ_ACTIVATE,
                             &((recvreq)->req_recv.req_base),
                             PERUSE_RECV);

    MCA_PML_BFO_RECV_REQUEST_START(recvreq);
    *request = (ompi_request_t *) recvreq;
    return OMPI_SUCCESS;
}


int mca_pml_bfo_recv(void *addr,
                     size_t count,
                     ompi_datatype_t * datatype,
                     int src,
                     int tag,
                     struct ompi_communicator_t *comm,
                     ompi_status_public_t * status)
{
    int rc;
    mca_pml_bfo_recv_request_t *recvreq;
    MCA_PML_BFO_RECV_REQUEST_ALLOC(recvreq, rc);
    if (NULL == recvreq)
        return rc;

    MCA_PML_BFO_RECV_REQUEST_INIT(recvreq,
                                   addr,
                                   count, datatype, src, tag, comm, false);

    PERUSE_TRACE_COMM_EVENT (PERUSE_COMM_REQ_ACTIVATE,
                             &((recvreq)->req_recv.req_base),
                             PERUSE_RECV);

    MCA_PML_BFO_RECV_REQUEST_START(recvreq);
    ompi_request_wait_completion(&recvreq->req_recv.req_base.req_ompi);

    if (NULL != status) {  /* return status */
        OMPI_STATUS_SET(status, &recvreq->req_recv.req_base.req_ompi.req_status);
    }
    rc = recvreq->req_recv.req_base.req_ompi.req_status.MPI_ERROR;
    ompi_request_free( (ompi_request_t**)&recvreq );
    return rc;
}


int
mca_pml_bfo_imrecv( void *buf,
                    size_t count,
                    ompi_datatype_t *datatype,
                    struct ompi_message_t **message,
                    struct ompi_request_t **request )
{
    mca_pml_bfo_recv_frag_t* frag;
    mca_pml_bfo_recv_request_t *recvreq;
    mca_pml_bfo_hdr_t *hdr;
    int src, tag;
    ompi_communicator_t *comm;
    mca_pml_bfo_comm_proc_t* proc;
    mca_pml_bfo_comm_t* bfo_comm;
    uint64_t seq;
    
    /* get the request from the message and the frag from the request
       before we overwrite everything */
    recvreq = (mca_pml_bfo_recv_request_t*) (*message)->req_ptr;
    frag = (mca_pml_bfo_recv_frag_t*) recvreq->req_recv.req_base.req_addr;
    src = recvreq->req_recv.req_base.req_ompi.req_status.MPI_SOURCE;
    tag = recvreq->req_recv.req_base.req_ompi.req_status.MPI_TAG;
    comm = (*message)->comm;
    bfo_comm = recvreq->req_recv.req_base.req_comm->c_pml_comm;
    seq = recvreq->req_recv.req_base.req_sequence;

    /* make the request a recv request again */
    recvreq->req_recv.req_base.req_type = MCA_PML_REQUEST_RECV;
    MCA_PML_BFO_RECV_REQUEST_INIT(recvreq,
                                  buf,
                                  count, datatype, 
                                  src, tag, comm, false);

    PERUSE_TRACE_COMM_EVENT (PERUSE_COMM_REQ_ACTIVATE,
                             &((recvreq)->req_recv.req_base),
                             PERUSE_RECV);

    /* init/re-init the request */
    recvreq->req_lock = 0;
    recvreq->req_pipeline_depth  = 0;
    recvreq->req_bytes_received  = 0;
    /* What about req_rdma_cnt ? */
    recvreq->req_rdma_idx = 0;
    recvreq->req_pending = false;
    recvreq->req_ack_sent = false;

    MCA_PML_BASE_RECV_START(&recvreq->req_recv.req_base);

    /* Note - sequence number already assigned */
    recvreq->req_recv.req_base.req_sequence = seq;

    proc = &bfo_comm->procs[recvreq->req_recv.req_base.req_peer];
    recvreq->req_recv.req_base.req_proc = proc->ompi_proc;
    prepare_recv_req_converter(recvreq);

    /* we can't go throught he match, since we already have the match.
       Cheat and do what REQUEST_START does, but without the frag
       search */
    hdr = (mca_pml_bfo_hdr_t*)frag->segments->seg_addr.pval;
    switch(hdr->hdr_common.hdr_type) {
    case MCA_PML_BFO_HDR_TYPE_MATCH:
        mca_pml_bfo_recv_request_progress_match(recvreq, frag->btl, frag->segments,
                                                frag->num_segments);
        break;
    case MCA_PML_BFO_HDR_TYPE_RNDV:
        mca_pml_bfo_recv_request_progress_rndv(recvreq, frag->btl, frag->segments,
                                               frag->num_segments);
        break;
    case MCA_PML_BFO_HDR_TYPE_RGET:
        mca_pml_bfo_recv_request_progress_rget(recvreq, frag->btl, frag->segments,
                                               frag->num_segments);
        break;
    default:
        assert(0);
    }
    MCA_PML_BFO_RECV_FRAG_RETURN(frag);
    
    ompi_message_return(*message);
    *message = MPI_MESSAGE_NULL;
    *request = (ompi_request_t *) recvreq;

    return OMPI_SUCCESS;
}


int
mca_pml_bfo_mrecv( void *buf,
                   size_t count,
                   ompi_datatype_t *datatype,
                   struct ompi_message_t **message,
                   ompi_status_public_t* status )
{
    mca_pml_bfo_recv_frag_t* frag;
    mca_pml_bfo_recv_request_t *recvreq;
    mca_pml_bfo_hdr_t *hdr;
    int src, tag, rc;
    ompi_communicator_t *comm;
    mca_pml_bfo_comm_proc_t* proc;
    mca_pml_bfo_comm_t* bfo_comm;
    uint64_t seq;

    /* get the request from the message and the frag from the request
       before we overwrite everything */
    comm = (*message)->comm;
    recvreq = (mca_pml_bfo_recv_request_t*) (*message)->req_ptr;
    frag = (mca_pml_bfo_recv_frag_t*) recvreq->req_recv.req_base.req_addr;
    src = recvreq->req_recv.req_base.req_ompi.req_status.MPI_SOURCE;
    tag = recvreq->req_recv.req_base.req_ompi.req_status.MPI_TAG;
    seq = recvreq->req_recv.req_base.req_sequence;
    bfo_comm = recvreq->req_recv.req_base.req_comm->c_pml_comm;

    /* make the request a recv request again */
    recvreq->req_recv.req_base.req_type = MCA_PML_REQUEST_RECV;
    MCA_PML_BFO_RECV_REQUEST_INIT(recvreq,
                                  buf,
                                  count, datatype, 
                                  src, tag, comm, false);

    PERUSE_TRACE_COMM_EVENT (PERUSE_COMM_REQ_ACTIVATE,
                             &((recvreq)->req_recv.req_base),
                             PERUSE_RECV);

    /* init/re-init the request */
    recvreq->req_lock = 0;
    recvreq->req_pipeline_depth  = 0;
    recvreq->req_bytes_received  = 0;
    recvreq->req_rdma_cnt = 0;
    recvreq->req_rdma_idx = 0;
    recvreq->req_pending = false;

    MCA_PML_BASE_RECV_START(&recvreq->req_recv.req_base);

    /* Note - sequence number already assigned */
    recvreq->req_recv.req_base.req_sequence = seq;

    proc = &bfo_comm->procs[recvreq->req_recv.req_base.req_peer];
    recvreq->req_recv.req_base.req_proc = proc->ompi_proc;
    prepare_recv_req_converter(recvreq);

    /* we can't go throught he match, since we already have the match.
       Cheat and do what REQUEST_START does, but without the frag
       search */
    hdr = (mca_pml_bfo_hdr_t*)frag->segments->seg_addr.pval;
    switch(hdr->hdr_common.hdr_type) {
    case MCA_PML_BFO_HDR_TYPE_MATCH:
        mca_pml_bfo_recv_request_progress_match(recvreq, frag->btl, frag->segments,
                                                frag->num_segments);
        break;
    case MCA_PML_BFO_HDR_TYPE_RNDV:
        mca_pml_bfo_recv_request_progress_rndv(recvreq, frag->btl, frag->segments,
                                               frag->num_segments);
        break;
    case MCA_PML_BFO_HDR_TYPE_RGET:
        mca_pml_bfo_recv_request_progress_rget(recvreq, frag->btl, frag->segments,
                                               frag->num_segments);
        break;
    default:
        assert(0);
    }
    
    ompi_message_return(*message);
    *message = MPI_MESSAGE_NULL;
    ompi_request_wait_completion(&(recvreq->req_recv.req_base.req_ompi));

    MCA_PML_BFO_RECV_FRAG_RETURN(frag);

    if (NULL != status) {  /* return status */
        *status = recvreq->req_recv.req_base.req_ompi.req_status;
    }
    rc = recvreq->req_recv.req_base.req_ompi.req_status.MPI_ERROR;
    ompi_request_free( (ompi_request_t**)&recvreq );
    return rc;
}

