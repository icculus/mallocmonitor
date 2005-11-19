/*
 * Turns a dumpfile into meaningful information.
 *
 * Written by Ryan C. Gordon (icculus@icculus.org)
 *
 * Please see the file LICENSE in the source's root directory.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <unistd.h>
#include <sys/stat.h>
#include <assert.h>

#include "dumpfile.h"

#if _HAVE_ASM_BYTEORDER_H_
#include <asm/byteorder.h>
#endif

static int platform_byteorder = 0;
static inline int is_bigendian(void)
{
    uint32 x = 0x01000000;
    return(*((uint8 *) &x));
} // is_bigendian


inline void BYTESWAP16(uint16 &x)
{
    #ifdef __arch__swab16
    x = __arch__swab16(x);
    #else
    x = ( (x << 8) | (x >> 8) );
    #endif
} // BYTESWAP16

inline void BYTESWAP32(uint32 &x)
{
    #ifdef __arch__swab32
    x = __arch__swab32(x);
    #else
    x = ((x<<24) | ((x<<8) & 0x00FF0000) | ((x>>8) & 0x0000FF00) | (x>>24));
    #endif
} // BYTESWAP16

inline void BYTESWAP64(uint64 &x)
{
    #ifdef __arch__swab64
    x = __arch__swab64(x);
    #else
    uint32 lo = (uint32) (x & 0xFFFFFFFF);
    BYTESWAP32(lo);
    uint32 hi = (uint32) ((x >> 32) & 0xFFFFFFFF);
    BYTESWAP32(hi);
    x = ( (((uint64) lo) << 32) | ((uint64) hi) );
    #endif
} // BYTESWAP64


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


FragMapSnapshot::FragMapSnapshot(uint32 nodecount, size_t opidx)
    : total_nodes(nodecount), operation_index(opidx)
{
    nodes = new FragMapNode*[total_nodes];
} // FragMapSnapshot::FragMapSnapshot


FragMapSnapshot::~FragMapSnapshot()
{
    for (size_t i = 0; i < total_nodes; i++)
        FragMapNodePool::put(nodes[i]);
    delete[] nodes;
} // FragMapSnapshot::~FragMapSnapshot


inline void FragMapManager::hash_snapshot(FragMapSnapshot *snapshot)
{
    size_t max = snapshot->total_nodes;
    FragMapNode **node = snapshot->nodes;
    for (size_t i = 0; i < max; i++, node++)
        insert_block((*node)->ptr, (*node)->size);
} // FragMapManager::hash_snapshot


void FragMapManager::walk_fragmap(DumpFile *df, size_t startop, size_t endop)
{
    if (endop >= df->getOperationCount())  // !!! FIXME: This is catching a bug...
        endop = df->getOperationCount()-1;

    for (size_t i = startop; i <= endop; i++)
    {
        DumpFileOperation *op = df->getOperation(i);
        switch (op->getOperationType())
        {
            case DUMPFILE_OP_MALLOC: hash_malloc(op); break;
            case DUMPFILE_OP_REALLOC: hash_realloc(op); break;
            case DUMPFILE_OP_FREE: hash_free(op); break;
            default: assert(0 && "unknown dumpfile operation!"); break;
        } // switch
    } // for
} // FragMapManager::walk_fragmap


// !!! FIXME: this code sucks.
FragMapNode **FragMapManager::get_fragmap(DumpFile *df, size_t op_index, size_t &nodecount)
{
    // Find the closest snapshot...
    // !!! FIXME: Linear search is slow...
    uint32 i;
    FragMapSnapshot *ss = NULL;
    size_t thisop = 0;

    // clamp the value if it's past the end of the dumpfile...
    if (op_index >= df->getOperationCount())
        op_index = df->getOperationCount()-1;

    for (i = 0; i < total_snapshots; i++)
    {
        ss = snapshots[i];
        thisop = ss->operation_index;
        if (thisop == op_index)  // exact match!
        {
            nodecount = ss->total_nodes;
            return(ss->nodes);
        } // if

        if (thisop > op_index)  // we passed it.
            break;
    } // for

    // delete this snapshot, since we're going to replace it with a new
    //  one based on the previous snapshot walked to the requested position.
    delete ss;
    empty_hashtable();  // clear out anything that's sitting around.

    // hash the closest previous snapshot so we can walk from there to the
    //  requested position. If there isn't a previous, walk from operation 0.
    if (i > 0)
    {
        hash_snapshot(snapshots[i-1]);
        thisop = snapshots[i-1]->operation_index;
    } // if
    walk_fragmap(df, op_index, thisop);
    ss = snapshots[i] = create_snapshot();  // turn hash into new snapshot.
    nodecount = ss->total_nodes;
    return(ss->nodes);
} // FragMapManager::get_fragmap


#define FRAGMAPMANAGER_QUICKSORT_THRESHOLD 4

static inline int cmpfn(FragMapNode *a, FragMapNode *b)
{
    return(b->ptr - a->ptr);
} // cmpfn

static inline void swapfn(FragMapNode **a, uint32 e1, uint32 e2)
{
    FragMapNode *tmp = a[e1];
    a[e1] = a[e2];
    a[e2] = tmp;
} // swapfn

void FragMapManager::bubble_sort(FragMapNode **a, uint32 lo, uint32 hi)
{
    uint32 i;
    bool sorted;

    do
    {
        sorted = true;
        for (i = lo; i < hi; i++)
        {
            if (cmpfn(a[i], a[i+1]) > 0)
            {
                swapfn(a, i, i+1);
                sorted = false;
            } /* if */
        } /* for */
    } while (!sorted);
} // FragMapManager::bubble_sort


