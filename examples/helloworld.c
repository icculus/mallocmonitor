/*
 * A brief and silly test case.
 *
 * You can run this standalone and LD_PRELOAD malloc_monitor.so, or
 *  statically link the appropriate object files, or define
 *  MANUALLY_MONITOR and just statically link the malloc_monitor_client
 *  object file to talk to the daemon directly.
 *
 * Written by Ryan C. Gordon (icculus@icculus.org)
 *
 * Please see the file LICENSE in the source's root directory.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#if MANUALLY_MONITOR
#include "malloc_monitor.h"
#endif

static void *mymalloc(size_t s)
{
    void *retval = malloc(s);

    #if MANUALLY_MONITOR
    if (MALLOCMONITOR_connected())
        MALLOCMONITOR_put_malloc(s, retval);
    #endif

    return(retval);
} /* mymalloc */


static void myfree(void *ptr)
{
    free(ptr);
    #if MANUALLY_MONITOR
    if (MALLOCMONITOR_connected())
        MALLOCMONITOR_put_free(ptr);
    #endif
} /* myfree */


#define SHOUTOUT "Hello, %s!\n"

static void hello(const char *name)
{
    char *buffer = mymalloc(strlen(SHOUTOUT) + strlen(name) + 1);
    if (buffer == NULL)
    {
        fprintf(stderr, "mymalloc() failed for name '%s'!\n", name);
        return;
    } /* if */

    printf(SHOUTOUT, name);
    myfree(buffer);
} /* hello */


int main(int argc, char **argv)
{
    int i;

    #if MANUALLY_MONITOR
    MALLOCMONITOR_defaultconnect();
    #endif

    for (i = 1; i < argc; i++)
        hello(argv[i]);
    hello("all y'all suckers");

    #if MANUALLY_MONITOR
    MALLOCMONITOR_disconnect();
    #endif

    return(0);
} /* main */

/* end of helloworld.c ... */

