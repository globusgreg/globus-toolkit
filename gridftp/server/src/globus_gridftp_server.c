
#include "globus_xio.h"
#include "globus_xio_tcp_driver.h"
#include "globus_i_gridftp_server.h"
#include "version.h"

#include <sys/wait.h>

static globus_cond_t                    globus_l_gfs_cond;
static globus_mutex_t                   globus_l_gfs_mutex;
static globus_bool_t                    globus_l_gfs_terminated = GLOBUS_FALSE;
static int                              globus_l_gfs_open_count = 0;
static globus_xio_driver_t              globus_l_gfs_tcp_driver = GLOBUS_NULL;
static globus_xio_server_t              globus_l_gfs_xio_server = GLOBUS_NULL;
static globus_bool_t                    globus_l_gfs_xio_server_accepting;
static globus_xio_attr_t                globus_l_gfs_xio_attr;
static globus_bool_t                    globus_l_gfs_exit = GLOBUS_FALSE;

static
void
globus_l_gfs_check_log_and_die(
    globus_result_t                     result);
    
static
void
globus_l_gfs_terminate_server(
    globus_bool_t                       immediately);

static
globus_result_t
globus_l_gfs_open_new_server(
    globus_xio_handle_t                 handle);

static
void
globus_l_gfs_server_closed(
    void *                              user_arg);

static
void
globus_l_gfs_bad_signal_handler(
    int                                 signum)
{
    globus_i_gfs_log_message(
        GLOBUS_I_GFS_LOG_ERR, 
        "an unexpected signal occured: %d\n", 
        signum);
    if(!globus_l_gfs_exit)
    {
        signal(signum, SIG_DFL);
        raise(signum);
    }
    else
    {
        exit(signum);
    }
}


static
void 
globus_l_gfs_sigint(
    void *                              user_arg)
{
    
    globus_mutex_lock(&globus_l_gfs_mutex);
    {
        globus_l_gfs_open_count = 0;
    }
    globus_mutex_unlock(&globus_l_gfs_mutex);
    globus_l_gfs_terminate_server(GLOBUS_TRUE);
    globus_i_gfs_log_message(
        GLOBUS_I_GFS_LOG_ERR, 
        "Server is exiting...\n");         
}

static
void 
globus_l_gfs_sighup(
    void *                              user_arg)
{
    int                                 argc;
    char **                             argv;

    globus_i_gfs_log_message(
        GLOBUS_I_GFS_LOG_INFO, 
        "Reloading config...\n");         
    
    argv = (char **) globus_i_gfs_config_get("argv");
    argc = globus_i_gfs_config_int("argc");
    
    globus_i_gfs_config_init(argc, argv);
    globus_i_gfs_log_message(
        GLOBUS_I_GFS_LOG_INFO, 
        "Done reloading config.\n");           
}

/* now have an open channel (when we get here, we hand off to the
 * control or data server code)
 * XXX all thats left for process management is to setuid iff this is an inetd
 * instance (or spawned from this daemon code)
 */
static
void 
globus_l_gfs_sigchld(
    void *                              user_arg)
{
    int                                 child_pid;
    int                                 child_status;
    int                                 child_rc;

    child_pid = waitpid(-1, &child_status, WNOHANG);

    if(child_pid < 0)
    {
        globus_i_gfs_log_message(
            GLOBUS_I_GFS_LOG_ERR, 
            "SIGCHLD handled but waitpid has error: %d\n", 
            errno);
    }    
    if(WIFEXITED(child_status))
    {
        child_rc = WEXITSTATUS(child_status);
        globus_i_gfs_log_message(
            GLOBUS_I_GFS_LOG_INFO, 
            "Child process %d ended with rc = %d\n", 
            child_pid, 
            child_rc);
    }
    else if(WIFSIGNALED(child_status))
    {
        globus_i_gfs_log_message(
            GLOBUS_I_GFS_LOG_INFO, 
            "Child process %d killed by signal %d\n",
            child_pid, 
            WTERMSIG(child_rc));
    }

    globus_mutex_lock(&globus_l_gfs_mutex);
    {
        globus_l_gfs_open_count--;
    }
    globus_mutex_unlock(&globus_l_gfs_mutex);        
}

