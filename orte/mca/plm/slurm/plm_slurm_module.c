/*
 * Copyright (c) 2004-2007 The Trustees of Indiana University and Indiana
 *                         University Research and Technology
 *                         Corporation.  All rights reserved.
 * Copyright (c) 2004-2006 The University of Tennessee and The University
 *                         of Tennessee Research Foundation.  All rights
 *                         reserved.
 * Copyright (c) 2004-2005 High Performance Computing Center Stuttgart,
 *                         University of Stuttgart.  All rights reserved.
 * Copyright (c) 2004-2005 The Regents of the University of California.
 *                         All rights reserved.
 * Copyright (c) 2006-2007 Cisco Systems, Inc.  All rights reserved.
 * Copyright (c) 2007-2011 Los Alamos National Security, LLC.  All rights
 *                         reserved. 
 * $COPYRIGHT$
 *
 * Additional copyrights may follow
 *
 * $HEADER$
 *
 * These symbols are in a file by themselves to provide nice linker
 * semantics.  Since linkers generally pull in symbols by object
 * files, keeping these symbols as the only symbols in this file
 * prevents utility programs such as "ompi_info" from having to import
 * entire components just to query their version and parameters.
 */

#include "orte_config.h"

#ifdef HAVE_STRING_H
#include <string.h>
#endif
#include <sys/types.h>
#ifdef HAVE_UNISTD_H
#include <unistd.h>
#endif
#include <signal.h>
#ifdef HAVE_STDLIB_H
#include <stdlib.h>
#endif
#ifdef HAVE_SYS_TYPES_H
#include <sys/types.h>
#endif
#ifdef HAVE_SYS_TIME_H
#include <sys/time.h>
#endif
#ifdef HAVE_SYS_STAT_H
#include <sys/stat.h>
#endif
#ifdef HAVE_FCNTL_H
#include <fcntl.h>
#endif

#include "opal/mca/installdirs/installdirs.h"
#include "opal/util/argv.h"
#include "opal/util/output.h"
#include "opal/util/opal_environ.h"
#include "opal/util/path.h"
#include "opal/util/basename.h"
#include "opal/mca/base/mca_base_param.h"

#include "orte/constants.h"
#include "orte/types.h"
#include "orte/util/show_help.h"
#include "orte/util/name_fns.h"
#include "orte/runtime/orte_globals.h"
#include "orte/runtime/orte_wait.h"
#include "orte/runtime/orte_quit.h"
#include "orte/mca/errmgr/errmgr.h"
#include "orte/mca/rmaps/rmaps.h"

#include "orte/orted/orted.h"

#include "orte/mca/plm/plm.h"
#include "orte/mca/plm/base/plm_private.h"
#include "plm_slurm.h"


/*
 * Local functions
 */
static int plm_slurm_init(void);
static int plm_slurm_launch_job(orte_job_t *jdata);
static int plm_slurm_terminate_orteds(void);
static int plm_slurm_signal_job(orte_jobid_t jobid, int32_t signal);
static int plm_slurm_finalize(void);

static int plm_slurm_start_proc(int argc, char **argv, char **env,
                                char *prefix);


/*
 * Global variable
 */
orte_plm_base_module_1_0_0_t orte_plm_slurm_module = {
    plm_slurm_init,
    orte_plm_base_set_hnp_name,
    plm_slurm_launch_job,
    NULL,
    orte_plm_base_orted_terminate_job,
    plm_slurm_terminate_orteds,
    orte_plm_base_orted_kill_local_procs,
    plm_slurm_signal_job,
    plm_slurm_finalize
};

/*
 * Local variables
 */
static pid_t primary_srun_pid = 0;
static bool primary_pid_set = false;
static bool launching_daemons;

/**
* Init the module
 */
static int plm_slurm_init(void)
{
    int rc;
    
    if (ORTE_SUCCESS != (rc = orte_plm_base_comm_start())) {
        ORTE_ERROR_LOG(rc);
    }
    
    /* if we don't want to launch (e.g., someone just wants
     * to test the mappers), then we assign vpids at "launch"
     * so the mapper has something to work with
     */
    if (orte_do_not_launch) {
        orte_plm_globals.daemon_nodes_assigned_at_launch = true;
    } else {
        /* we do NOT assign daemons to nodes at launch - we will
         * determine that mapping when the daemon
         * calls back. This is required because slurm does
         * its own mapping of proc-to-node, and we cannot know
         * in advance which daemon will wind up on which node
         */
        orte_plm_globals.daemon_nodes_assigned_at_launch = false;
    }

    return rc;
}

