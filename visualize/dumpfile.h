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
 * This is used internally by the FragMapManager to keep track of created
 *  snapshots.
 */
class FragMapSnapshot
{
public:
    FragMapNode *nodes;
    size_t total_nodes;
    size_t operation_index;
};


/*
 * The FragMapManager keeps an ongoing working set of the memory space,
 *  which is just a B-Tree...this lets us insert and remove allocated blocks
 *  into the FragMap with pretty good efficiency, and makes it trivial to
 *  flatten the tree into a sorted linked list quickly when making
 *  snapshots. A hashtable might be faster for insertion and lookup, in this
 *  case, but the free sorting we get when flattening the tree is pretty much
 *  unbeatable.
 *
 * The typical application usage patterns for memory seem to suggest that
 *  a large portion of the allocations are either extremely short lived
 *  (allocate scratch memory, work in it, free it, possibly in the same
 *  function), or permanent (allocate an object at init time, delete it
 *  during process termination). This means that our tree is going to
 *  quickly become lopsided, with a few allocations to the left, and
 *  most to the right. It is worth rebalancing the tree every few snapshots
 *  to keep insertions and lookups fast.
 *
 * Obviously, trees aren't a good representation for the application, since
 *  they'll want the memory layout from beginning to end in a linear order,
 *  easy to iterate over, which is the entire purpose of flattening the tree
 *  at snapshot time.
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

protected:
    FragMapSnapshot *snapshots;
    size_t total_snapshots;
    void insert_block(dumpptr ptr, size_t s);
    void remove_block(dumpptr ptr);

private:
    FragMapNode **fragmap;

    static inline uint16 calculate_hash(dumpptr val);
    static void delete_nodelist(FragMapNode *node);
    inline FragMapNode *allocate_node(dumpptr ptr, size_t s);
    inline void delete_node(FragMapNode *node);
    FragMapNode *freepool;
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

