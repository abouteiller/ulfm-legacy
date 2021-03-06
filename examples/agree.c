/*
 * Copyright (c) 2014      The University of Tennessee and The University
 *                         of Tennessee Research Foundation.  All rights
 *                         reserved.
 *
 * $COPYRIGHT$
 * 
 * Additional copyrights may follow
 * 
 * $HEADER$
 */

#include "mpi.h"

#include <stdlib.h>
#include <stdio.h>
#include <getopt.h>
#include <sys/types.h>
#include <unistd.h>
#include <sched.h>
#include <signal.h>
#include <dlfcn.h>
#include <assert.h>

typedef struct {
    int  size;
    int *input;
    int *output;
    int *returned;
} result_t;

static void usleep_random(void)
{
    usleep( (int)(1000.0 * (double)rand() / (double)RAND_MAX) );
}

int main(int argc, char *argv[])
{
    int rank, size;
    MPI_Comm comm;
    int c, i, r, rc;
    double start, init, loop;
    int common, flag, ret;
    unsigned int seed = 1789;

    result_t *result;

    int verbose      = 0;
    int de_sync_loop = 0;
    int de_sync_init = 0;
    int check_result = 0;
    int create_comm  = 0;
    int benchmark    = 0;
    int nb_test      = 20;
    int debug        = 0;
    unsigned int nb_success = (unsigned int)-1;
    int failures     = 1;
    char *help_msg   = NULL;
    char host[256];

    int fail_this_round, success, simultaneous;
    int *victims;
    void *handle;
    int *coll_ftbasic_debug_rank_may_fail;

    start = MPI_Wtime();

    while(1) {
        static struct option long_options[] = {
            { "verbose",      0, 0, 'v' },
            { "de-sync-loop", 0, 0, 's' },
            { "de-sync-init", 0, 0, 'i' },
            { "check-result", 0, 0, 'r' },
            { "create-comm",  1, 0, 'c' },
            { "benchmark",    0, 0, 'b' },
            { "nb-test",      1, 0, 'n' },
            { "help",         0, 0, 'h' },
            { "failures",     1, 0, 'F' },
            { "successes",    1, 0, 'S' },
            { "debug",        0, 0, 'd' },
            { NULL,           0, 0, 0   }
        };

        c = getopt_long(argc, argv, "vsirc:bn:hdF:S:", long_options, NULL);
        if (c == -1)
            break;

        switch(c) {
        case 'v':
            verbose = 1;
            break;
        case 's':
            de_sync_loop = 1;
            break;
        case 'i':
            de_sync_init = 1;
            break;
        case 'r':
            check_result = 1;
            break;
        case 'c':
            create_comm = atoi(optarg);
            break;
        case 'b':
            benchmark = 1;
            break;
        case 'n':
            nb_test = atoi(optarg);
            break;
        case 'd':
            debug = 1;
            break;
        case 'F':
            failures = atoi(optarg);
            break;
        case 'S':
            nb_success = atoi(optarg);
            break;
        case 'h':
            help_msg = "";
            break;
        default:
            help_msg = "Unrecognized option\n";
            break;
        }
    }

    gethostname(host, 256);
    if( debug ) {
        int stop = 0;
        fprintf(stderr, "ssh -t %s gdb -p %d\n", host, getpid());
        while( stop == 0 ) {
            sched_yield();
        }
    }

    if( de_sync_init && check_result ) {
        help_msg = "You can't have --de-sync-init and --check-result at the same time:\n"
            "  the initial allgather for check-result will synchronize the init\n";
        de_sync_init = 0;
    }

    srand(getpid());

    if( de_sync_init ) {
        usleep_random();
    }

    comm = MPI_COMM_WORLD;

    MPI_Init(&argc, &argv);

    handle = dlopen(NULL, RTLD_LAZY|RTLD_LOCAL);
    coll_ftbasic_debug_rank_may_fail = dlsym(handle, "coll_ftbasic_debug_rank_may_fail");
    dlclose(handle);
    if( NULL == coll_ftbasic_debug_rank_may_fail ) {
        fprintf(stderr, "Could not find Open MPI internal symbol coll_ftbasic_debug_rank_may_fail. Resort to simple raise(SIGKILL) type of failure\n");
    }

    MPI_Comm_set_errhandler(MPI_COMM_WORLD,MPI_ERRORS_RETURN);

    MPI_Comm_rank(comm, &rank);
    MPI_Comm_size(comm, &size);

    if( NULL != help_msg ) {
        if( 0 == rank ) {
            fprintf(stderr, "%s", help_msg);
            fprintf(stderr, 
                    "Usage: mpirun [MPI_OPTIONS] %s [OPTIONS]\n"
                    " Where typical MPI_OPTIONS for Open MPI are:\n"
                    "   -am ft-enable-mpi                     To enable FT-extensions\n"
                    "   -mca coll ftbasic,basic,tuned,self    To select FT version of the agreement\n"
                    "   -mca coll_ftbasic_method <I>          To choose which method is used for the agreement\n"
                    "                                           (0 - 4) 3 = ETA, 4 = ERA, see ompi_info -param all all\n"
                    " And OPTIONS are:\n"
                    "\n"
                    "--verbose         | -v        To print progress messages\n"
                    "--de-sync-loop    | -s        To introduce process-specific random wait times between iterations\n"
                    "--de-sync-init    | -i        To introduce process-specific random wait time before calling MPI_Init\n"
                    "--check-result    | -r        To check the result of the consensus\n"
                    "--create-comm <I> | -c <I>    To create a new communicator every I iterations, and free the old one\n"
                    "--benchmark       | -b        To measure the times\n"
                    "--nb-test <I>     | -n <I>    To do I iterations of the agreement\n"
                    "--debug           | -d        Wait that a gdb attaches to the process and call stop=1 to continue\n"
                    "--failures <I>    | -F <I>    Introduce random process failures of at most I processes\n"
                    "--success <I>     | -S <I>    Don't introduce new failures before I agreements succeed\n"
                    "--help            | -h        To display this help\n",
                    argv[0]);
        }
        MPI_Finalize();
        return 0;
    }

    victims = (int*)malloc(size * sizeof(int));
    result = (result_t*)malloc( nb_test * sizeof(result_t) );
    for(i = 0; i < nb_test; i++) {
        result[i].input = (int*)malloc( size * sizeof(int) );
        common = rand_r(&seed);
        for(r = 0; r < size; r++) {
            result[i].input[r] = rand_r(&seed) | common;
        }
        if( check_result ) {
            result[i].output = (int*)malloc( size * sizeof(int) );
            result[i].returned = (int*)malloc( size * sizeof(int) );
        } else {
            result[i].output = NULL;
            result[i].returned = NULL;
        }
    }
    init = MPI_Wtime() - start;

    start = MPI_Wtime();
    success = nb_success;
    for(i = 0; i < nb_test; i++) {
        if( create_comm > 0 && ( (i % create_comm) == 0 ) ) {
            MPI_Comm temp;
            rc = MPI_Comm_dup(comm, &temp);
            flag = (rc == MPI_SUCCESS);
            ret = OMPI_Comm_agree(comm, &flag);
            if( ret != MPI_SUCCESS || flag == 0 ) {
                int orank = rank;
                int osize = size;

                if(verbose) {
                    printf("Communicator Creation (with DUP) failed during test. Shrinking to continue.\n");
                }
                if( rc == MPI_SUCCESS && MPI_COMM_NULL != temp ) {
                    MPI_Comm_free(&temp);
                }
                rc = OMPI_Comm_shrink(comm, &temp);
                if( rc != MPI_SUCCESS ) {
                    fprintf(stderr, "MPI_Comm_shrink returned %d instead of MPI_SUCCESS on rank %d/%d!\n", rc, rank, size);
                }
                if( temp == MPI_COMM_NULL ) {
                    fprintf(stderr, "MPI_Comm_shrink returned MPI_COMM_NULL and MPI_SUCCESS on rank %d/%d!\n", rank, size);
                }
                if( comm != MPI_COMM_WORLD )
                    MPI_Comm_free(&comm);
                comm = temp;
                MPI_Comm_rank(comm, &rank);
                MPI_Comm_size(comm, &size);
                if(verbose) {
                    printf("Rank %d/%d in previous comm is now %d/%d after shrink\n", orank, osize, rank, size);
                }
            } else {
                if( comm != MPI_COMM_WORLD )
                    MPI_Comm_free(&comm);
                comm = temp;
                MPI_Comm_rank(comm, &rank);
                MPI_Comm_size(comm, &size);
            }
        }

        if( failures > 0 && success == 0 ) {
            fail_this_round = 0;
            failures = (failures < size-1) ? failures : size-1;
            if( failures == 0 ) {
                if( verbose ) {
                    printf("Cannot kill more processes without stopping the execution. No more failures\n");
                }
            } else {
                success = nb_success;
                simultaneous = (rand_r((unsigned int*)&seed) % (failures+1));
                if( verbose && rank == 0 ) {
                    printf("Decided to do %d failures at this round\n", simultaneous);
                }
                for(c = 0; c < simultaneous; c++) {
                    do {
                        rc = rand_r((unsigned int*)&seed) % size;
                        for(i = 0; i < c; i++) {
                            if( rc == victims[i] )
                                break;
                        }
                        if( i == c ) {
                            victims[i] = rc;
                            if( rc == rank ) {
                                fail_this_round = 1;
                            }
                        }
                    } while(i != c);
                }
            }

            if( fail_this_round ) {
                if( NULL != coll_ftbasic_debug_rank_may_fail ) {
                    if( verbose ) {
                        fprintf(stderr, "Rank %d/%d will fail\n", rank, size);
                    }
                    *coll_ftbasic_debug_rank_may_fail = 1;
                } else {
                    if( verbose ) {
                        fprintf(stderr, "Rank %d/%d fails\n", rank, size);
                    }
                    raise(SIGKILL);
                }
            } else {
                if( NULL != coll_ftbasic_debug_rank_may_fail ) {
                    *coll_ftbasic_debug_rank_may_fail = 0;
                }
            }
        }

        flag = result[i].input[rank];
        if( check_result ) {
            result[i].size = size;
        }
        if( verbose ) {
            printf("Rank %d/%d enters MPI_Comm_agree %d with %08x\n", rank, size, i, flag);
        }
        if( de_sync_loop ) {
            usleep_random();
        }
        ret = OMPI_Comm_agree(comm, &flag);
        if( verbose ) {
            printf("Rank %d/%d leaves MPI_Comm_agree %d with %08x and %d\n", rank, size, i, flag, ret);
        }
        if( ret != MPI_SUCCESS ) {
            MPI_Comm temp;
            int orank = rank;
            int osize = size;
            if(verbose) {
                printf("Rank %d/%d needs to shrink after a failure\n", rank, size);
            }
            rc = OMPI_Comm_shrink(comm, &temp);
            if( rc != MPI_SUCCESS ) {
                fprintf(stderr, "MPI_Comm_shrink returned %d instead of MPI_SUCCESS on rank %d/%d!\n", rc, rank, size);
            }
            if( temp == MPI_COMM_NULL ) {
                fprintf(stderr, "MPI_Comm_shrink returned MPI_COMM_NULL and MPI_SUCCESS on rank %d/%d!\n", rank, size);
            }
            if( comm != MPI_COMM_WORLD )
                MPI_Comm_free(&comm);
            comm = temp;
            MPI_Comm_rank(comm, &rank);
            MPI_Comm_size(comm, &size);
            if(verbose) {
                printf("Rank %d/%d in previous comm is now %d/%d after shrink\n", orank, osize, rank, size);
            }
        } else {
            success--;
        }
        if( check_result ) {
            MPI_Allgather(&flag, 1, MPI_INT, result[i].output, 1, MPI_INT, comm);
            MPI_Allgather(&ret, 1, MPI_INT, result[i].returned, 1, MPI_INT, comm);
        }
    }
    loop = MPI_Wtime() - start;

    if( benchmark && ( rank == 0 ) ) {
        printf("### Time: %g (s) NB Agreements: %d Time / Agreement: %g (s) Interval Comm Dup: %d Check Result: %s DeSyncLoop: %s DeSyncInit: %s Init Time: %g (s)\n",
               loop, nb_test, loop / (double)nb_test, create_comm,
               check_result ? "Yes" : "No",
               de_sync_loop ? "Yes" : "No",
               de_sync_init ? "Yes" : "No",
               init);
    }
    
    if( check_result && (rank == 0) ) {
        int errors = 0;

        for(i = 0; i < nb_test; i++) {
            int vl = ~0, ret;
            for(r = 0; r < result[i].size; r++) {
                vl &= result[i].input[r];
            }
            ret = result[i].returned[0];
            for(r = 0; r < result[i].size; r++) {
                if( result[i].output[r] != vl ) {
                    printf("!!! Consensus %d, rank %d obtained output %08x, expected %08x\n",
                           i, r, result[i].output[r], vl);
                    errors++;
                }
                if( result[i].returned[r] != ret ) {
                    printf("!!! Consensus %d, rank %d returned %d while rank 0 returned %d\n",
                           i, r, result[i].returned[r], r);
                    errors++;
                }
            }
        }
        printf("There was %d errors\n", errors);
    }

    if( MPI_COMM_WORLD != comm ) {
        MPI_Comm_free(&comm);
    }

    MPI_Finalize();

    return 0;
}