/* When working in this function, ALWAYS jump to "cleanup" if
 * you encounter an error so that orterun will be woken up and
 * the job can cleanly terminate
 */
static int plm_slurm_launch_job(orte_job_t *jdata)
{
    orte_app_context_t *app;
    orte_node_t *node;
    orte_std_cntr_t n;
    orte_job_map_t *map;
    char *jobid_string = NULL;
    char *param;
    char **argv = NULL;
    int argc;
    int rc;
    char *tmp;
    char** env = NULL;
    char *nodelist_flat;
    char **nodelist_argv;
    char *name_string;
    char **custom_strings;
    int num_args, i;
    char *cur_prefix;
    struct timeval launchstart, launchstop;
    int proc_vpid_index;
    orte_jobid_t failed_job;
    bool failed_launch=true;
    orte_job_t *daemons;

    /* if we are launching debugger daemons, then just go
     * do it - no new daemons will be launched
     */
    if (ORTE_JOB_CONTROL_DEBUGGER_DAEMON & jdata->controls) {
        failed_job = jdata->jobid;
        if (ORTE_SUCCESS != (rc = orte_plm_base_launch_apps(jdata->jobid))) {
            ORTE_ERROR_LOG(rc);
            failed_launch = true;
        } else {
            failed_launch = false;
        }
        goto cleanup;
    }

    /* if we are timing, record the start time */
    if (orte_timing) {
        gettimeofday(&orte_plm_globals.daemonlaunchstart, NULL);
    }
    
     /* flag the daemons as failing by default */
    failed_job = ORTE_PROC_MY_NAME->jobid;
    
    if (orte_timing) {
        if (0 != gettimeofday(&launchstart, NULL)) {
            opal_output(0, "plm_slurm: could not obtain job start time");
            launchstart.tv_sec = 0;
            launchstart.tv_usec = 0;
        }        
    }
    
    /* if we don't want to launch, then don't attempt to
     * launch the daemons - the user really wants to just
     * look at the proposed process map
     */
    if (orte_do_not_launch) {
        goto launch_apps;
    }

    /* start by setting up the virtual machine */
    daemons = orte_get_job_data_object(ORTE_PROC_MY_NAME->jobid);
    if (ORTE_SUCCESS != (rc = orte_plm_base_setup_virtual_machine(jdata))) {
        ORTE_ERROR_LOG(rc);
        goto cleanup;
    }

    /* indicate the state of the launch */
    launching_daemons = true;
    
    OPAL_OUTPUT_VERBOSE((1, orte_plm_globals.output,
                         "%s plm:slurm: launching vm",
                         ORTE_NAME_PRINT(ORTE_PROC_MY_NAME)));
    
    
    /* set the active jobid */
    failed_job = daemons->jobid;
    
    /* Get the map for this job */
    if (NULL == (map = daemons->map)) {
        ORTE_ERROR_LOG(ORTE_ERR_NOT_FOUND);
        rc = ORTE_ERR_NOT_FOUND;
        goto cleanup;
    }
        
    if (0 == map->num_new_daemons) {
        /* no new daemons required - just launch apps */
        OPAL_OUTPUT_VERBOSE((1, orte_plm_globals.output,
                             "%s plm:slurm: no new daemons to launch",
                             ORTE_NAME_PRINT(ORTE_PROC_MY_NAME)));
        goto launch_apps;
    }

    /* need integer value for command line parameter */
    asprintf(&jobid_string, "%lu", (unsigned long) daemons->jobid);

    /*
     * start building argv array
     */
    argv = NULL;
    argc = 0;

    /*
     * SLURM srun OPTIONS
     */

    /* add the srun command */
    opal_argv_append(&argc, &argv, "srun");

    /* alert us if any orteds die during startup */
    opal_argv_append(&argc, &argv, "--kill-on-bad-exit");

    /* Append user defined arguments to srun */
    if ( NULL != mca_plm_slurm_component.custom_args ) {
        custom_strings = opal_argv_split(mca_plm_slurm_component.custom_args, ' ');
        num_args       = opal_argv_count(custom_strings);
        for (i = 0; i < num_args; ++i) {
            opal_argv_append(&argc, &argv, custom_strings[i]);
        }
        opal_argv_free(custom_strings);
    }

    /* create nodelist */
    nodelist_argv = NULL;

    for (n=0; n < map->nodes->size; n++ ) {
        if (NULL == (node = (orte_node_t*)opal_pointer_array_get_item(map->nodes, n))) {
            continue;
        }
        /* if the daemon already exists on this node, then
         * don't include it
         */
        if (node->daemon_launched) {
            continue;
        }
        
        /* otherwise, add it to the list of nodes upon which
         * we need to launch a daemon
         */
        opal_argv_append_nosize(&nodelist_argv, node->name);
    }
    if (0 == opal_argv_count(nodelist_argv)) {
        orte_show_help("help-plm-slurm.txt", "no-hosts-in-list", true);
        rc = ORTE_ERR_FAILED_TO_START;
        goto cleanup;
    }
    nodelist_flat = opal_argv_join(nodelist_argv, ',');
    opal_argv_free(nodelist_argv);

    /* if we are using all allocated nodes, then srun doesn't
     * require any further arguments
     */
    if (map->num_new_daemons < orte_num_allocated_nodes) {
        asprintf(&tmp, "--nodes=%lu", (unsigned long)map->num_new_daemons);
        opal_argv_append(&argc, &argv, tmp);
        free(tmp);

        asprintf(&tmp, "--nodelist=%s", nodelist_flat);
        opal_argv_append(&argc, &argv, tmp);
        free(tmp);
    }

    /* tell srun how many tasks to run */
    asprintf(&tmp, "--ntasks=%lu", (unsigned long)map->num_new_daemons);
    opal_argv_append(&argc, &argv, tmp);
    free(tmp);

    OPAL_OUTPUT_VERBOSE((2, orte_plm_globals.output,
                         "%s plm:slurm: launching on nodes %s",
                         ORTE_NAME_PRINT(ORTE_PROC_MY_NAME), nodelist_flat));
    
    /*
     * ORTED OPTIONS
     */

    /* add the daemon command (as specified by user) */
    orte_plm_base_setup_orted_cmd(&argc, &argv);
    
    /* Add basic orted command line options, including debug flags */
    orte_plm_base_orted_append_basic_args(&argc, &argv,
                                          NULL, &proc_vpid_index,
                                          nodelist_flat);
    free(nodelist_flat);

    /* tell the new daemons the base of the name list so they can compute
     * their own name on the other end
     */
    rc = orte_util_convert_vpid_to_string(&name_string, map->daemon_vpid_start);
    if (ORTE_SUCCESS != rc) {
        opal_output(0, "plm_slurm: unable to get daemon vpid as string");
        goto cleanup;
    }

    free(argv[proc_vpid_index]);
    argv[proc_vpid_index] = strdup(name_string);
    free(name_string);

    /* Copy the prefix-directory specified in the
       corresponding app_context.  If there are multiple,
       different prefix's in the app context, complain (i.e., only
       allow one --prefix option for the entire slurm run -- we
       don't support different --prefix'es for different nodes in
       the SLURM plm) */
    cur_prefix = NULL;
    for (n=0; n < jdata->apps->size; n++) {
        char * app_prefix_dir;
        if (NULL == (app = (orte_app_context_t*)opal_pointer_array_get_item(jdata->apps, n))) {
            continue;
        }
        app_prefix_dir = app->prefix_dir;
        /* Check for already set cur_prefix_dir -- if different,
           complain */
        if (NULL != app_prefix_dir) {
            if (NULL != cur_prefix &&
                0 != strcmp (cur_prefix, app_prefix_dir)) {
                orte_show_help("help-plm-slurm.txt", "multiple-prefixes",
                               true, cur_prefix, app_prefix_dir);
                return ORTE_ERR_FATAL;
            }

            /* If not yet set, copy it; iff set, then it's the
             * same anyway
             */
            if (NULL == cur_prefix) {
                cur_prefix = strdup(app_prefix_dir);
                OPAL_OUTPUT_VERBOSE((1, orte_plm_globals.output,
                                     "%s plm:slurm: Set prefix:%s",
                                     ORTE_NAME_PRINT(ORTE_PROC_MY_NAME),
                                     cur_prefix));
            }
        }
    }

    /* setup environment */
    env = opal_argv_copy(orte_launch_environ);

    if (0 < opal_output_get_verbosity(orte_plm_globals.output)) {
        param = opal_argv_join(argv, ' ');
        OPAL_OUTPUT_VERBOSE((1, orte_plm_globals.output,
                             "%s plm:slurm: final top-level argv:\n\t%s",
                             ORTE_NAME_PRINT(ORTE_PROC_MY_NAME),
                             (NULL == param) ? "NULL" : param));
        if (NULL != param) free(param);
    }
    
    /* exec the daemon(s) */
    if (ORTE_SUCCESS != (rc = plm_slurm_start_proc(argc, argv, env, cur_prefix))) {
        ORTE_ERROR_LOG(rc);
        goto cleanup;
    }
    
    /* do NOT wait for srun to complete. Srun only completes when the processes
     * it starts - in this case, the orteds - complete. Instead, we'll catch
     * any srun failures and deal with them elsewhere
     */
    
    /* wait for daemons to callback */
    if (ORTE_SUCCESS != (rc = orte_plm_base_daemon_callback(map->num_new_daemons))) {
        OPAL_OUTPUT_VERBOSE((1, orte_plm_globals.output,
                             "%s plm:slurm: daemon launch failed on error %s",
                             ORTE_NAME_PRINT(ORTE_PROC_MY_NAME),
                             ORTE_ERROR_NAME(rc)));
        goto cleanup;
    }
    
 launch_apps:
    /* get here if daemons launch okay - any failures now by apps */
    launching_daemons = false;
    /* setup the job */
    if (ORTE_SUCCESS != (rc = orte_plm_base_setup_job(jdata))) {
        ORTE_ERROR_LOG(rc);
        failed_job = jdata->jobid;
        goto cleanup;
    }
    failed_job = jdata->jobid;

    if (ORTE_SUCCESS != (rc = orte_plm_base_launch_apps(jdata->jobid))) {
        OPAL_OUTPUT_VERBOSE((1, orte_plm_globals.output,
                             "%s plm:slurm: launch of apps failed for job %s on error %s",
                             ORTE_NAME_PRINT(ORTE_PROC_MY_NAME),
                             ORTE_JOBID_PRINT(jdata->jobid), ORTE_ERROR_NAME(rc)));
        goto cleanup;
    }

    /* declare the launch a success */
    failed_launch = false;
    
    if (orte_timing) {
        if (0 != gettimeofday(&launchstop, NULL)) {
            opal_output(0, "plm_slurm: could not obtain stop time");
        } else {
            opal_output(0, "plm_slurm: total job launch time is %ld usec",
                        (launchstop.tv_sec - launchstart.tv_sec)*1000000 + 
                        (launchstop.tv_usec - launchstart.tv_usec));
        }
    }

    if (ORTE_SUCCESS != rc) {
        opal_output(0, "plm:slurm: start_procs returned error %d", rc);
        goto cleanup;
    }

 cleanup:
    if (NULL != argv) {
        opal_argv_free(argv);
    }
    if (NULL != env) {
        opal_argv_free(env);
    }
    
    if(NULL != jobid_string) {
        free(jobid_string);
    }
    
    /* check for failed launch - if so, force terminate */
    if (failed_launch) {
        if (ORTE_ERR_SILENT == rc) {
            orte_errmgr.update_state(failed_job, ORTE_JOB_STATE_SILENT_ABORT,
                                     NULL, ORTE_PROC_STATE_UNDEF,
                                     0, ORTE_ERROR_DEFAULT_EXIT_CODE);
        } else {
            orte_errmgr.update_state(failed_job, ORTE_JOB_STATE_FAILED_TO_START,
                                     NULL, ORTE_PROC_STATE_UNDEF,
                                     0, ORTE_ERROR_DEFAULT_EXIT_CODE);
        }
    }
    
    return rc;
}


