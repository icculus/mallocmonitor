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


typedef void (*DumpFileProgress)(const char *status, int percent);

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


class CallstackNode
{
public:
    CallstackNode(uint64 _ptr=0, CallstackNode *p=NULL, size_t d=0)
        : ptr(_ptr), depth(d), parent(p), children(NULL), sibling(NULL) {}
    ~CallstackNode();
    uint64 ptr;
    size_t depth;
    CallstackNode *parent;
    CallstackNode *children;
    CallstackNode *sibling;
}; // CallstackNode


class CallstackManager
{
public:
    typedef void *callstackid;  // Consider this opaque.

    callstackid add(uint64 *ptrs, size_t framecount);
    void done_adding(DumpFileProgress dfp);
    size_t framecount(callstackid id);
    void get(callstackid id, uint64 *ptrs);

protected:
    CallstackNode root;
}; // CallstackManager


class BadBehaviourList
{
public:
    // !!! FIXME: write this!
};


// Fragmentation Map tracking...

// How this works:
//  We build up "snapshots" that represent the fragmentation map every X
//  memory operations. We use these sort of like MPEG "I-Frames"...a
//  snapshot is a complete representation of the memory usage at that moment,
//  then you can iterate through the memory operations from there to find
//  a moment's accurate representation fairly efficiently.
//
// Snapshots move back and forth as needed; if you request a snapshot, the
//  FragMapManager will find the closest snapsnot and update it to the
//  timestamp requested. Over time, the snapshots will be in variable
//  positions instead of every X operations.
//
// !!! FIXME: (this isn't implemented yet)
class FragMapNode
{
public:
    uint64 ptr;
    size_t size;
};

class FragMapSnapshot
{
public:
    FragMapNode *nodes;
    size_t total_nodes;
};

class FragMapManager
{
public:
    void add_malloc(size_t size, uint64 rc);
    void add_realloc(uint64 ptr, size_t size, uint64 rc);
    void add_memalign(size_t b, size_t a, uint64 rc);
    void add_free(uint64 ptr);
    void done_adding(DumpFileProgress dfp);

protected:
    FragMapSnapshot *snapshots;
    size_t total_snapshots;

private:
    // !!! FIXME: hashtable goes here.  :/
};


class DumpFile;

/*
 * This entire class is READ-ONLY!
 */
class DumpFileOperation
{
public:
    dumpfile_operation_t getOperationType() { return optype; }
    tick_t getTimestamp() { return timestamp; }

    union  /* read only! */
    {
        struct
        {
            uint64 size;
            uint64 retval;
        } op_malloc;

        struct
        {
            uint64 ptr;
            uint64 size;
            uint64 retval;
        } op_realloc;

        struct
        {
            uint64 boundary;
            uint64 size;
            uint64 retval;
        } op_memalign;

        struct
        {
            uint64 ptr;
        } op_free;
    };

protected:
    friend class DumpFile;
    DumpFileOperation *next;
    dumpfile_operation_t optype;
    tick_t timestamp;
    CallstackManager::callstackid callstack;
};


/*
 * Constructing a dumpfile can take a LOT of processing and allocate a
 *  ton of memory! Since the constructor may block for a long time, it
 *  offers a callback you can use to pump your event queue or give updates.
 */
class DumpFile
{
public:
    DumpFile(const char *fname, DumpFileProgress dfp=NULL) throw (const char *);
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
    CallstackManager callstacks;

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
    void destruct();
    inline void read_block(void *ptr, size_t size) throw (const char *);
    inline void read_ui8(uint8 &ui8) throw (const char *);
    inline void read_ui32(uint32 &ui32) throw (const char *);
    inline void read_ui64(uint64 &ui64) throw (const char *);
    inline void read_ptr(uint64 &ptr) throw (const char *);
    inline void read_sizet(uint64 &sizet) throw (const char *);
    inline void read_timestamp(tick_t &t) throw (const char *);
    inline void read_callstack() throw (const char *);
    inline void read_asciz(char *&str) throw (const char *);
    FILE *io;
};

#endif

/* end of dumpfile.h ... */

