/*
 * Implementation of the interface to the Malloc Monitor daemon.
 *  Verbose chatter about these functions are in malloc_monitor.h.
 *
 * Written by Ryan C. Gordon (icculus@icculus.org)
 *
 * Please see the file LICENSE in the source's root directory.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>

#include "malloc_monitor.h"

#define DAEMON_HELLO_SIG "Malloc Monitor!"
#define DAEMON_PROTOCOL_VERSION 1


#ifdef _WIN32
    #error look out, this is not a tested codepath!
    #include <winsock.h>
    #ifdef _MSC_VER
        #define snprintf _snprintf
    #endif

    #define GetLastSocketError() WSAGetLastError()
    #define GetLastHostError() WSAGetLastError()
    #define MSG_NOSIGNAL 0x0000

    static int SocketLayerInitialized = 0;

    static inline int SocketLayerInitialize(void)
    {
        if (!SocketLayerInitialized)
        {
            WSADATA data;
            int rc = WSAStartup(0x0101, &data);
            if (rc == 0)
            {
                SocketLayerInitialized = 1;
                return(1);
            } /* if */

            fprintf(stderr, "MALLOCMONITOR: WSAStartup() failed: %d\n", rc);
            return(0);
        } /* if */

        return(1);
    } /* SocketLayerInitialize */

    static inline void SocketLayerCleanup(void)
    {
        if (SocketLayerInitialized)
        {
            WSACleanup();
            SocketLayerInitialized = 0;
        } /* if */
    } /* SocketLayerCleanup */

    static inline void get_process_filename(char *fname, size_t s)
    {
        fname[0] = 0;  // !!! FIXME
    }

#else
    #include <sys/socket.h>
    #include <netinet/in.h>
    #include <arpa/inet.h>
    #include <netdb.h>

    #if MACOSX /* || FREEBSD? */
        #define GetLastHostError() errno
    #else
        #define GetLastHostError() h_errno
    #endif

    /* typedefs for various Winsock crapola. */
    typedef struct sockaddr_in SOCKADDR_IN;
    typedef struct sockaddr SOCKADDR;
    typedef struct hostent HOSTENT;
    #define GetLastSocketError() errno
    #define closesocket(s) close(s)
    #define INVALID_SOCKET -1
    #define SOCKET_ERROR -1
    #define SocketLayerInitialize() (1)
    #define SocketLayerCleanup()

    /* Not defined before glibc < 2.1.3 */
    #ifndef MSG_NOSIGNAL
    #define MSG_NOSIGNAL 0x4000
    #endif

    #if MACOSX
    static inline void get_process_filename(char *fname, size_t s)
    {
        fname[0] = 0;  // !!! FIXME
    }
    #else
    static inline void get_process_filename(char *fname, size_t s)
    {
        /* Holy Linux-specific, batman! */
        char proclink[64];
        pid_t pid = getpid();
        snprintf(proclink, sizeof (proclink), "/proc/%d/exe", (int) pid);
        if (readlink(proclink, fname, s) == -1)
            *fname = '\0';
        fname[s-1] = '\0';  /* just in case. */
    } /* get_process_filename */
    #endif

#endif

/* sizes are checked at runtime... */
typedef unsigned int uint32;
typedef unsigned char uint8;

typedef enum
{
    MONITOR_OP_NOOP = 0,
    MONITOR_OP_GOODBYE,
    MONITOR_OP_MALLOC,
    MONITOR_OP_REALLOC,
    MONITOR_OP_MEMALIGN,
    MONITOR_OP_FREE,
    MONITOR_OP_TOTAL
} monitor_operation_t;

static int sockfd = -1;
static uint32 lastip = 0;
static int lastport = 0;
static int isfile = 0;

static inline int is_bigendian(void)
{
    uint32 x = 0x01000000;
    return(*((unsigned char *) &x));
} /* is_bigendian */


/* returns a static buffer! */
static const char *hostfromip(uint32 ip)
{
    static char buf[16];

    /* !!! FIXME: byte order? */
    snprintf(buf, sizeof (buf), "%u.%u.%u.%u",
                (ip >> 24) & 0xFF,
                (ip >> 16) & 0xFF,
                (ip >>  8) & 0xFF,
                (ip      ) & 0xFF);

    return(buf);
} /* hostfromip */


static void disconnect_from_daemon(int graceful);

static int daemon_write(const void *block, size_t blocksize)
{
    if (sockfd == -1)
        return(0);

    if (isfile)
    {
        if (write(sockfd, block, blocksize) != blocksize)
        {
            /* !!! FIXME: strerror() probably doesn't work with WinSock. */
            int e = errno;
            fprintf(stderr, "MALLOCMONITOR: write() failed: %d (%s)\n",
                    e, strerror(e));
            disconnect_from_daemon(0);
            return(0);
        } /* if */
    } /* if */

    else  /* tcp/ip socket. */
    {
        if (send(sockfd, block, blocksize, MSG_NOSIGNAL) != blocksize)
        {
            /* !!! FIXME: strerror() probably doesn't work with WinSock. */
                int e = GetLastSocketError();
            fprintf(stderr, "MALLOCMONITOR: send() failed: %d (%s)\n",
                    e, strerror(e));
            disconnect_from_daemon(0);
            return(0);
        } /* if */
    } /* else */

    return(1);
} /* daemon_write */


