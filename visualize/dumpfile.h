/*
 * Interface to parsing a dumpfile.
 *
 * Written by Ryan C. Gordon (icculus@icculus.org)
 *
 * Please see the file LICENSE in the source's root directory.
 */

#ifndef _INCL_DUMPFILE_H_
#define _INCL_DUMPFILE_H_

#ifndef __cplusplus
#error this is for C++ only.
#endif

#include <stdio.h>

/*
 * A callback interface for presenting a progress UI to the end user, and
 *  pumping a GUI's event queue. "update" is called periodically during
 *  dumpfile processing (an expensive operation). "status" is the name
 *  of the operation ("parsing raw data" or whatnot), and percent is the
 *  operation's progress...zero to 100.
 */
class ProgressNotify
{
public:
    virtual void update(const char *status, int percent) = 0;
};

class ProgressNotifyDummy : public ProgressNotify
{
    virtual void update(const char *status, int percent) {}
};


/*
 * The CallstackManager efficiently stores and retrieves callstack data
 *  from the dumpfile. It minimizes the amount of memory needed by
 *  aggressively caching duplicate information.
 *
 * The DumpFile class maintains an instance of CallstackManager, and feeds
 *  it callstacks from the dumpfile. The CallstackManager feeds back a unique
 *  ID that represents that callstack. The original callstack data can be
 *  recovered via this ID. If a callstack has already been seen by the
 *  CallstackManager, it'll feed back the original ID.
 */
class CallstackManager
{
public:
    typedef void *callstackid;  // Consider this opaque.

    CallstackManager() : total_frames(0), unique_frames(0) {}
    callstackid add(dumpptr *ptrs, size_t framecount);
    void done_adding(ProgressNotify &pn);
    size_t framecount(callstackid id);
    void get(callstackid id, dumpptr *ptrs);
    size_t getTotalCallstackFrames() { return(total_frames); }
    size_t getUniqueCallstackFrames() { return(unique_frames); }

protected:
    /*
     * This is what the CallstackManager::callstackid type really is; an
     *  opaque pointer to a CallstackNode. The guts aren't accessible to
     *  the application, though: they get the pertinent data via
     *  CallstackManager.
     */
    class CallstackNode
    {
    public:
        CallstackNode(dumpptr _ptr=0, CallstackNode *p=NULL, size_t d=0)
            : ptr(_ptr), depth(d), parent(p), children(NULL), sibling(NULL) {}
        ~CallstackNode();
        dumpptr ptr;
        size_t depth;
        CallstackNode *parent;
        CallstackNode *children;
        CallstackNode *sibling;
    }; // CallstackNode

    CallstackNode root;
    size_t total_frames;
    size_t unique_frames;
}; // CallstackManager


typedef enum
{
    DUMPFILE_OP_NOOP = 0,   /* never shows up in DumpFileOperations */
    DUMPFILE_OP_GOODBYE,    /* never shows up in DumpFileOperations */
    DUMPFILE_OP_MALLOC,
    DUMPFILE_OP_REALLOC,
    DUMPFILE_OP_MEMALIGN,
    DUMPFILE_OP_FREE,
    DUMPFILE_OP_TOTAL       /* never shows up in DumpFileOperations */
} dumpfile_operation_t;


class DumpFile;

/*
 * This class represents one operation (malloc, realloc, free, etc) in
 *  the dumpfile. Basically the end result of parsing a dumpfile is
 *  several views of a collection of operations.
 *
 * As far as your application is concerned, this entire class is READ-ONLY!
 */
class DumpFileOperation
{
public:
    dumpfile_operation_t getOperationType() { return optype; }
    tick_t getTimestamp() { return timestamp; }
    CallstackManager::callstackid getCallstackId() { return callstack; }

    union  /* read only! */
    {
        struct
        {
            dumpptr size;
            dumpptr retval;
        } op_malloc;

        struct
        {
            dumpptr ptr;
            dumpptr size;
            dumpptr retval;
        } op_realloc;

        struct
        {
            dumpptr boundary;
            dumpptr size;
            dumpptr retval;
        } op_memalign;

        struct
        {
            dumpptr ptr;
        } op_free;
    };

protected:
    friend class DumpFile;
    DumpFileOperation *next;
    dumpfile_operation_t optype;
    tick_t timestamp;
    CallstackManager::callstackid callstack;
};



class BadBehaviourList
{
public:
    // !!! FIXME: write this!
};


/*
 * Fragmentation Map tracking...
 *
 * How this works:
 *  We build up "snapshots" that represent the fragmentation map every X
 *  memory operations. We use these sort of like MPEG "I-Frames"...a
 *  snapshot is a complete representation of the memory usage at that moment,
 *  then you can iterate through the memory operations from there to find
 *  a moment's accurate representation fairly efficiently.
 *
 * Snapshots move back and forth as needed; if you request a snapshot, the
 *  FragMapManager will find the closest snapsnot and update it to the
 *  timestamp requested. Over time, the snapshots will be in variable
 *  positions instead of every X operations, but this implementation detail
 *  is hidden from the application.
 *
 * The DumpFile class maintains an instance of FragMapManager. The application
 *  talks to this FragMapManager and requests a snapshot; this is given to the
 *  app as a READ ONLY linked list of FragMapNode objects, ordered by
 *  address of allocated block, lowest to highest. This list is not to be
 *  deallocated or modified by the app, and is guaranteed to be valid until
 *  a new snapshot is requested.
 */


