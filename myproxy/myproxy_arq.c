/*
 * myproxy_arq.c
 *
 * Admin repository query tool
 *
 */

#include "myproxy_creds.h"
#include "myproxy.h"
#include "myproxy_server.h"
#include "myproxy_log.h"
#include "ssl_utils.h"
#include "gnu_getopt.h"
#include "verror.h"
#include "myproxy_read_pass.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>

#define BINARY_NAME "myproxy-arq"

static char usage[] = \
"\n"\
" Syntax:  "  BINARY_NAME "[-usage|-help] [-version]\n"\
"\n"\
"    Options\n"\
"    -h | --help                	Displays usage\n"\
"    -u | --usage                             \n"\
"						\n"\
"    -v | --verbose             	Display debugging messages\n"\
"    -V | --version             	Displays version\n"\
"    -s | --storage <directory> 	Specifies the credential storage directory\n"
"                                            \n"\
"\n";

struct option long_options[] =
{
    {"help",             no_argument, NULL, 'h'},
    {"usage",            no_argument, NULL, 'u'},
    {"storage",	   required_argument, NULL, 's'},
    {"verbose",          no_argument, NULL, 'v'},
    {"version",          no_argument, NULL, 'V'},
    {0, 0, 0, 0}
};

static char short_options[] = "hus:vV";

static char version[] =
BINARY_NAME "version " MYPROXY_VERSION " (" MYPROXY_VERSION_DATE ") "  "\n";

/* Function declarations */
void init_arguments(int argc, char *argv[]);

void print_cred_info(myproxy_creds_t *creds);

int
main(int argc, char *argv[]) 
{
    struct myproxy_creds cred;
 
   /* Initialize arguments*/
    init_arguments(argc, argv);

    memset (&cred, 0, sizeof (cred));
    if (myproxy_admin_retrieve_all(&cred) < 0) {
        myproxy_log_verror();
        fprintf (stderr, "Unable to read credentials !! %s\n", verror_get_string());
    } else {
	print_cred_info (&cred);
    }

    return 0;
}

void 
init_arguments(int argc, 
		       char *argv[])
{
    extern char *gnu_optarg;
    int arg;

    while((arg = gnu_getopt_long(argc, argv, short_options, 
                             long_options, NULL)) != EOF) 
    {
        switch(arg) 
        {  
        case 's': /* set the credential storage directory */
        { char *s;
          s=(char *) malloc(strlen(gnu_optarg) + 1);
          strcpy(s,gnu_optarg);
          myproxy_set_storage_dir(s);
          break;
         }
        case 'u': 	/* print help and exit */
            fprintf(stderr, usage);
            exit(1);
       	    break;
	case 'h': 	/* print help and exit */
            fprintf(stderr, usage);
            exit(1);
            break;
	case 'v':	/* verbose */
	    myproxy_debug_set_level(1);
	    break;
        case 'V':       /* print version and exit */
            fprintf(stderr, version);
            exit(1);
            break;
        default:        /* print usage and exit */ 
            fprintf(stderr, usage);
	    exit(1);
            break;	
        }
    }

    return;
}

void
print_cred_info(myproxy_creds_t *creds)
{
    int first_time = 1;
    if (!creds) return;
    for (; creds; creds = creds->next) {
        time_t time_diff, now;
        float days;
        if (creds->credname) {
            printf("%s:\n", creds->credname);
        } else if (first_time) {
            printf("default credential:\n");
        } else {
            printf("unnamed credential:\n");
        }

    	printf("  owner: %s\n", creds->owner_name);
        if (creds->creddesc) {
            printf("  description: %s\n", creds->creddesc);
        }
        if (creds->retrievers) {
            printf("  retrieval policy: %s\n", creds->retrievers);
        }
        if (creds->renewers) {
            printf("  renewal policy: %s\n", creds->renewers);
        }
        now = time(0);
        if (creds->end_time > now) {
            time_diff = creds->end_time - now;
            days = time_diff / 86400.0;
        } else {
            time_diff = 0;
            days = 0.0;
        }
        printf("  timeleft: %ld:%02ld:%02ld",
               (long)(time_diff / 3600),
               (long)(time_diff % 3600) / 60,
               (long)time_diff % 60 );
        if (days > 1.0) {
            printf("  (%.1f days)\n", days);
        } else {
            printf("\n");
        }
        first_time = 0;
    }
}

