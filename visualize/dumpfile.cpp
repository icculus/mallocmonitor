
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <unistd.h>
#include <sys/stat.h>
#include <assert.h>

#include "dumpfile.h"

static int platform_byteorder = 0;
static inline int is_bigendian(void)
{
    uint32 x = 0x01000000;
    return(*((uint8 *) &x));
} // is_bigendian


#define BYTESWAP32(x) throw("byteswapping not implemented yet!")
#define BYTESWAP64(x) throw("byteswapping not implemented yet!")


CallstackManager::callstackid CallstackManager::add(uint64 *ptrs, size_t framecount)
{
    CallstackNode *parent = &this->root;  // top of tree.
    CallstackNode *node = parent->children;  // root node is placeholder.
    size_t origframecount = framecount;

    // assume everything is coming from main(), so start from the back so
    //  we put it at the top of the tree. This will result in less dupes,
    //  as nodes that have common ancestry will share common nodes.
    ptrs += framecount;

    while ((node != NULL) && (framecount))
    {
        if (node->ptr != *ptrs)  // non-matching node; check siblings.
            node = node->sibling;
        else  // matches, check next level...
        {
            ptrs--;
            framecount--;
            parent = node;
            node = node->children;
        } // else
    } // while

    // framecount==0 means complete match with existing branch.
    assert((framecount == 0) || (node == NULL));

    while (framecount)  // build any missing nodes...
    {
        node = new CallstackNode(*ptrs, parent, origframecount - framecount);
        node->sibling = parent->children;
        parent->children = node;
        parent = node;
        ptrs--;
        framecount--;
    } // if

    return((callstackid) node);   // bottom of the callstack ("main", etc).
} // CallstackManager::add


void CallstackManager::done_adding(DumpFileProgress dfp)
{
    // no-op in this implementation
} // CallstackManager::done_adding


size_t CallstackManager::framecount(callstackid id)
{
    return(((CallstackNode *) id)->depth);
} // CallstackManager::framecount


void CallstackManager::get(callstackid id, uint64 *ptrs)
{
    CallstackNode *node = (CallstackNode *) id;
    while (node)
    {
        *ptrs = node->ptr;
        ptrs++;
        node = node->parent;
    } // while
} // CallstackManager::get


// CallstackManager automatically deletes the root node, which causes the
//  whole tree to collapse.
CallstackNode::~CallstackNode()
{
    CallstackNode *kid = this->children;
    while (kid)
    {
        CallstackNode *next = kid->sibling;
        delete kid;
        kid = next;
    } // while
} // CallstackNode destructor


void DumpFile::destruct(void)
{
    if (io != NULL)
        fclose(io);
    io = NULL;

    delete[] id;
    id = NULL;

    delete[] fname;
    fname = NULL;

    for (size_t i = 0; i < total_operations; i++)
        delete operations[i];
    delete[] operations;
    operations = NULL;
    total_operations = 0;
} // DumpFile::Destruct


DumpFile::~DumpFile(void)
{
    destruct();
} // DumpFile::~DumpFile



inline void DumpFile::read_block(void *ptr, size_t size) throw (const char *)
{
    if (fread(ptr, size, 1, io) != 1) throw(strerror(errno));
} // DumpFile::read_block

void DumpFile::read_ui8(uint8 &ui8) throw (const char *)
{
    read_block(&ui8, sizeof (ui8));
} // DumpFile::read_ui8

inline void DumpFile::read_ui32(uint32 &ui32) throw (const char *)
{
    read_block(&ui32, sizeof (ui32));
    if (byte_order != platform_byteorder)
        BYTESWAP32(ui32);
} // DumpFile::read_ui32

inline void DumpFile::read_ui64(uint64 &ui64) throw (const char *)
{
    read_block(&ui64, sizeof (ui64));
    if (byte_order != platform_byteorder)
        BYTESWAP64(ui64);
} // DumpFile::read_ui64

inline void DumpFile::read_ptr(uint64 &ptr) throw (const char *)
{
    if (sizeofptr == 4)
    {
        uint32 ui32;
        read_ui32(ui32);
        ptr = (uint64) ui32;
    } // if
    else
    {
        read_ui64(ptr);
    } // else
} // DumpFile::read_ui32

inline void DumpFile::read_sizet(uint64 &sizet) throw (const char *)
{
    read_ptr(sizet);
} // DumpFile::read_sizet

inline void DumpFile::read_timestamp(tick_t &t) throw (const char *)
{
    read_ui32(t);
} // DumpFile::read_timestamp

