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
 * $COPYRIGHT$
 * 
 * Additional copyrights may follow
 * 
 * $HEADER$
 */

#include "ompi_config.h"

#include "ompi/mpi/f77/bindings.h"
#include "ompi/mpi/f77/constants.h"
#include "ompi/communicator/communicator.h"

#if OPAL_HAVE_WEAK_SYMBOLS && OMPI_PROFILE_LAYER
#pragma weak PMPI_MRECV = mpi_mrecv_f
#pragma weak pmpi_mrecv = mpi_mrecv_f
#pragma weak pmpi_mrecv_ = mpi_mrecv_f
#pragma weak pmpi_mrecv__ = mpi_mrecv_f
#elif OMPI_PROFILE_LAYER
OMPI_GENERATE_F77_BINDINGS (PMPI_MRECV,
                            pmpi_mrecv,
                            pmpi_mrecv_,
                            pmpi_mrecv__,
                            pmpi_mrecv_f,
                            (char *buf, MPI_Fint *count, MPI_Fint *datatype, MPI_Fint *message,
                             MPI_Fint *status, MPI_Fint *ierr),
                            (buf, count, datatype, message, status, ierr) )
#endif

#if OPAL_HAVE_WEAK_SYMBOLS
#pragma weak MPI_MRECV = mpi_mrecv_f
#pragma weak mpi_mrecv = mpi_mrecv_f
#pragma weak mpi_mrecv_ = mpi_mrecv_f
#pragma weak mpi_mrecv__ = mpi_mrecv_f
#endif

#if ! OPAL_HAVE_WEAK_SYMBOLS && ! OMPI_PROFILE_LAYER
OMPI_GENERATE_F77_BINDINGS (MPI_MRECV,
                            mpi_mrecv,
                            mpi_mrecv_,
                            mpi_mrecv__,
                            mpi_mrecv_f,
                            (char *buf, MPI_Fint *count, MPI_Fint *datatype,
                             MPI_Fint *message, MPI_Fint *status, MPI_Fint *ierr),
                            (buf, count, datatype, message, status, ierr) )
#endif


#if OMPI_PROFILE_LAYER && ! OPAL_HAVE_WEAK_SYMBOLS
#include "ompi/mpi/f77/profile/defines.h"
#endif

void mpi_mrecv_f(char *buf, MPI_Fint *count, MPI_Fint *datatype, 
                 MPI_Fint *message, MPI_Fint *status, MPI_Fint *ierr)
{
   MPI_Status *c_status;
#if OMPI_SIZEOF_FORTRAN_INTEGER != SIZEOF_INT
   MPI_Status c_status2;
#endif
   MPI_Message c_message = MPI_Message_f2c(*message);
   MPI_Datatype c_type = MPI_Type_f2c(*datatype);

   /* See if we got MPI_STATUS_IGNORE */
   if (OMPI_IS_FORTRAN_STATUS_IGNORE(status)) {
      c_status = MPI_STATUS_IGNORE;
   } else {
      /* If sizeof(int) == sizeof(INTEGER), then there's no
         translation necessary -- let the underlying functions write
         directly into the Fortran status */

#if OMPI_SIZEOF_FORTRAN_INTEGER == SIZEOF_INT
      c_status = (MPI_Status *) status;
#else
      c_status = &c_status2;
#endif
   }

   /* Call the C function */
   *ierr = OMPI_INT_2_FINT(MPI_Mrecv(OMPI_F2C_BOTTOM(buf), OMPI_FINT_2_INT(*count),
                                     c_type, &c_message,
                                     c_status));
   if (MPI_SUCCESS == OMPI_FINT_2_INT(*ierr)) {
#if OMPI_SIZEOF_FORTRAN_INTEGER != SIZEOF_INT
       if (MPI_STATUS_IGNORE != c_status) {
           MPI_Status_c2f(c_status, status);
       }
#endif
      /* message is an INOUT, and may be updated by the recv */
      *message = MPI_Message_c2f(c_message);
   }
}