static
void
globus_l_gfs_signal_init()
{
    
#   ifdef SIGINT
    globus_callback_register_signal_handler(
        SIGINT,
        GLOBUS_TRUE,
        globus_l_gfs_sigint,
        NULL);
#   endif

#   ifdef SIGHUP
    globus_callback_register_signal_handler(
        SIGHUP,
        GLOBUS_TRUE,
        globus_l_gfs_sighup,
        NULL);
#   endif

#   ifdef SIGKILL
    {
        //signal(SIGKILL, globus_l_gfs_bad_signal_handler);
    }
#   endif
#   ifdef SIGSEGV
    {
        signal(SIGSEGV, globus_l_gfs_bad_signal_handler);
    }
#   endif
#   ifdef SIGABRT
    {
        signal(SIGABRT, globus_l_gfs_bad_signal_handler);
    }
#   endif
#   ifdef SIGBUS
    {
        signal(SIGBUS, globus_l_gfs_bad_signal_handler);
    }
#   endif
#   ifdef SIGFPE
    {
        signal(SIGFPE, globus_l_gfs_bad_signal_handler);
    }
#   endif
#   ifdef SIGILL
    {
        signal(SIGILL, globus_l_gfs_bad_signal_handler);
    }
#   endif
#   ifdef SIGIOT
    {
        signal(SIGIOT, globus_l_gfs_bad_signal_handler);
    }
#   endif
#   ifdef SIGPIPE
    {
        signal(SIGPIPE, globus_l_gfs_bad_signal_handler);
    }
#   endif
#   ifdef SIGEMT
    {
        signal(SIGEMT, globus_l_gfs_bad_signal_handler);
    }

#   endif
#   ifdef SIGSYS
    {
        signal(SIGSYS, globus_l_gfs_bad_signal_handler);
    }

#   endif
#   ifdef SIGTRAP
    {
        signal(SIGTRAP, globus_l_gfs_bad_signal_handler);
    }

#   endif
#   ifdef SIGSTOP
    {
        //signal(SIGSTOP, globus_l_gfs_bad_signal_handler);
    }

#   endif
}


static
globus_result_t
globus_l_gfs_spawn_child(
    globus_xio_handle_t                 handle)
{
    char **                             new_argv;
    char **                             prog_argv;
    globus_result_t                     result;
    pid_t                               child_pid;
    globus_xio_system_handle_t          socket_handle;
    int                                 i;
    int                                 rc;
    GlobusGFSName(globus_l_gfs_spawn_child);

    globus_callback_register_signal_handler(
        SIGCHLD,
        GLOBUS_TRUE,
        globus_l_gfs_sigchld,
        NULL);

    result = globus_xio_handle_cntl(
        handle,
        globus_l_gfs_tcp_driver,
        GLOBUS_XIO_TCP_GET_HANDLE,
        &socket_handle);
    if(result != GLOBUS_SUCCESS)
    {
        globus_i_gfs_log_result(
            "Could not get handle from daemon process", result);
        goto error;
    }

    child_pid = fork();
    if (child_pid == 0)
    { 
        if(globus_l_gfs_xio_server)
        {
            globus_xio_server_close(globus_l_gfs_xio_server);
            globus_l_gfs_xio_server = GLOBUS_NULL;
        }
        
        globus_l_gfs_terminate_server(GLOBUS_TRUE);
        
        rc = dup2(socket_handle, STDIN_FILENO);
        if(rc == -1)
        {
            result = GlobusGFSErrorSystemError("dup2", errno);
            globus_i_gfs_log_result(
                "Could not open new handle for child process", result);
            goto error;
        }

        close(socket_handle);

        /* exec the process */
        prog_argv = (char **) globus_i_gfs_config_get("argv");
        for(i = 0; prog_argv[i] != NULL; i++)
        {
        }
        new_argv = (char **) globus_calloc(sizeof(char *) * i, 1);
        if(new_argv == NULL)
        {
        }
        new_argv[0] = globus_i_gfs_config_get("exec_name");
        for(i = 1; prog_argv[i] != NULL; i++)
        {
            if(strcmp(prog_argv[i], "-S") == 0 ||
                strcmp(prog_argv[i], "-s") == 0)
            {
                new_argv[i] = "-i";
            }
            else
            {
                new_argv[i] = prog_argv[i];
            }
        }
        new_argv[i] = NULL;

        rc = execv(new_argv[0], new_argv);
        if(rc == -1)
        {
            result = GlobusGFSErrorSystemError("execv", errno);
            globus_i_gfs_log_result(
                "Could not exec child process", result);
            goto error;
        }
    } 
    else if(child_pid == -1)
    {
    }
    else
    { 
        result = globus_xio_close(
            handle,
            GLOBUS_NULL);
        if(result != GLOBUS_SUCCESS)
        {
            globus_i_gfs_log_result(
                "Could not close handle in daemon process", result);
            goto error;
        }
    }    
   
    return GLOBUS_SUCCESS;
    
error:
    return result;
}