inline void DumpFile::read_callstack() throw (const char *)
{
    uint64 buf[1024];
    uint32 count;

    read_ui32(count);

    // !!! FIXME:
    if (count >= 1024)
        throw("Buffer overflow");
    if (count)
        read_block(buf, count * sizeofptr);
} // read_callstack

inline void DumpFile::read_asciz(char *&str) throw (const char *)
{
    // inefficient, but who cares? It's only used twice in the header!
    uint8 buf[1024];
    size_t i;
    for (i = 0; i < sizeof (buf); i++)
    {
        read_ui8(buf[i]);
        if (buf[i] == 0)
            break;
    } // for

    if (i >= sizeof (buf))
        throw("Buffer overflow");

    str = new char[i+1];
    strcpy(str, (char *) buf);
} // read_asciz


DumpFile::DumpFile(const char *fname, DumpFileProgress dfp) throw (const char *)
    : id(NULL),
      fname(NULL),
      total_operations(0),
      operations(NULL),
      io(NULL)
{
    platform_byteorder = is_bigendian();

    try
    {
        io = fopen(fname, "rb");
        if (io == NULL)
            throw ((const char *) strerror(errno));

        double fsize;
        struct stat statbuf;
        if (fstat(fileno(io), &statbuf) == -1)
            throw ((const char *) strerror(errno));

        if (statbuf.st_size == 0)
            throw ("File is empty");

        fsize = (double) statbuf.st_size;

        char sigbuf[16];
        read_block(sigbuf, sizeof (sigbuf));
        if (strcmp(sigbuf, "Malloc Monitor!") != 0)
            throw("Not a Malloc Monitor dumpfile");
        read_ui8(protocol_version);
        if (protocol_version != 1)
            throw("Unknown dumpfile format version");

        read_ui8(byte_order);
        read_ui8(sizeofptr);
        read_asciz(id);
        read_asciz(this->fname);
        read_ui32(pid);

        uint8 optype;
        DumpFileOperation dummyop;
        DumpFileOperation *op;
        DumpFileOperation *prevop = &dummyop;
        bool bogus_data = false;

        while (!bogus_data)
        {
            op = NULL;
            try
            {
                read_ui8(optype);
                if (optype == DUMPFILE_OP_GOODBYE)
                    break;
                else if (optype == DUMPFILE_OP_NOOP)
                    continue;

                op = new DumpFileOperation;
                op->optype = (dumpfile_operation_t) optype;
                read_timestamp(op->timestamp);
                switch (optype)
                {
                    case DUMPFILE_OP_MALLOC:
//printf("malloc\n");
                        read_sizet(op->op_malloc.size);
                        read_ptr(op->op_malloc.retval);
                        break;

                    case DUMPFILE_OP_REALLOC:
//printf("realloc\n");
                        read_ptr(op->op_realloc.ptr);
                        read_sizet(op->op_realloc.size);
                        read_ptr(op->op_realloc.retval);
                        break;

                    case DUMPFILE_OP_MEMALIGN:
//printf("memalign\n");
                        read_sizet(op->op_memalign.boundary);
                        read_sizet(op->op_memalign.size);
                        read_ptr(op->op_memalign.retval);
                        break;

                    case DUMPFILE_OP_FREE:
//printf("free\n");
                        read_sizet(op->op_free.ptr);
                        break;

                    default:
printf("bogus opcode: %d\n", (int) optype);
                        bogus_data = true;
                        break;
                } // switch

                read_callstack();
            } // try

            catch (const char *e)  // half-written records are possible!
            {
                delete op;  // nuke what was half-written, if anything.
                break;  // break loop, we're done.
            } // catch

            prevop->next = op;
            prevop = op;
            total_operations++;

            if (dfp)
            {
                dfp( "Parsing raw data",
                     (int) ((((double) ftell(io)) / fsize) * 100.0) );
            } // if
        } // while

        callstacks.done_adding(dfp);

        if (op != NULL)
            op->next = NULL;

        op = dummyop.next;
        operations = new DumpFileOperation*[total_operations];
        for (size_t i; i < total_operations; i++)
        {
            operations[i] = op;
            op = op->next;
        } // for

        if (bogus_data)
            throw("Unexpected or corrupted data in dumpfile!");
    } // try

    catch (const char *e)
    {
        destruct();
        throw(e);
    } // catch

    if (io != NULL)
    {
        fclose(io);
        io = NULL;
    } // if
} // Constructor

// end of dumpfile.cpp ...

