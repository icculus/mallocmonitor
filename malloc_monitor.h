/*
 * Interface to the Malloc Monitor daemon.
 *
 * These functions will connect to the monitor daemon if needed, using
 *  the MALLOCMONITORHOST and MALLOCMONITORPORT environment variables.
 *  If you don't want to use those environment variables or autoconnect,
 *  you can manually connect with the MALLOCMONITOR_connect() function.
 *
 * Please note that "daemon" may be another host or process via a socket,
 *  or it might just be a file we dump data to.
 *
 * MAKE SURE that it is safe to call C runtime functions when you call
 *  any of these functions! Also, they may call malloc() themselves, but
 *  you shouldn't hook these calls (or be prepared to ignore the calls
 *  until they return).
 *
 * The monitoring daemon considers it acceptable for connections to rudely
 *  drop, since we can't guarantee that you'll be able to run a shutdown
 *  function. Still, you can call MALLOCMONITOR_shutdown() if possible.
 *  If you can't, the socket will drop when the process is terminated, which
 *  comes to the same thing.
 *
 * If these fail, it means that the daemon couldn't be contacted or the
 *  connection was lost. In the case of failure, you should remove your
 *  hooks if possible, or at least stop calling these functions, since
 *  further monitoring is basically useless for this run. If you don't, the
 *  next call to one of these functions will try to reconnect to the
 *  daemon, but you won't have a complete view of your allocation patterns.
 *
 * Any of these functions may block. You have been warned.
 *
 * Written by Ryan C. Gordon (icculus@icculus.org)
 *
 * Please see the file LICENSE in the source's root directory.
 */

#ifndef _INCL_MALLOC_MONITOR_H_
#define _INCL_MALLOC_MONITOR_H_

#ifdef __cplusplus
extern "C" {
#endif

#include <stdlib.h>  /* size_t definition. */

#define MALLOCMONITOR_DEFAULT_PORT 22222

/*
 * Connect to the monitoring daemon. Will implicitly hang up first if there's
 *  already a connection. You usually don't need this call; any of the
 *  MALLOCMONITOR_put_* calls will automatically connect to the daemon with
 *  MALLOCMONITOR_defaultconnect() if need be, but this lets you have finer
 *  control.
 *
 * The default "host" is actually a file in the cwd named after the id.
 * The default port is MALLOCMONITOR_DEFAULT_PORT.
 *
 *     params : host == hostname where daemon lives or "[file]"
 *              port == TCP/IP port daemon is listening on.
 *              id == identifier for this client.
 *    returns : non-zero if connects to monitor daemon, zero on failure.
 */
int MALLOCMONITOR_connect(const char *host, int port, const char *id);

/*
 * Connect to the monitoring daemon. Will implicitly hang up first if there's
 *  already a connection. You usually don't need this call; any of the
 *  MALLOCMONITOR_put_* calls will call this for you if needed.
 *
 * This function will generate a unique ID for this session...usually
 *  something like the operating system process id and the binary's name.
 *
 * If there was a previous successful connection to the daemon during this
 *  run, it'll use that hostname/port. Otherwise, it uses the environment
 *  variables MALLOCMONITORHOST and MALLOCMONITORPORT for hostname and port
 *  number, or "[file]" and MALLOCMONITOR_DEFAULT_PORT if they aren't set.
 *
 * After figuring all this out, it will call MALLOCMONITOR_connect().
 *
 *     params : none.
 *    returns : non-zero if connects to monitor daemon, zero on failure.
 */
int MALLOCMONITOR_defaultconnect(void);

/*
 * Determine if we're connected to a monitoring daemon. Doesn't block.
 *  This may report "not connected" if MALLOCMONITOR_connect() succeeded
 *  but the connection was since lost, even if you didn't call
 *  MALLOCMONITOR_disconnect().
 *
 *     params : none.
 *    returns : non-zero if connected to monitor daemon, zero if not.
 */
int MALLOCMONITOR_connected(void);

/*
 * Terminate the connection to the monitoring daemon. Next call to something
 *  that wants to talk to the daemon will cause a reconnect with the
 *  previous connection's hostname and port if you don't explicitly
 *  reconnect with something else.
 *
 * It's okay to not call this on program termination; the daemon is fine
 *  with rude hangups. This is just for better manual control.
 *
 *     params : none.
 *    returns : void.
 */
void MALLOCMONITOR_disconnect(void);


/*
 * Tell the monitoring daemon that the application just called malloc().
 *
 *     params : s == number of bytes app wanted to malloc().
 *              rc == what C runtime's malloc() returned.
 *    returns : non-zero if reported to monitor daemon, zero on failure.
 */
int MALLOCMONITOR_put_malloc(size_t s, void *rc);

/*
 * Tell the monitoring daemon that the application just called realloc().
 *
 *     params : p == address of memory block app wanted to reallocate.
 *              s == number of bytes app wanted to realloc().
 *              rc == what C runtime's realloc() returned.
 *    returns : non-zero if reported to monitor daemon, zero on failure.
 */
int MALLOCMONITOR_put_realloc(void *p, size_t s, void *rc);

/*
 * Tell the monitoring daemon that the application just called memalign()
 *  or some variation, like posix_memalign() or valloc().
 *
 *     params : b == multiple of boundary that app wants to align on.
 *              s == number of bytes app wanted to allocate.
 *              rc == what C runtime's memalign() returned.
 *    returns : non-zero if reported to monitor daemon, zero on failure.
 */
int MALLOCMONITOR_put_memalign(size_t b, size_t s, void *rc);

/*
 * Tell the monitoring daemon that the application just called free()
 *
 *     params : p == pointer that was free()'d.
 *    returns : non-zero if reported to monitor daemon, zero on failure.
 */
int MALLOCMONITOR_put_free(void *p);

#ifdef __cplusplus
}
#endif

#endif  /* include-once blocker. */

/* end of malloc_monitor.h ... */