static
void
globus_l_gfs_new_server_cb(
    globus_xio_handle_t                 handle,
    globus_result_t                     result,
    void *                              user_arg)
{
    globus_xio_system_handle_t          system_handle;
    char *                              remote_contact;
    
    if(result != GLOBUS_SUCCESS)
    {
        goto error_cb;
    }
    
    result = globus_xio_handle_cntl(
        handle,
        globus_l_gfs_tcp_driver,
        GLOBUS_XIO_TCP_GET_REMOTE_CONTACT,
        &remote_contact);
    if(result != GLOBUS_SUCCESS)
    {
        goto error_peername;
    }
    
    globus_i_gfs_log_message(
        GLOBUS_I_GFS_LOG_INFO, "New connection from: %s\n", remote_contact);

    result = globus_xio_handle_cntl(
        handle,
        globus_l_gfs_tcp_driver,
        GLOBUS_XIO_TCP_GET_HANDLE,
        &system_handle);
    if(result != GLOBUS_SUCCESS)
    {
        goto error_peername;
    }
    
    if(globus_i_gfs_config_bool("data_node"))
    {
        result = globus_i_gfs_data_node_start(
            handle, system_handle, remote_contact);
    }
    else
    {        
        result = globus_i_gfs_control_start(
            handle, 
            system_handle, 
            remote_contact, 
            globus_l_gfs_server_closed,
            NULL);
    }
    
    if(result != GLOBUS_SUCCESS)
    {
        globus_i_gfs_log_result("Connection failed", result);
        goto error_start;
    }
    
    globus_free(remote_contact);
    return;

error_start:
    globus_free(remote_contact);
    
error_peername:
error_cb:
    globus_l_gfs_server_closed(NULL);
    if(!globus_l_gfs_xio_server)
    {
        /* I am the only one expected to run, die */
        globus_l_gfs_terminate_server(GLOBUS_TRUE);
    }
}

/* begin new server */
static
globus_result_t
globus_l_gfs_open_new_server(
    globus_xio_handle_t                 handle)
{
    globus_result_t                     result;
    
    globus_mutex_lock(&globus_l_gfs_mutex);
    {
        globus_l_gfs_open_count++;
    }
    globus_mutex_unlock(&globus_l_gfs_mutex);
    
    /* dont need the handle here, will get it in callback too */
    result = globus_xio_register_open(
        handle,
        GLOBUS_NULL,
        globus_l_gfs_xio_attr,
        globus_l_gfs_new_server_cb,
        GLOBUS_NULL);
    if(result != GLOBUS_SUCCESS)
    {
        goto error_open;
    }
    
    return GLOBUS_SUCCESS;

error_open:
    globus_l_gfs_server_closed(NULL);
    return result;
}

static
void
globus_l_gfs_prepare_stack(
    globus_xio_stack_t *                stack)
{
    globus_result_t                     result;
    
    result = globus_xio_stack_init(stack, GLOBUS_NULL);
    globus_l_gfs_check_log_and_die(result);
    
    result = globus_xio_stack_push_driver(*stack, globus_l_gfs_tcp_driver);
    globus_l_gfs_check_log_and_die(result);
}

/* begin a server instance from the channel already connected on stdin */
static
void
globus_l_gfs_convert_inetd_handle(void)
{
    globus_result_t                     result;
    globus_xio_stack_t                  stack;
    globus_xio_handle_t                 handle;
    
    globus_l_gfs_prepare_stack(&stack);
    
    result = globus_xio_attr_cntl(
        globus_l_gfs_xio_attr,
        globus_l_gfs_tcp_driver,
        GLOBUS_XIO_TCP_SET_HANDLE,
        STDIN_FILENO);
    globus_l_gfs_check_log_and_die(result);

    result = globus_xio_handle_create(&handle, stack);
    globus_l_gfs_check_log_and_die(result);
    globus_xio_stack_destroy(stack);
    
    result = globus_l_gfs_open_new_server(handle);
    globus_l_gfs_check_log_and_die(result);
}