void FragMapManager::quick_sort(FragMapNode **a, uint32 lo, uint32 hi)
{
    uint32 i;
    uint32 j;
    uint32 v;

    if ((hi - lo) <= FRAGMAPMANAGER_QUICKSORT_THRESHOLD)
        bubble_sort(a, lo, hi);
    else
    {
        i = (hi + lo) >> 1;
        if (cmpfn(a[lo], a[i]) > 0) swapfn(a, i, lo);
        if (cmpfn(a[lo], a[hi]) > 0) swapfn(a, lo, hi);
        if (cmpfn(a[i], a[hi]) > 0) swapfn(a, i, hi);

        j = hi - 1;
        swapfn(a, j, i);
        i = lo;
        v = j;
        while (1)
        {
            while (cmpfn(a[++i], a[v]) < 0) { /* do nothing */ }
            while (cmpfn(a[--j], a[v]) > 0) { /* do nothing */ }
            if (j < i) break;
            swapfn(a, i, j);
        } // while
        swapfn(a, i, hi-1);
        quick_sort(a, lo, j);
        quick_sort(a, i+1, hi);
    } // else
} // FragMapManager::quick_sort


void FragMapManager::sort(FragMapNode **entries, uint32 max)
{
    /*
     * Quicksort w/ Bubblesort fallback algorithm inspired by code from here:
     *   http://www.cs.ubc.ca/spider/harrison/Java/sorting-demo.html
     */
    quick_sort(entries, 0, max - 1);
} // FragMapManager::sort


FragMapSnapshot *FragMapManager::create_snapshot()
{
    uint32 cnt = 0;
    FragMapSnapshot *ss;

    ss = new FragMapSnapshot(total_nodes, current_operation);

    for (int i = 0; i <= 0xFFFF; i++)
    {
        FragMapNode *node = fragmap[i];
        while (node != NULL)
        {
            ss->nodes[cnt++] = FragMapNodePool::get(node->ptr, node->size);
            node = node->right;
        } // while
    } // for

    assert(total_nodes == cnt);
    sort(ss->nodes, total_nodes);
    return(ss);
} // FragMapManager::create_snapshot


void FragMapManager::add_snapshot()
{
    FragMapSnapshot *snapshot = create_snapshot();

    // !!! FIXME: realloc? yuck!
    total_snapshots++;
    snapshots = (FragMapSnapshot **) realloc(snapshots,
                    total_snapshots * sizeof (FragMapSnapshot *));
    assert(snapshots != NULL);  // !!! FIXME: lame.
    snapshots[total_snapshots-1] = snapshot;
} // FragMapManager::add_snapshot


FragMapNode *FragMapNodePool::freepool = NULL;

inline void FragMapNodePool::put(FragMapNode *node)
{
    if (node != NULL)
    {
        node->right = FragMapNodePool::freepool;
        FragMapNodePool::freepool = node;
    } // if
} // FragMapNodePool::put


void FragMapNodePool::putlist(FragMapNode *node)
{
    if (node != NULL)
    {
        FragMapNode *prev = NULL;
        while (node != NULL)
        {
            prev = node;
            node = node->right;
        } // while

        prev->right = FragMapNodePool::freepool;
        FragMapNodePool::freepool = prev;
    } // if
} // FragMapNodePool::putlist