static inline int daemon_write_ui8(uint8 ui8)
{
    return(daemon_write(&ui8, sizeof (uint8)));
} /* daemon_write_ui8 */


static inline int daemon_write_ui32(uint32 ui32)
{
    return(daemon_write(&ui32, sizeof (uint32)));
} /* daemon_write_ui32 */


static inline int daemon_write_asciz(const char *str)
{
    return(daemon_write(str, strlen(str) + 1));
} /* daemon_write_asciz */


static inline int daemon_write_operation(monitor_operation_t op)
{
    return(daemon_write_ui8((uint8) op));
} /* daemon_write_operation */


static inline int daemon_write_ptr(const void *ptr)
{
    return(daemon_write((void *) &ptr, sizeof (const void *)));
} /* daemon_write_operation */


static inline int daemon_write_sizet(size_t s)
{
    return(daemon_write(&s, sizeof (size_t)));
} /* daemon_write_operation */


static inline int daemon_write_callstack(const void *caller)
{
    if (caller == NULL)  /* no callstack. */
    {
        if (!daemon_write_ui32(0)) return(0);
        return(1);
    } /* if */

    /* !!! FIXME! */
    if (!daemon_write_ui32(0)) return(0);
    return(1);
} /* daemon_write_operation */


static void disconnect_from_daemon(int graceful)
{
    if (sockfd != -1)
    {
        if (graceful)
            daemon_write_operation(MONITOR_OP_GOODBYE);

        if (isfile)
            close(sockfd);
        else
        {
            closesocket(sockfd);
            SocketLayerCleanup();
        } /* else */
        sockfd = -1;
    } /* if */
} /* disconnect_from_daemon */


static inline int daemon_write_handshake(const char *id)
{
    uint8 sizeofptr = (uint8) (sizeof (void *));
    uint8 byteorder = (is_bigendian() ? 1 : 0);
    char fname[512];
    uint32 pid = (uint32) getpid();
    get_process_filename(fname, sizeof (fname));

    /* if the server drops us, daemon_write_* cleans up. */
    if (!daemon_write_asciz(DAEMON_HELLO_SIG)) return(0);
    if (!daemon_write_ui8(DAEMON_PROTOCOL_VERSION)) return(0);
    if (!daemon_write_ui8(byteorder)) return(0);
    if (!daemon_write_ui8(sizeofptr)) return(0);
    if (!daemon_write_asciz(id)) return(0);
    if (!daemon_write_asciz(fname)) return(0);
    if (!daemon_write_ui32(pid)) return(0);

    return(1);
} /* daemon_write_handshake */


static int connect_to_daemon(uint32 ip, int port, const char *id)
{
    SOCKADDR_IN addr;

    if (port == 0)
    {
        fprintf(stderr, "MALLOCMONITOR: port is 0!\n");
        return(0);
    } /* if */

    MALLOCMONITOR_disconnect();
    if (!SocketLayerInitialize())
        return(0);

    sockfd = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (sockfd == INVALID_SOCKET)
    {
        /* !!! FIXME: strerror() probably doesn't work with WinSock. */
        int e = GetLastSocketError();
        fprintf(stderr, "MALLOCMONITOR: socket() failed: %d (%s)\n",
                e, strerror(e));
        SocketLayerCleanup();
        return(0);
    } /* if */

    memset(&addr, '\0', sizeof (addr));
    addr.sin_family = AF_INET;
    addr.sin_port = htons( ((short) port) );
    addr.sin_addr.s_addr = htonl(ip);
    if (connect(sockfd, (SOCKADDR*) &addr, sizeof (addr)) == SOCKET_ERROR)
    {
        /* !!! FIXME: strerror() probably doesn't work with WinSock. */
        int e = GetLastSocketError();
        fprintf(stderr, "MALLOCMONITOR: connect() to %s:%d failed: %d (%s)\n",
                hostfromip(ip), (int) port, e, strerror(e));
        closesocket(sockfd);
        sockfd = -1;
        SocketLayerCleanup();
        return(0);
    } /* if */

    if (!daemon_write_handshake(id))
        return(0);

    /* we're golden. */
    fprintf(stderr, "MALLOCMONITOR: Connected to daemon at %s:%d!\n",
            hostfromip(ip), port);

    lastip = ip;
    lastport = port;
    return(1);
} /* connect_to_daemon */


static inline int verify_connection(void)
{
    if (!MALLOCMONITOR_connected())
        if (!MALLOCMONITOR_defaultconnect())
            return(0);
    return(1);
} /* verify_connection */


