
#include <stdio.h>
#include "dumpfile.h"

int main(int argc, char **argv)
{
    for (int i = 1; i < argc; i++)
    {
        try
        {
            DumpFile df(argv[i]);
        } // try
        catch (const char *err)
        {
            printf("Error processing %s: %s\n", argv[i], err);
        } // catch
    } // for

    return(0);
} // main

// end of stats.cpp ...