/* a new client has connected */
static
void
globus_l_gfs_server_accept_cb(
    globus_xio_server_t                 server,
    globus_xio_handle_t                 handle,
    globus_result_t                     result,
    void *                              user_arg)
{
    if(result != GLOBUS_SUCCESS)
    {
        goto error_accept;
    }
    
    if(globus_i_gfs_config_bool("daemon"))
    {
        result = globus_l_gfs_spawn_child(handle);
        if(result != GLOBUS_SUCCESS)
        {

        }
    }
    else
    {
        result = globus_l_gfs_open_new_server(handle);
        if(result != GLOBUS_SUCCESS)
        {
            globus_i_gfs_log_result("Could not open new handle", result);
            /* we're not going to terminate the server just because we 
             * couldnt open a single client
             */
            result = GLOBUS_SUCCESS;
        }
    }
        /* be sure to close handle on server proc and close server on 
         * client proc (close on exec)
         *
         * need to handle proc exits and decrement the open server count
         * to keep the limit in effect.
         * 
         * win32 will have to simulate fork/exec... should i just do that
         * for unix too?
         */
    globus_mutex_lock(&globus_l_gfs_mutex);
    {
        int                             max;
        
        max = globus_i_gfs_config_int("max_connections");
        if(!globus_l_gfs_terminated &&
            (max == 0 || globus_l_gfs_open_count < max) &&
            !globus_i_gfs_config_bool("connections_disabled"))
        {
            result = globus_xio_server_register_accept(
                server,
                globus_l_gfs_server_accept_cb,
                GLOBUS_NULL);
            if(result != GLOBUS_SUCCESS)
            {
                goto error_register_accept;
            }
        }
        else
        {
            /* we've exceeded the open connections count.  delay further
             * accepts until an instance ends.
             * XXX this currently only affects non-daemon code.
             */
            globus_l_gfs_xio_server_accepting = GLOBUS_FALSE;
        }
    }
    globus_mutex_unlock(&globus_l_gfs_mutex);
    
    return;

error_register_accept:
    globus_mutex_unlock(&globus_l_gfs_mutex);
    
error_accept:
    if(!globus_l_gfs_terminated)
    {
        globus_i_gfs_log_result("Unable to accept connections", result);
        globus_l_gfs_terminate_server(GLOBUS_FALSE);
    }
}

/* start up a daemon which will spawn server instances */
static
void
globus_l_gfs_be_daemon(void)
{
    globus_result_t                     result;
    globus_xio_stack_t                  stack;
    globus_xio_attr_t                   attr;
    
    globus_l_gfs_prepare_stack(&stack);
    
    result = globus_xio_attr_init(&attr);
    globus_l_gfs_check_log_and_die(result);
    
    result = globus_xio_attr_cntl(
        attr,
        globus_l_gfs_tcp_driver,
        GLOBUS_XIO_TCP_SET_PORT,
        globus_i_gfs_config_int("port"));
    globus_l_gfs_check_log_and_die(result);
    
    result = globus_xio_attr_cntl(
        attr,
        globus_l_gfs_tcp_driver,
        GLOBUS_XIO_TCP_SET_REUSEADDR,
        GLOBUS_TRUE);
    globus_l_gfs_check_log_and_die(result);
    
    result = globus_xio_server_create(&globus_l_gfs_xio_server, attr, stack);
    globus_l_gfs_check_log_and_die(result);
    globus_xio_stack_destroy(stack);
    globus_xio_attr_destroy(attr);
    
    chdir("/");
    
    if(globus_i_gfs_config_int("port") == 0 ||
        globus_i_gfs_config_bool("contact_string"))
    {
        char *                          contact_string;
        
        result = globus_xio_server_get_contact_string(
            globus_l_gfs_xio_server,
            &contact_string);
        globus_l_gfs_check_log_and_die(result);
        
        globus_libc_printf("Server listening at %s\n", contact_string);
        globus_free(contact_string);
    }
    
/*
    if(globus_i_gfs_config_bool("daemon"))
    {
        struct sigaction                act;
        struct sigaction                oldact;

	signal(SIGPIPE, SIG_IGN);
	
        act.sa_handler = globus_l_gfs_sigchld;
        sigaction(SIGCHLD, &act, &oldact); 
    }
*/
    if(globus_i_gfs_config_bool("detach"))
    {
        /* this is where i would detach the server into the background
         * not sure how this will work for win32.  if it involves starting a
         * new process, need to set server handle to not close on exec
         */
        pid_t                           pid;
        pid = fork();
        if(pid < 0)
        {
        }
        else if(pid != 0)
        {
            exit(0);
        }
        else
        {
            setsid();
            chdir("/");
        }
    }
    
    globus_l_gfs_xio_server_accepting = GLOBUS_TRUE;
    result = globus_xio_server_register_accept(
        globus_l_gfs_xio_server,
        globus_l_gfs_server_accept_cb,
        GLOBUS_NULL);
    globus_l_gfs_check_log_and_die(result);
}

