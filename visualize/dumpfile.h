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
    size_t callstack_index;
};


class DumpFile
{
public:
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
    uint64 **getCallstack(DumpFileOperation &op)
    {
        //size_t idx = op.callstack_index;
        //if (idx > total_callstacks)
        //    return(NULL);
        return callstacks[op.callstack_index];
    } // getCallstack

protected:
    uint8 protocol_version; /* dumpfile format version. */
    uint8 byte_order;  /* byte order on original platform, not this one. */
    uint8 sizeofptr;  /* sizeof (void *) on original platform, not this one. */
    char *id;  /* arbitrary id associated with dump: asciz string. */
    char *fname;  /* filename of dump's binary: asciz string. */
    uint32 pid;   /* process ID associated with dump. */
    uint32 total_operations; /* number of Operation objects in this dump. */
    DumpFileOperation **operations; /* the ops in chronological order. */
    uint32 total_callstacks; /* number of callstack objects in this dump. */
    uint64 ***callstacks;  /* actual callstacks. */

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

