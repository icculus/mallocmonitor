/*
 * Hacky program to profile fragmap snapshot request speed.
 */

#include <stdio.h>
#include <stdlib.h>
#include <sys/time.h>
#include <time.h>

#include "dumpfile.h"

#define DO_LINEAR_SEEK_TEST 0
#define DO_REVERSE_LINEAR_SEEK_TEST 0
#define DO_SEQUENTIAL_SKIP_SEEK_TEST 1
#define DO_RANDOM_SKIP_SEEK_TEST 1

class ProgressNotifyNoOp : public ProgressNotify
{
public:
    ProgressNotifyNoOp() {}
    virtual void update(const char *str, int percent)
    {
    } // update
};


// !!! FIXME: cut and paste from the client code.
static struct timeval tickbase;
static inline void reset_tick_base(void)
{
    gettimeofday(&tickbase, NULL);
} /* reset_tick_base */

static inline tick_t get_ticks(void)
{
    struct timeval curtime;
    gettimeofday(&curtime, NULL);
    return( (tick_t) ( ((curtime.tv_sec - tickbase.tv_sec) * 1000) +
                       ((curtime.tv_usec - tickbase.tv_usec) / 1000) ) );
} /* get_ticks */


static void jump_around(const char *filename, DumpFile &df)
{
    const uint32 ITERATIONS = 3;
    uint32 opcount = df.getOperationCount();
    tick_t ticks = 0;

    printf("%s: %u operations total.\n", filename, (unsigned int) opcount);

    #if DO_LINEAR_SEEK_TEST
    ticks = 0;
    for (uint32 iter = 0; iter < ITERATIONS; iter++)
    {
        printf(" + linear fragmap seek iteration #%d...\n", (int) iter);
        reset_tick_base();
        for (uint32 i = 0; i < opcount; i++)
        {
            size_t nc = 0;
            df.fragmapManager.get_fragmap(&df, i, nc);
        } // for
        ticks += get_ticks();
    } // for

    printf(" +  (%d ticks, %d iterations == %d ticks per iteration)\n",
            (int) ticks, (int) ITERATIONS, (int) (ticks / ITERATIONS));
    #endif

    #if DO_REVERSE_LINEAR_SEEK_TEST
    ticks = 0;
    for (uint32 iter = 0; iter < ITERATIONS; iter++)
    {
        printf(" + reverse linear fragmap seek iteration #%d...\n", (int) iter);
        reset_tick_base();
        for (uint32 i = opcount-1; i >= 0; i--)
        {
            size_t nc = 0;
            df.fragmapManager.get_fragmap(&df, i, nc);
        } // for
        ticks += get_ticks();
    } // for

    printf(" +  (%d ticks, %d iterations == %d ticks per iteration)\n",
            (int) ticks, (int) ITERATIONS, (int) (ticks / ITERATIONS));
    #endif

    #if DO_SEQUENTIAL_SKIP_SEEK_TEST
    ticks = 0;
    for (uint32 iter = 0; iter < ITERATIONS; iter++)
    {
        printf(" + sequential skip fragmap seek iteration #%d...\n", (int) iter);
        reset_tick_base();
        uint32 skip = (uint32) (((float) opcount) * 0.05f);
        for (uint32 i = 0; i < opcount; i += skip)
        {
            size_t nc = 0;
            df.fragmapManager.get_fragmap(&df, i, nc);
        } // for
        ticks += get_ticks();
    } // for

    printf(" +  (%d ticks, %d iterations == %d ticks per iteration)\n",
            (int) ticks, (int) ITERATIONS, (int) (ticks / ITERATIONS));
    #endif

    #if DO_RANDOM_SKIP_SEEK_TEST
    ticks = 0;
    for (uint32 iter = 0; iter < ITERATIONS; iter++)
    {
        printf(" + random skip fragmap seek iteration #%d...\n", (int) iter);
        reset_tick_base();
        uint32 skip = (uint32) (((float) opcount) * 0.05f);
        for (uint32 i = 0; i < opcount; i += skip)
        {
            int op = rand() % opcount;
            size_t nc = 0;
            df.fragmapManager.get_fragmap(&df, op, nc);
        } // for
        ticks += get_ticks();
    } // for

    printf(" +  (%d ticks, %d iterations == %d ticks per iteration)\n",
            (int) ticks, (int) ITERATIONS, (int) (ticks / ITERATIONS));
    #endif
} // jump_around


int main(int argc, char **argv)
{
    for (int i = 1; i < argc; i++)
    {
        try
        {
            ProgressNotifyNoOp pn;
            DumpFile df(argv[i], pn);
            jump_around(argv[i], df);
        } // try

        catch (const char *err)
        {
            printf("Error processing %s: %s\n", argv[i], err);
        } // catch
    } // for

    return(0);
} // main

// end of jumparound.cpp ...

