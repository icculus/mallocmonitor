
#include <stdio.h>
#include "dumpfile.h"


static void DumpFileProgressCallback(const char *str, int percent)
{
    static int lastpercent = 100;
    if (percent != lastpercent)
    {
        lastpercent = percent;
        printf("%s: %d%%\n", str, percent);
    } // if
} // DumpFileProgressCallback


int main(int argc, char **argv)
{
    for (int i = 1; i < argc; i++)
    {
        try
        {
            DumpFile df(argv[i], DumpFileProgressCallback);
        } // try
        catch (const char *err)
        {
            printf("Error processing %s: %s\n", argv[i], err);
        } // catch
    } // for

    return(0);
} // main

// end of stats.cpp ...