int
main(
    int                                 argc,
    char **                             argv)
{
    globus_result_t                     result;
    
    globus_module_activate(GLOBUS_XIO_MODULE);
    globus_module_activate(GLOBUS_FTP_CONTROL_MODULE);
    globus_module_activate(GLOBUS_GRIDFTP_SERVER_CONTROL_MODULE);
    
    globus_i_gfs_config_init(argc, argv);
    globus_i_gfs_log_open();
    globus_l_gfs_signal_init();
    globus_i_gfs_data_init();
    globus_gfs_ipc_init();

    if(globus_i_gfs_config_bool("version"))
    {
        globus_version_print(
            local_package_name,
            &local_version,
            stderr,
            GLOBUS_TRUE);

        return 0;
    }
    if(globus_i_gfs_config_bool("versions"))
    {
        globus_version_print(
            local_package_name,
            &local_version,
            stderr,
            GLOBUS_TRUE);

        globus_module_print_activated_versions(
            stderr,
            GLOBUS_TRUE);

        return 0;
    }

    globus_mutex_init(&globus_l_gfs_mutex, GLOBUS_NULL);
    globus_cond_init(&globus_l_gfs_cond, GLOBUS_NULL);
    
    result = globus_xio_driver_load("tcp", &globus_l_gfs_tcp_driver);
    globus_l_gfs_check_log_and_die(result);
    
    result = globus_xio_attr_init(&globus_l_gfs_xio_attr);
    globus_l_gfs_check_log_and_die(result);

    if(globus_i_gfs_config_bool("inetd"))
    {
        globus_l_gfs_convert_inetd_handle();
    }
    else
    {
        globus_l_gfs_be_daemon();
    }
    
    globus_mutex_lock(&globus_l_gfs_mutex);
    {
        while(!globus_l_gfs_terminated || globus_l_gfs_open_count > 0)
        {
            globus_cond_wait(&globus_l_gfs_cond, &globus_l_gfs_mutex);
        }
    }
    globus_mutex_unlock(&globus_l_gfs_mutex);
    
    if(globus_l_gfs_xio_server)
    {
        globus_xio_server_close(globus_l_gfs_xio_server);
    }
    
    globus_xio_attr_destroy(globus_l_gfs_xio_attr);
    globus_xio_driver_unload(globus_l_gfs_tcp_driver);
    globus_i_gfs_log_close();
    
    globus_module_deactivate_all();
    
    return 0;
}


static
void
globus_l_gfs_terminate_server(
    globus_bool_t                       immediately)
{
    /* if(immediately), abort all current connections...
     * only applicable when not forking off children..
     * should be able to terminated forked children also
     */
    globus_mutex_lock(&globus_l_gfs_mutex);
    {
        if(!globus_l_gfs_terminated)
        {
            globus_l_gfs_terminated = GLOBUS_TRUE;
            if(globus_l_gfs_open_count == 0)
            {
                globus_cond_signal(&globus_l_gfs_cond);
            }
        }
    }
    globus_mutex_unlock(&globus_l_gfs_mutex);
}

static
void
globus_l_gfs_server_closed(
    void *                              user_arg)
{
    globus_result_t                     result;
    

    if(globus_i_gfs_config_bool("inetd"))
    {
        globus_l_gfs_terminate_server(GLOBUS_TRUE);
    }
    globus_mutex_lock(&globus_l_gfs_mutex);
    {
        if(--globus_l_gfs_open_count == 0 && globus_l_gfs_terminated)
        {
            globus_cond_signal(&globus_l_gfs_cond);
        }
        else if(globus_l_gfs_xio_server &&
            !globus_l_gfs_xio_server_accepting &&
            !globus_l_gfs_terminated)
        {
            /* we must have hit the limit of open servers before...
             * the death of this one opens a slot
             */
            globus_l_gfs_xio_server_accepting = GLOBUS_TRUE;
            result = globus_xio_server_register_accept(
                globus_l_gfs_xio_server,
                globus_l_gfs_server_accept_cb,
                GLOBUS_NULL);
            if(result != GLOBUS_SUCCESS)
            {
                goto error_accept;
            }
        }
    }
    globus_mutex_unlock(&globus_l_gfs_mutex);
        
    return;
    
error_accept:
    globus_mutex_unlock(&globus_l_gfs_mutex);
    globus_i_gfs_log_result("Unable to accept connections", result);
    globus_l_gfs_terminate_server(GLOBUS_FALSE);
}

/* check. if ! success, log and die */
static
void
globus_l_gfs_check_log_and_die(
    globus_result_t                     result)
{
    if(result != GLOBUS_SUCCESS)
    {
        globus_i_gfs_log_result("Could not start server", result);
        exit(1);
    }
}