int MALLOCMONITOR_connect(const char *host, int port, const char *id)
{
    HOSTENT *hostent;
    uint32 ip;

    /* check our assumptions... */
    if ( (sizeof (uint8) != 1) ||
         (sizeof (uint32) != 4) ||
         (sizeof (void *) != sizeof (size_t)) )
    {
        fprintf(stderr, "MALLOCMONITOR: we're miscompiled!\n");
        return(0);
    } /* if */

    isfile = (strcmp(host, "[file]") == 0);
    if (isfile)
    {
        char fname[64];
        snprintf(fname, sizeof (fname), "./mallocmonitor-%s.dump", id);
        sockfd = open(fname, O_CREAT | O_TRUNC | O_WRONLY, S_IREAD | S_IWRITE);
        if (sockfd == -1)
        {
            /* !!! FIXME: strerror() probably doesn't work with WinSock. */
            int e = errno;
            fprintf(stderr, "MALLOCMONITOR: open('%s') failed: %d (%s)\n",
                    fname, e, strerror(e));

            return(0);
        } /* if */

        if (!daemon_write_handshake(id))
            return(0);

        /* we're golden. */
        fprintf(stderr, "MALLOCMONITOR: Logging to file '%s'!\n", fname);
        return(1);
    } /* if */


    /* it's a networked daemon... */

    if (!SocketLayerInitialize())
        return(0);

    hostent = gethostbyname(host);
    if (hostent == NULL)
    {
        /* !!! FIXME: strerror() probably doesn't work with WinSock. */
        int e = GetLastHostError();
        fprintf(stderr, "MALLOCMONITOR: gethostbyname('%s') failed: %d (%s)\n",
                host, e, strerror(e));
        MALLOCMONITOR_disconnect();
        return(0);
    } /* if */

    ip = *((uint32 *)(hostent->h_addr_list[0]));
    ip = ntohl(ip);

    return(connect_to_daemon(ip, port, id));
} /* MALLOCMONITOR_connect */


int MALLOCMONITOR_defaultconnect(void)
{
    char id[64];
    pid_t pid = getpid();
    /* !!! FIXME: need process name. */
    snprintf(id, sizeof (id), "%lu", (unsigned long) pid);

    if (lastport == 0)  /* no previous connection? */
    {
        const char *envhost = getenv("MALLOCMONITORHOST");
        const char *envport = getenv("MALLOCMONITORPORT");
        int port = ((envport) ? atoi(envport) : MALLOCMONITOR_DEFAULT_PORT);
        if (envhost == NULL)
            envhost = "[file]";
        return(MALLOCMONITOR_connect(envhost, port, id));
    } /* if */

    return(connect_to_daemon(lastip, lastport, id));
} /* MALLOCMONITOR_defaultconnect */


int MALLOCMONITOR_connected(void)
{
    return(sockfd != -1);
} /* MALLOCMONITOR_connected */


void MALLOCMONITOR_disconnect(void)
{
    disconnect_from_daemon(1);
} /* MALLOCMONITOR_disconnect */


int MALLOCMONITOR_put_malloc(size_t s, void *rc, const void *c)
{
    if (!verify_connection()) return(0);
    if (!daemon_write_operation(MONITOR_OP_MALLOC)) return(0);
    if (!daemon_write_sizet(s)) return(0);
    if (!daemon_write_ptr(rc)) return(0);
    if (!daemon_write_callstack(c)) return(0);
    return(1);
} /* MALLOCMONITOR_put_malloc */


int MALLOCMONITOR_put_realloc(void *p, size_t s, void *rc, const void *c)
{
    if (!verify_connection()) return(0);
    if (!daemon_write_operation(MONITOR_OP_REALLOC)) return(0);
    if (!daemon_write_ptr(p)) return(0);
    if (!daemon_write_sizet(s)) return(0);
    if (!daemon_write_ptr(rc)) return(0);
    if (!daemon_write_callstack(c)) return(0);
    return(1);
} /* MALLOCMONITOR_put_realloc */


int MALLOCMONITOR_put_memalign(size_t b, size_t s, void *rc, const void *c)
{
    if (!verify_connection()) return(0);
    if (!daemon_write_operation(MONITOR_OP_MEMALIGN)) return(0);
    if (!daemon_write_sizet(b)) return(0);
    if (!daemon_write_sizet(s)) return(0);
    if (!daemon_write_ptr(rc)) return(0);
    if (!daemon_write_callstack(c)) return(0);
    return(1);
} /* MALLOCMONITOR_put_memalign */


int MALLOCMONITOR_put_free(void *p, const void *c)
{
    if (!verify_connection()) return(0);
    if (!daemon_write_operation(MONITOR_OP_FREE)) return(0);
    if (!daemon_write_ptr(p)) return(0);
    if (!daemon_write_callstack(c)) return(0);
    return(1);
} /* MALLOCMONITOR_put_free */

/* end of malloc_monitor_client.c ... */


