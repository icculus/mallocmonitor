
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

CallstackManager::callstackid CallstackManager::add(dumpptr *ptrs, size_t framecount)
{
    CallstackNode *parent = &this->root;  // top of tree.
    CallstackNode *node = parent->children;  // root node is placeholder.
    CallstackNode *lastnode = NULL;
    size_t origframecount = framecount;

    // assume everything is coming from main(), so start from the back so
    //  we put it at the top of the tree. This will result in less dupes,
    //  as nodes that have common ancestry will share common nodes.
    ptrs += (framecount - 1);

    total_frames += framecount;

    dumpptr ptr = *ptrs;  // local var so we don't deference on each sibling...
    while ((node != NULL) && (framecount))
    {
        if (node->ptr != ptr)  // non-matching node; check siblings.
        {
            lastnode = node;
            node = node->sibling;
        } // if
        else  // matches, check next level...
        {
            // Move this node to the start of the list so frequently-used
            //  nodes bubble to the top...
            if (lastnode != NULL)
            {
                lastnode->sibling = node->sibling;
                node->sibling = parent->children;
                parent->children = node;
                lastnode = NULL;
            } // if

            ptrs--;
            framecount--;
            parent = node;
            node = node->children;
            ptr = *ptrs;
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


void CallstackManager::get(callstackid id, dumpptr *ptrs)
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

#if 0
FragMapManager::FragMapManager()
    : snapshots(NULL), total_snapshots(0), fragmap(NULL)
{
} // FragMapManager constructor

FragMapManager::~FragMapManager()
{
    delete fragmap;
    delete snapshots;
} // FragMapManager destructor


void FragMapManager::insert_block(FragMapNode *insnode)
{
    dumpptr ptr = insnode->ptr;
    FragMapNode *node = fragmap;
    FragMapNode **child = NULL;

    while (node)
    {
        dumpptr nodeptr = node->ptr;
        assert(ptr != nodeptr);
        child = (ptr < nodeptr) ? &node->left : &node->right;
        if (*child == NULL)  // if null, then this is our insertion point!
        {
            *child = insnode;
            return;
        } // if

        // keep looking...
        node = *child;
    } // while

    // you should only be here if the fragmap is totally empty.
    assert(fragmap == NULL);
    fragmap = insnode;
} // FragMapManager::insert_block


void FragMapManager::insert_block(dumpptr ptr, size_t size)
{
    insert_block(new FragMapNode(ptr, size));
} // FragMapManager::insert_block


FragMapNode *FragMapManager::find_block(dumpptr ptr, FragMapNode *node)
{
    while (node)
    {
        dumpptr nodeptr = node->ptr;
        if (ptr == nodeptr)
            return(node);  // we have a match!
        else
            node = (ptr < nodeptr) ? node->left : node->right;
    } // while

    return(NULL);
} // FragMapManager::find_block


void FragMapManager::remove_block(dumpptr ptr)
{
    FragMapNode *node = fragmap;
    FragMapNode *parent = NULL;

    if ((node) && (node->ptr == ptr))  // the root node is to be removed...
    {
        fragmap = node->left;
        if (node->right)
            insert_block(node->right);
        delete node;
        return;
    } // if

    while (node)
    {
        dumpptr nodeptr = node->ptr;
        if (ptr != nodeptr)  // not our node, keep looking...
        {
            parent = node;
            node = (ptr < nodeptr) ? node->left : node->right;
        } // if

        else  // This is the node; remove it.
        {
            if (ptr < parent->ptr)
            {
                parent->left = node->left;
                if (node->right)
                    insert_block(node->right);
            } // if
            else
            {
                parent->right = node->right;
                if (node->left)
                    insert_block(node->left);
            } // else

            delete node;
            return;
        } // else
    } // while
} // FragMapManager::remove_block
#endif


inline void FragMapManager::delete_node(FragMapNode *node)
{
    node->right = freepool;
    freepool = node;
} // FragMapManager::delete_node


inline FragMapNode *FragMapManager::allocate_node(dumpptr ptr, size_t size)
{
    FragMapNode *retval = freepool;
    if (retval == NULL)
        retval = new FragMapNode(ptr, size);
    else
    {
        freepool = retval->right;
        retval->ptr = ptr;
        retval->size = size;
    } // else

    return(retval);
} // FragMapManager::allocate_node


void FragMapManager::delete_nodelist(FragMapNode *node)
{
    FragMapNode *next;
    while (node)
    {
        next = node->right;
        delete node;
        node = next;
    } // while
} // FragMapManager::delete_nodelist



FragMapManager::FragMapManager()
    : snapshots(NULL), total_snapshots(0), fragmap(NULL), freepool(NULL)
{
    fragmap = new FragMapNode*[0xFFFF + 1];
    memset(fragmap, '\0', (0xFFFF + 1) * sizeof (FragMapNode *));
} // FragMapManager constructor


FragMapManager::~FragMapManager()
{
    delete_nodelist(freepool);

    for (size_t i = 0; i < 0xFFFF; i++)
        delete_nodelist(fragmap[i]);

    delete[] fragmap;
    delete snapshots;
} // FragMapManager destructor


inline uint16 FragMapManager::calculate_hash(dumpptr ptr)
{
#if 0
    uint16 retval = ((ptr & 0xFFFF) |
                     ((ptr & (1 << 17)) >> 17) |
                     ((ptr & (1 << 18)) >> 17));
#elif 0
    uint16 retval = (((ptr & 0x55555555) >> 1) ^ ptr) & 0xFFFF;
#elif 1   // this seems to give the best distribution...
    uint16 retval = (((ptr & 0xFFFF0000) >> 16) ^ ptr) & 0xFFFF;
#else
#error Please define a hash function...
#endif

    return(retval);
} // calculate_hash


void FragMapManager::insert_block(dumpptr ptr, size_t size)
{
    uint16 hashval = calculate_hash(ptr);
    FragMapNode *node = allocate_node(ptr, size);
    node->right = fragmap[hashval];  // FIXME: do this in the constructor.
    fragmap[hashval] = node;
} // FragMapManager::insert_block


void FragMapManager::remove_block(dumpptr ptr)
{
    uint16 hashval = calculate_hash(ptr);
    FragMapNode *node = fragmap[hashval];
    FragMapNode *prev = NULL;
    while ((node != NULL) && (node->ptr != ptr))
    {
        prev = node;
        node = node->right;
    } // while

    if (node != NULL)
    {
        if (prev != NULL)
            prev->right = node->right;
        else
            fragmap[hashval] = node->right;
        delete_node(node);
    } // if
} // FragMapManager::remove_block


void FragMapManager::add_malloc(DumpFileOperation *op)
{
    insert_block(op->op_malloc.retval, op->op_malloc.size);
} // FragMapManager::add_malloc


void FragMapManager::add_realloc(DumpFileOperation *op)
{
    // !!! FIXME: Is realloc(NULL, 0) illegal?

    // !!! FIXME: Don't remove and reinsert if ptr == rc && size > 0...
    if (op->op_realloc.ptr)
        remove_block(op->op_realloc.ptr);

    if (op->op_realloc.size)
        insert_block(op->op_realloc.retval, op->op_realloc.size);
} // FragMapManager::add_realloc


void FragMapManager::add_memalign(DumpFileOperation *op)
{
    insert_block(op->op_memalign.retval, op->op_memalign.size);
} // FragMapManager::add_memalign


void FragMapManager::add_free(DumpFileOperation *op)
{
    remove_block(op->op_free.ptr);
} // FragMapManager::add_free


void FragMapManager::done_adding(ProgressNotify &pn)
{
    // !!! FIXME: flatten out final fragmap?

#if 1  // PROFILING_STATISTICS
    int deepest = 0;
    int totalnodes = 0;

    #define MAXDEPTHS 16
    int depths[MAXDEPTHS];
    memset(depths, '\0', sizeof (depths));

    for (size_t i = 0; i < 0xFFFF; i++)
    {
        FragMapNode *node = fragmap[i];
        int depth = 0;
        while (node)
        {
            totalnodes++;
            depth++;
            node = node->right;
        }

        if (depth < MAXDEPTHS)
            depths[depth]++;

        if (depth > deepest)
            deepest = depth;
    }

    fprintf(stderr, "fragmap total == %d, deepest == %d, empties == %d\n",
            totalnodes, deepest, depths[0]);

    if (deepest >= MAXDEPTHS)
        deepest = MAXDEPTHS - 1;
    for (int i = 1; i <= deepest; i++)
        fprintf(stderr, " %d: %d\n", i, depths[i]);
#endif
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

inline void DumpFile::read_ui64(dumpptr &ui64) throw (const char *)
{
    read_block(&ui64, sizeof (ui64));
    if (byte_order != platform_byteorder)
        BYTESWAP64(ui64);
} // DumpFile::read_ui64

inline void DumpFile::read_ptr(dumpptr &ptr) throw (const char *)
{
#if SUPPORT_64BIT_CLIENTS
    if (sizeofptr == 4)
    {
        uint32 ui32;
        read_ui32(ui32);
        ptr = (dumpptr) ui32;
    } // if
    else
    {
        read_ui64(ptr);
    } // else
#else
    uint32 ui32;
    read_ui32(ui32);
    ptr = (dumpptr) ui32;
#endif
} // DumpFile::read_ui32

inline void DumpFile::read_sizet(dumpptr &sizet) throw (const char *)
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
    dumpptr *buf = NULL;
    uint32 count;

    read_ui32(count);
    if (count)
    {
        buf = (dumpptr *) alloca(sizeof (dumpptr)/*sizeofptr*/ * count);
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

        // rebuild with dumpptr defined to something bigger...
        if (sizeofptr > sizeof (dumpptr))
            throw("This build doesn't support this dumpfile's pointer size");

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
                        fragmapManager.add_malloc(op);
                        break;

                    case DUMPFILE_OP_REALLOC:
                        //printf("realloc\n");
                        read_ptr(op->op_realloc.ptr);
                        read_sizet(op->op_realloc.size);
                        read_ptr(op->op_realloc.retval);
                        fragmapManager.add_realloc(op);
                        break;

                    case DUMPFILE_OP_MEMALIGN:
                        //printf("memalign\n");
                        read_sizet(op->op_memalign.boundary);
                        read_sizet(op->op_memalign.size);
                        read_ptr(op->op_memalign.retval);
                        fragmapManager.add_memalign(op);
                        break;

                    case DUMPFILE_OP_FREE:
                        //printf("free\n");
                        read_sizet(op->op_free.ptr);
                        fragmapManager.add_free(op);
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
        fragmapManager.done_adding(pn);

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