inline FragMapNode *FragMapNodePool::get(dumpptr ptr, size_t size)
{
    FragMapNode *retval = FragMapNodePool::freepool;
    if (retval == NULL)
        retval = new FragMapNode(ptr, size);
    else
    {
        FragMapNodePool::freepool = retval->right;
        retval->ptr = ptr;
        retval->size = size;
    } // else

    return(retval);
} // FragMapNodePool::get


void FragMapNodePool::flush()
{
    FragMapNode *node = FragMapNodePool::freepool;
    while (node)
    {
        FragMapNode *next = node->right;
        delete node;
        node = next;
    } // while

    FragMapNodePool::freepool = NULL;
} // FragMapNodePool::flush


FragMapManager::FragMapManager()
    : snapshots(NULL),
      total_snapshots(0),
      fragmap(NULL),
      total_nodes(0),
      current_operation(0),
      snapshot_operations(0)
{
    fragmap = new FragMapNode*[0xFFFF + 1];
    memset(fragmap, '\0', (0xFFFF + 1) * sizeof (FragMapNode *));
} // FragMapManager::FragMapManager


FragMapManager::~FragMapManager()
{

    for (size_t i = 0; i < total_snapshots; i++)
        delete snapshots[i];

    free(snapshots); // !!! FIXME: allocated with realloc()...
    delete[] fragmap;
    FragMapNodePool::flush();
} // FragMapManager::~FragMapManager


inline void FragMapManager::empty_hashtable()
{
    for (size_t i = 0; i <= 0xFFFF; i++)
        FragMapNodePool::putlist(fragmap[i]);

    memset(fragmap, '\0', (0xFFFF + 1) * sizeof (FragMapNode *));
    total_nodes = 0;
} // FragMapManager::empty_hashtable


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
    // !!! FIXME: check for dupes before inserting?
    FragMapNode *node = FragMapNodePool::get(ptr, size);
    node->right = fragmap[hashval];  // FIXME: do this in the constructor.
    fragmap[hashval] = node;
    total_nodes++;
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
        FragMapNodePool::put(node);
        total_nodes--;
    } // if
} // FragMapManager::remove_block


inline void FragMapManager::hash_malloc(DumpFileOperation *op)
{
    insert_block(op->op_malloc.retval, op->op_malloc.size);
} // FragMapManager::hash_malloc


inline void FragMapManager::hash_realloc(DumpFileOperation *op)
{
    // !!! FIXME: Don't remove and reinsert if ptr == rc && size > 0...
    if (op->op_realloc.ptr)
        remove_block(op->op_realloc.ptr);

    if (op->op_realloc.size)
        insert_block(op->op_realloc.retval, op->op_realloc.size);
} // FragMapManager::hash_realloc


inline void FragMapManager::hash_free(DumpFileOperation *op)
{
    remove_block(op->op_free.ptr);
} // FragMapManager::hash_free


void FragMapManager::add_malloc(DumpFileOperation *op)
{
    hash_malloc(op);
    increment_operations();
} // FragMapManager::add_malloc


void FragMapManager::add_realloc(DumpFileOperation *op)
{
    // !!! FIXME: Is realloc(NULL, 0) illegal?
    hash_realloc(op);
    increment_operations();
} // FragMapManager::add_realloc


void FragMapManager::add_free(DumpFileOperation *op)
{
    hash_free(op);
    increment_operations();
} // FragMapManager::add_free


void FragMapManager::done_adding(ProgressNotify &pn)
{
    // flatten out final fragmap...
    add_snapshot();
    empty_hashtable();

#if 0  // PROFILING_STATISTICS
    int deepest = 0;
    int totalnodes = 0;

    #define MAXDEPTHS 16
    int depths[MAXDEPTHS];
    memset(depths, '\0', sizeof (depths));

    for (size_t i = 0; i <= 0xFFFF; i++)
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

    printf("total_snapshots == %d\n", total_snapshots);
#endif
} // FragMapManager::done_adding


inline void FragMapManager::increment_operations()
{
    current_operation++;
    if (++snapshot_operations >= FRAGMAP_SNAPSHOT_THRESHOLD)
    {
        add_snapshot();
        snapshot_operations = 0;
    } // if
} // FragMapManager::increment_operations


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
} // DumpFile::read_ptr

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