/**
* Terminate the orteds for a given job
 */
static int plm_slurm_terminate_orteds(void)
{
    int rc;
    orte_job_t *jdata;
    
    /* tell them to die without sending a reply - we will rely on the
     * waitpid to tell us when they have exited!
     */
    if (ORTE_SUCCESS != (rc = orte_plm_base_orted_exit(ORTE_DAEMON_HALT_VM_CMD))) {
        ORTE_ERROR_LOG(rc);
    }
    
    /* check to see if the primary pid is set. If not, this indicates
     * that we never launched any additional daemons, so we cannot
     * not wait for a waitpid to fire and tell us it's okay to
     * exit. Instead, we simply trigger an exit for ourselves
     */
    if (!primary_pid_set) {
        OPAL_OUTPUT_VERBOSE((1, orte_plm_globals.output,
                             "%s plm:slurm: primary daemons complete!",
                             ORTE_NAME_PRINT(ORTE_PROC_MY_NAME)));
        jdata = orte_get_job_data_object(ORTE_PROC_MY_NAME->jobid);
        jdata->state = ORTE_JOB_STATE_TERMINATED;
        /* need to set the #terminated value to avoid an incorrect error msg */
        jdata->num_terminated = jdata->num_procs;
        orte_quit();
    }
    
    return rc;
}


