
#include <stdio.h>
#include <stdlib.h>
#include "dumpfile.h"

class ProgressNotifyStdio : public ProgressNotify
{
public:
    ProgressNotifyStdio() : lastpercent(-1) {}
    virtual void update(const char *str, int percent)
    {
        if (percent != lastpercent)
        {
            lastpercent = percent;
            printf("%s: %d%%\n", str, percent);
        } // if
    } // update

protected:
    int lastpercent;
};


static void print_callstack(CallstackManager &cm,
                            CallstackManager::callstackid id)
{
    size_t count = cm.framecount(id);
    dumpptr *frames = (dumpptr *) alloca(sizeof (dumpptr) * count);
    cm.get(id, frames);

    printf("      Callstack:\n");
    for (size_t i = 0; i < count; i++)
        printf("        #%d: 0x%X\n", (int) ((count-i)-1), (int) frames[i]);
} // print_callstack


int main(int argc, char **argv)
{
    for (int i = 1; i < argc; i++)
    {
        try
        {
            ProgressNotifyStdio pn;
            DumpFile df(argv[i], pn);
            CallstackManager &cm = df.callstackManager;
            size_t totalframes = cm.getTotalCallstackFrames();
            size_t uniqueframes = cm.getUniqueCallstackFrames();
            float frameratio = (((float) uniqueframes) /
                                ((float) totalframes)) * 100.0f;

            printf("\n=== %s ===\n", argv[i]);
            printf("  version: %d\n", (int) df.getFormatVersion());
            printf("  bigendian: %d\n", (int) df.platformIsBigendian());
            printf("  littleendian: %d\n", (int) df.platformIsLittleendian());
            printf("  sizeof (void *): %d\n", (int) df.getSizeofPtr());
            printf("  id: %s\n", df.getId());
            printf("  binary filename: %s\n", df.getBinaryFilename());
            printf("  process id: %d\n", (int) df.getProcessId());
            printf("  total operations: %d\n", (int) df.getOperationCount());
            printf("  total callstack frames: %d\n", (int) totalframes);
            printf("  unique callstack frames: %d\n", (int) uniqueframes);
            printf("  unique/total ratio: %f\n", frameratio);

            printf("\n  Operations...\n");
            uint32 max = df.getOperationCount();
            for (uint32 i = 0; i < max; i++)
            {
                DumpFileOperation *op = df.getOperation(i);
                printf("    op %d, timestamp %d: ",
                        (int) i, (int) op->getTimestamp());

                dumpfile_operation_t optype = op->getOperationType();
                switch (optype)
                {
                    case DUMPFILE_OP_MALLOC:
                        printf("malloc(%d), returned 0x%X\n",
                               (int) op->op_malloc.size,
                               (int) op->op_malloc.retval);
                        break;

                    case DUMPFILE_OP_REALLOC:
                        printf("realloc(0x%X, %d), returned 0x%X\n",
                               (int) op->op_realloc.ptr,
                               (int) op->op_realloc.size,
                               (int) op->op_realloc.retval);
                        break;

                    case DUMPFILE_OP_MEMALIGN:
                        printf("memalign(%d, %d), returned 0x%X\n",
                               (int) op->op_memalign.boundary,
                               (int) op->op_memalign.size,
                               (int) op->op_memalign.retval);
                        break;

                    case DUMPFILE_OP_FREE:
                        printf("free(0x%X)\n",
                               (int) op->op_free.ptr);
                        break;

                    default:
                        printf("unknown operation %d!\n", (int) optype);
                        break;
                } // switch

                print_callstack(cm, op->getCallstackId());
            } // for
        } // try

        catch (const char *err)
        {
            printf("Error processing %s: %s\n", argv[i], err);
        } // catch
    } // for

    return(0);
} // main

// end of stats.cpp ...

