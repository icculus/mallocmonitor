/*
 * glibc-compatible hooks into C runtime memory allocation routines.
 *
 * Details on this hackery are in the glibc manual:
 *   http://www.delorie.com/gnu/docs/glibc/libc_34.html
 *
 * Build this:
 *  gcc -Wall -O0 -g -shared -o malloc_hook_glibc.so malloc_hook_glibc.c
 *
 * Run a program:
 *  LD_PRELOAD=./malloc_hook_glibc.so ./myprogram
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <malloc.h>

#include "malloc_monitor.h"

/* !!! FIXME: don't use void * ... */
static void *glibc_malloc_hook = NULL;
static void *glibc_realloc_hook = NULL;
static void *glibc_memalign_hook = NULL;
static void *glibc_free_hook = NULL;

static inline save_glibc_hooks(void);
static inline set_glibc_hooks(void);
static inline set_override_hooks(void);

static void *override_malloc_hook(size_t s, const void *caller)
{
    void *retval;

    set_glibc_hooks();  /* put glibc back in control. */
    retval = malloc(s);  /* call glibc version. */
    save_glibc_hooks();  /* update in case glibc changed them. */

    if (MALLOCMONITOR_put_malloc(s, retval, caller))
        set_override_hooks(); /* only restore hooks if daemon is listening */

    return(retval);
} /* override_malloc_hook */


static void *override_realloc_hook(void *ptr, size_t s, const void *caller)
{
    void *retval;

    set_glibc_hooks();  /* put glibc back in control. */
    retval = realloc(ptr, s);  /* call glibc version. */
    save_glibc_hooks();  /* update in case glibc changed them. */

    if (MALLOCMONITOR_put_realloc(ptr, s, retval, caller))
        set_override_hooks(); /* only restore hooks if daemon is listening */

    return(retval);
} /* override_realloc_hook */


static void *override_memalign_hook(size_t a, size_t s, const void *caller)
{
    void *retval;
    set_glibc_hooks();  /* put glibc back in control. */
    retval = memalign(a, s);  /* call glibc version. */
    save_glibc_hooks();  /* update in case glibc changed them. */

    if (MALLOCMONITOR_put_memalign(a, s, retval, caller))
        set_override_hooks(); /* only restore hooks if daemon is listening */

    return(retval);
} /* override_memalign_hook */


static void override_free_hook(void *ptr, const void *caller)
{
    set_glibc_hooks();  /* put glibc back in control. */
    free(ptr);  /* call glibc version. */
    save_glibc_hooks();  /* update in case glibc changed them. */

    if (MALLOCMONITOR_put_free(ptr, caller))
        set_override_hooks(); /* only restore hooks if daemon is listening */
} /* override_free_hook */


/*
 * Save a copy of the original allocation hooks, so we can call into them
 *  from our overriding functions. It's possible that glibc might change
 *  these hooks under various conditions (so the manual's examples seem
 *  to suggest), so we update them whenever we finish calling into the
 *  the originals.
 */
static inline save_glibc_hooks(void)
{
    glibc_malloc_hook = __malloc_hook;
    glibc_realloc_hook = __realloc_hook;
    glibc_memalign_hook = __memalign_hook;
    glibc_free_hook = __free_hook;
} /* save_glibc_hooks */

/*
 * Restore the hooks to the glibc versions. This is needed since, say,
 *  their realloc() might call malloc() or free() under the hood, etc, so
 *  it's safer to let them have complete control over the subsystem, which
 *  also makes our logging saner, too.
 */
static inline set_glibc_hooks(void)
{
    __malloc_hook = glibc_malloc_hook;
    __realloc_hook = glibc_realloc_hook;
    __memalign_hook = glibc_memalign_hook;
    __free_hook = glibc_free_hook;
} /* set_glibc_hooks */


/*
 * Put our hooks back in place. This should be done after the original
 *  glibc version has been called and we've finished any logging (which
 *  may call glibc functions, too). This sets us up for the next calls from
 *  the application.
 */
static inline set_override_hooks(void)
{
    __malloc_hook = override_malloc_hook;
    __realloc_hook = override_realloc_hook;
    __memalign_hook = override_memalign_hook;
    __free_hook = override_free_hook;
} /* set_override_hooks */


/*
 * glibc will call this when the malloc subsystem is initializing, giving
 *  us a chance to install hooks that override the functions.
 */
static void override_init_hook(void)
{
    /* install our hooks. Will connect to daemon on first malloc, etc. */
    save_glibc_hooks();
    set_override_hooks();
} /* override_init_hook */


/*
 * The Hook Of All Hooks:
 *
 * __malloc_initialize_hook is apparently a "weak variable", so you can
 *  define and assign it here even though it's in glibc, too. This lets
 *  us hook into malloc as soon as the runtime initializes, and before
 *  main() is called.
 */
void (*__malloc_initialize_hook)(void) = override_init_hook;

/* end of malloc_hook_glibc.c ... */