/**
 * Signal all the processes in the child srun by sending the signal directly to it
 */
static int plm_slurm_signal_job(orte_jobid_t jobid, int32_t signal)
{
    int rc = ORTE_SUCCESS;

    /* order them to pass this signal to their local procs */
    if (ORTE_SUCCESS != (rc = orte_plm_base_orted_signal_local_procs(jobid, signal))) {
        ORTE_ERROR_LOG(rc);
    }

    return rc;
}


static int plm_slurm_finalize(void)
{
    int rc;
    
    /* cleanup any pending recvs */
    if (ORTE_SUCCESS != (rc = orte_plm_base_comm_stop())) {
        ORTE_ERROR_LOG(rc);
    }
    
    return ORTE_SUCCESS;
}


static void srun_wait_cb(pid_t pid, int status, void* cbdata){
    orte_job_t *jdata;
    
    /* According to the SLURM folks, srun always returns the highest exit
     code of our remote processes. Thus, a non-zero exit status doesn't
     necessarily mean that srun failed - it could be that an orted returned
     a non-zero exit status. Of course, that means the orted failed(!), so
     the end result is the same - the job didn't start.
     
     As a result, we really can't do much with the exit status itself - it
     could be something in errno (if srun itself failed), or it could be
     something returned by an orted, or it could be something returned by
     the OS (e.g., couldn't find the orted binary). Somebody is welcome
     to sort out all the options and pretty-print a better error message. For
     now, though, the only thing that really matters is that
     srun failed. Report the error and make sure that orterun
     wakes up - otherwise, do nothing!
     
     Unfortunately, the pid returned here is the srun pid, not the pid of
     the proc that actually died! So, to avoid confusion, just use -1 as the
     pid so nobody thinks this is real
     */
    
    /* if we are in the launch phase, then any termination is bad */
    if (launching_daemons) {
        /* report that one or more daemons failed to launch so we can exit */
        OPAL_OUTPUT_VERBOSE((1, orte_plm_globals.output,
                             "%s plm:slurm: daemon failed during launch",
                             ORTE_NAME_PRINT(ORTE_PROC_MY_NAME)));
        orte_errmgr.update_state(ORTE_PROC_MY_NAME->jobid, ORTE_JOB_STATE_FAILED_TO_START,
                                 NULL, ORTE_PROC_STATE_UNDEF, 0, status);
    } else {
        /* if this is after launch, then we need to abort only if the status
         * returned is non-zero - i.e., if the orteds exited with an error
         */
        if (0 != status) {
            /* an orted must have died unexpectedly after launch - report
             * that the daemon has failed so we exit
             */
            OPAL_OUTPUT_VERBOSE((1, orte_plm_globals.output,
                                 "%s plm:slurm: daemon failed while running",
                                 ORTE_NAME_PRINT(ORTE_PROC_MY_NAME)));
            orte_errmgr.update_state(ORTE_PROC_MY_NAME->jobid, ORTE_JOB_STATE_ABORTED,
                                     NULL, ORTE_PROC_STATE_UNDEF, 0, status);
        }
        /* otherwise, check to see if this is the primary pid */
        if (primary_srun_pid == pid) {
            /* in this case, we just want to fire the proper trigger so
             * mpirun can exit
             */
            OPAL_OUTPUT_VERBOSE((1, orte_plm_globals.output,
                                 "%s plm:slurm: primary daemons complete!",
                                 ORTE_NAME_PRINT(ORTE_PROC_MY_NAME)));
            jdata = orte_get_job_data_object(ORTE_PROC_MY_NAME->jobid);
            jdata->state = ORTE_JOB_STATE_TERMINATED;
            /* need to set the #terminated value to avoid an incorrect error msg */
            jdata->num_terminated = jdata->num_procs;
            orte_quit();
        }
    }
}


