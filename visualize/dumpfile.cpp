
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
    ptrs += (framecount - 1);

    total_frames += framecount;

    while ((node != NULL) && (framecount))
    {
        if (node->ptr != *ptrs)  // non-matching node; check siblings.
            node = node->sibling;
        else  // matches, check next level...
        {
            // !!! FIXME: move matching node to front of list...
            ptrs--;
            framecount--;
            parent = node;
            node = node->children;
        } // else
    } // while

    // (framecount == 0) here means a complete match with existing branch.

    unique_frames += framecount;

    while (framecount)  // build any missing nodes...
    {
        node = new CallstackNode(*ptrs, parent, (origframecount-framecount)+1);
        node->sibling = parent->children;
        parent->children = node;
        parent = node;
        ptrs--;
        framecount--;
    } // if

    if (node == NULL)
        return((callstackid) &root);

    return((callstackid) node);   // bottom of the callstack ("main", etc).
} // CallstackManager::add


void CallstackManager::done_adding(ProgressNotify &pn)
{
    // no-op in this implementation
} // CallstackManager::done_adding


size_t CallstackManager::framecount(callstackid id)
{
    return( ((CallstackNode *) id)->depth);
} // CallstackManager::framecount


void CallstackManager::get(callstackid id, uint64 *ptrs)
{
    CallstackNode *node = (CallstackNode *) id;
    size_t depth = node->depth;
    while (depth--)
    {
        *ptrs = node->ptr;
        ptrs++;
        node = node->parent;
    } // while
} // CallstackManager::get


// CallstackManager automatically deletes the root node, which causes the
//  whole tree to collapse.
CallstackManager::CallstackNode::~CallstackNode()
{
    CallstackNode *kid = this->children;
    while (kid)
    {
        CallstackNode *next = kid->sibling;
        delete kid;
        kid = next;
    } // while
} // CallstackNode destructor


FragMapManager::~FragMapManager()
{
    delete snapshots;
} // FragMapManager destructor


void FragMapManager::insert_block(uint64 ptr, size_t size)
{
    if (ptr == 0x00000000)
        return;

    // !!! FIXME: malloc(0) is a legitimate call on Linux (ANSI?), apparently.

    FragMapNode *node = &fragmap;
    FragMapNode **child = NULL;

    while (node)
    {
        uint64 nodeptr = node->ptr;

        if (ptr == nodeptr)   // revive a dead node?
        {
            assert(node->dead);
            node->dead = false;
            node->size = size;
            return;
        } // if

        child = (ptr < nodeptr) ? &node->left : &node->right;
        if (*child == NULL)  // if null, then this is our insertion point!
        {
            *child = new FragMapNode(ptr, size);  // !!! FIXME: pool these!
            return;
        } // if

        // keep looking...
        node = *child;
    } // while

    assert(false);  // this should never happen.
} // FragMapManager::insert_block


FragMapNode *FragMapManager::find_block(uint64 ptr, FragMapNode *node)
{
    if (node == NULL)
        return(NULL);

    if (ptr == node->ptr)  // found?
        return(node);

    // recursive!
    FragMapNode *child = find_block(ptr, node->left);
    if (child != NULL)
        return(child);

    return(find_block(ptr, node->right));   // recursive!
} // FragMapManager::find_block


void FragMapManager::remove_block(uint64 ptr)
{
    if (ptr == 0x00000000)
        return;  // don't delete static root.

    FragMapNode *node = find_block(ptr);

    // "Removing" a node means flagging it as "dead".
    //  This is because adjusting the tree is complex and nasty,
    //  and it's likely a future allocation will reuse this address
    //  anyhow. This also lets us consider double-free()s, etc.
    // We can cull the dead nodes when rebalancing the tree, and
    //  put them into an allocation pool for reuse.
    if (node)
        node->dead = true;
} // FragMapManager::remove_block


void FragMapManager::add_malloc(size_t size, uint64 rc)
{
    insert_block(rc, size);
} // FragMapManager::add_malloc


void FragMapManager::add_realloc(uint64 ptr, size_t size, uint64 rc)
{
    // !!! FIXME: Is realloc(NULL, 0) illegal?

    // !!! FIXME: Don't remove and reinsert if ptr == rc && size > 0...
    if (ptr)
        remove_block(ptr);

    if (size)
        insert_block(rc, size);
} // FragMapManager::add_realloc


void FragMapManager::add_memalign(size_t b, size_t a, uint64 rc)
{
    insert_block(rc, a);
} // FragMapManager::add_memalign


void FragMapManager::add_free(uint64 ptr)
{
    remove_block(ptr);
} // FragMapManager::add_free


void FragMapManager::done_adding(ProgressNotify &pn)
{
    // !!! FIXME: flatten out final fragmap?
} // FragMapManager::done_adding



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

inline void DumpFile::read_callstack(CallstackManager::callstackid &id)
    throw (const char *)
{
    uint64 *buf = NULL;
    uint32 count;

    read_ui32(count);
    if (count)
    {
        buf = (uint64 *) alloca(sizeof (uint64)/*sizeofptr*/ * count);
//        read_block(buf, count * sizeofptr);
        for (uint32 i = 0; i < count; i++)
            read_ptr(buf[i]);
    } // if

    id = callstackManager.add(buf, count);
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


DumpFile::DumpFile(const char *fn, ProgressNotify &pn) throw (const char *)
{
    parse(fn, pn);
} // DumpFile constructor

DumpFile::DumpFile(const char *fn) throw (const char *)
{
    ProgressNotifyDummy pnd;
    parse(fn, pnd);
} // DumpFile constructor


void DumpFile::parse(const char *fn, ProgressNotify &pn) throw (const char *)
{
    // set sane initial state...
    fname = NULL;
    id = NULL;
    total_operations = 0;
    operations = NULL;
    io = NULL;

    platform_byteorder = is_bigendian();

    try
    {
        io = fopen(fn, "rb");
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
                        //fprintf(stderr, "bogus opcode: %d\n", (int) optype);
                        bogus_data = true;
                        break;
                } // switch

                read_callstack(op->callstack);
            } // try

            catch (const char *e)  // half-written records are possible!
            {
                delete op;  // nuke what was half-written, if anything.
                break;  // break loop, we're done.
            } // catch

            prevop->next = op;
            prevop = op;
            total_operations++;

            pn.update( "Parsing raw data",
                       (int) ((((double) ftell(io)) / fsize) * 100.0) );
        } // while

        callstackManager.done_adding(pn);

        if (op != NULL)
            op->next = NULL;

        op = dummyop.next;
        operations = new DumpFileOperation*[total_operations];
        for (size_t i = 0; i < total_operations; i++)
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
} // DumpFile::construct

// end of dumpfile.cpp ...

