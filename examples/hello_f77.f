C
C Copyright (c) 2004-2006 The Trustees of Indiana University and Indiana
C                         University Research and Technology
C                         Corporation.  All rights reserved.
C Copyright (c) 2006      Cisco Systems, Inc.  All rights reserved.
C
C Sample MPI "hello world" application in Fortran 77
C
        program main
        implicit none
        include 'mpif.h'
        integer ierr, rank, size, len
        character*(MPI_MAX_LIBRARY_VERSION_STRING) version

        call MPI_INIT(ierr)
        call MPI_COMM_RANK(MPI_COMM_WORLD, rank, ierr)
        call MPI_COMM_SIZE(MPI_COMM_WORLD, size, ierr)
        call MPI_GET_LIBRARY_VERSION(version, len, ierr)
        print *, "Hello, world, I am ", rank, " of ", size, " (", 
     &      version, ",", len, ")"
        call MPI_FINALIZE(ierr)

        end