static int plm_slurm_start_proc(int argc, char **argv, char **env,
                                char *prefix)
{
    int fd;
    int srun_pid;
    char *exec_argv = opal_path_findv(argv[0], 0, env, NULL);

    if (NULL == exec_argv) {
        return ORTE_ERR_NOT_FOUND;
    }

    srun_pid = fork();
    if (-1 == srun_pid) {
        ORTE_ERROR_LOG(ORTE_ERR_SYS_LIMITS_CHILDREN);
        free(exec_argv);
        return ORTE_ERR_SYS_LIMITS_CHILDREN;
    }
    
    if (0 == srun_pid) {  /* child */
        char *bin_base = NULL, *lib_base = NULL;

        /* Figure out the basenames for the libdir and bindir.  There
           is a lengthy comment about this in plm_rsh_module.c
           explaining all the rationale for how / why we're doing
           this. */

        lib_base = opal_basename(opal_install_dirs.libdir);
        bin_base = opal_basename(opal_install_dirs.bindir);

        /* If we have a prefix, then modify the PATH and
           LD_LIBRARY_PATH environment variables.  */
        if (NULL != prefix) {
            char *oldenv, *newenv;

            /* Reset PATH */
            oldenv = getenv("PATH");
            if (NULL != oldenv) {
                asprintf(&newenv, "%s/%s:%s", prefix, bin_base, oldenv);
            } else {
                asprintf(&newenv, "%s/%s", prefix, bin_base);
            }
            opal_setenv("PATH", newenv, true, &env);
            OPAL_OUTPUT_VERBOSE((1, orte_plm_globals.output,
                                 "%s plm:slurm: reset PATH: %s",
                                 ORTE_NAME_PRINT(ORTE_PROC_MY_NAME),
                                 newenv));
            free(newenv);

            /* Reset LD_LIBRARY_PATH */
            oldenv = getenv("LD_LIBRARY_PATH");
            if (NULL != oldenv) {
                asprintf(&newenv, "%s/%s:%s", prefix, lib_base, oldenv);
            } else {
                asprintf(&newenv, "%s/%s", prefix, lib_base);
            }
            opal_setenv("LD_LIBRARY_PATH", newenv, true, &env);
            OPAL_OUTPUT_VERBOSE((1, orte_plm_globals.output,
                                 "%s plm:slurm: reset LD_LIBRARY_PATH: %s",
                                 ORTE_NAME_PRINT(ORTE_PROC_MY_NAME),
                                 newenv));
            free(newenv);
        }

        fd = open("/dev/null", O_CREAT|O_WRONLY|O_TRUNC, 0666);
        if(fd > 0) {
            dup2(fd, 0);
        }

        /* When not in debug mode and --debug-daemons was not passed,
         * tie stdout/stderr to dev null so we don't see messages from orted
         * EXCEPT if the user has requested that we leave sessions attached
         */
        if (0 >= opal_output_get_verbosity(orte_plm_globals.output) &&
            !orte_debug_daemons_flag && !orte_leave_session_attached) {
            if (fd >= 0) {
                if (fd != 1) {
                    dup2(fd,1);
                }
                if (fd != 2) {
                    dup2(fd,2);
                }
            }
        }

        if (fd > 2) {
            close(fd);
        }

        /* get the srun process out of orterun's process group so that
           signals sent from the shell (like those resulting from
           cntl-c) don't get sent to srun */
        setpgid(0, 0);

        execve(exec_argv, argv, env);

        opal_output(0, "plm:slurm:start_proc: exec failed");
        /* don't return - need to exit - returning would be bad -
           we're not in the calling process anymore */
        exit(1);
    } else {  /* parent */
        /* just in case, make sure that the srun process is not in our
        process group any more.  Stevens says always do this on both
        sides of the fork... */
        setpgid(srun_pid, srun_pid);
        
        /* if this is the primary launch - i.e., not a comm_spawn of a
         * child job - then save the pid
         */
        if (!primary_pid_set) {
            primary_srun_pid = srun_pid;
            primary_pid_set = true;
        }
        
        /* setup the waitpid so we can find out if srun succeeds! */
        orte_wait_cb(srun_pid, srun_wait_cb, NULL);
        free(exec_argv);
    }

    return ORTE_SUCCESS;
}