/*
 * These nodes are actually used in two ways; internally, when updating a
 *  snapshot, they represent a B-tree, and "left" and "right" are pointers
 *  to children. When these are used in snapshots for the application, the
 *  tree is flattened into a doubly linked list, in which case "left" and
 *  "right" are used to iterate the linear data set.
 */
class FragMapNode
{
public:
    FragMapNode(dumpptr p=0x00000000, size_t s=0) :
        ptr(p), size(s), left(NULL), right(NULL) {}
    // !!! FIXME: ~FragMapNode();
    dumpptr ptr;
    size_t size;
    FragMapNode *left;
    FragMapNode *right;
};

/*
 * Allocates nodes, and pools them for reuse. NOT THREAD SAFE.
 */
class FragMapNodePool
{
public:
    static inline FragMapNode *get(dumpptr ptr, size_t size);
    static inline void put(FragMapNode *node);
    static void putlist(FragMapNode *node);
    static void flush();
    static FragMapNode *freepool;
};

/*
 * This is used internally by the FragMapManager to keep track of created
 *  snapshots.
 */
class FragMapSnapshot
{
public:
    FragMapSnapshot(uint32 nodecount, size_t opidx);
    ~FragMapSnapshot();
    FragMapNode **nodes;
    size_t total_nodes;
    size_t operation_index;
};


/*
 * The FragMapManager keeps an ongoing working set of the memory space,
 *  stored as a hashtable...this lets us insert and remove allocated blocks
 *  into the FragMap with really good efficiency. We flatten and sort the
 *  hashtable when creating snapshots.
 *
 * The app requests snapshots from the FragMapManager, which, to the app,
 *  is just a linear linked list, sorted by the allocations' pointers.
 */
class FragMapManager
{
public:
    FragMapManager();
    ~FragMapManager();
    void add_malloc(DumpFileOperation *op);
    void add_realloc(DumpFileOperation *op);
    void add_memalign(DumpFileOperation *op);
    void add_free(DumpFileOperation *op);
    void done_adding(ProgressNotify &pn);
    //FragMapNode **get_fragmap(size_t operation_index, size_t &nodecount);

protected:
    FragMapSnapshot **snapshots;
    uint32 total_snapshots;
    void insert_block(dumpptr ptr, size_t s);
    void remove_block(dumpptr ptr);
    void create_snapshot();

private:
    FragMapNode **fragmap;
    size_t total_nodes;
    size_t current_operation;
    size_t snapshot_operations;

    #define FRAGMAP_SNAPSHOT_THRESHOLD 1000
    inline void increment_operations();
    static inline uint16 calculate_hash(dumpptr val);
};


/*
 * This is the application's interface to all the data in a dumpfile.
 *
 * Constructing a dumpfile can take a LOT of processing and allocate a
 *  ton of memory! Since the constructor may block for a long time, it
 *  offers a callback you can use to pump your event queue or give updates.
 */
class DumpFile
{
public:
    DumpFile(const char *fname, ProgressNotify &pn) throw (const char *);
    DumpFile(const char *fname) throw (const char *);
    ~DumpFile();
    uint8 getFormatVersion() { return protocol_version; }
    uint8 platformIsBigendian() { return (byte_order == 1); }
    uint8 platformIsLittleendian() { return (byte_order == 0); }
    uint8 getSizeofPtr() { return sizeofptr; }
    const char *getId() { return id; }
    const char *getBinaryFilename() { return fname; }
    uint32 getProcessId() { return pid; }
    uint32 getOperationCount() { return total_operations; }
    DumpFileOperation *getOperation(size_t idx) { return operations[idx]; }
    CallstackManager callstackManager;
    FragMapManager fragmapManager;

protected:
    uint8 protocol_version; /* dumpfile format version. */
    uint8 byte_order;  /* byte order on original platform, not this one. */
    uint8 sizeofptr;  /* sizeof (void *) on original platform, not this one. */
    char *id;  /* arbitrary id associated with dump: asciz string. */
    char *fname;  /* filename of dump's binary: asciz string. */
    uint32 pid;   /* process ID associated with dump. */
    uint32 total_operations; /* number of Operation objects in this dump. */
    DumpFileOperation **operations; /* the ops in chronological order. */

private:
    void parse(const char *fname, ProgressNotify &pn) throw (const char *);
    void destruct();
    inline void read_block(void *ptr, size_t size) throw (const char *);
    inline void read_ui8(uint8 &ui8) throw (const char *);
    inline void read_ui32(uint32 &ui32) throw (const char *);
    inline void read_ui64(dumpptr &ui64) throw (const char *);
    inline void read_ptr(dumpptr &ptr) throw (const char *);
    inline void read_sizet(dumpptr &sizet) throw (const char *);
    inline void read_timestamp(tick_t &t) throw (const char *);
    inline void read_callstack(CallstackManager::callstackid &id) throw (const char *);
    inline void read_asciz(char *&str) throw (const char *);
    FILE *io;  // used during parsing...
};

#endif

/* end of dumpfile.h ... */

