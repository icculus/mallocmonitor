
/*
 * Hacky program to display fragmap snapshot request speed.
 */

#include <stdio.h>
#include <stdlib.h>
#include <sys/time.h>
#include <time.h>

#include "dumpfile.h"
#include "SDL.h"

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


static SDL_Surface *screen = NULL;
static volatile float scrubber = 0.0f;

static inline bool pump_queue(void)
{
    SDL_Event event;
    bool just_sleep = true;

    while (SDL_PollEvents(&event))
    {
        just_sleep = false;
        if (event.type == SDL_QUIT)
        {
            SDL_Quit();
            exit(0);
        } // if
        else if (event.type == SDL_KEYDOWN)
        {
            if (event.key.keysym.sym == SDLK_ESCAPE)
                return(false);
        } // else if
        else if (event.type == SDL_MOUSEMOTION)
        {
            if (event.motion.state)  // any button pressed
                scrubber = ((float) event.motion.x) / ((float) screen->w);
        } // else
    } // while

    if (just_sleep)
        SDL_Delay(10);

    return(true);
} // pump_queue


static void render_loop(const char *fname, DumpFile &df)
{
    printf("dumpfile %s\n", fname);
    float opcount = (float) df.getOperationCount();
    Uint32 black = SDL_MapRGB(screen, 0, 0, 0);
    Uint32 red = SDL_MapRGB(screen, 0xFF, 0, 0);
    Uint32 green = SDL_MapRGB(screen, 0, 0xFF, 0);
    Uint32 blue = SDL_MapRGB(screen, 0, 0, 0xFF);
    SDL_FillRect(screen, NULL, black);
    SDL_Flip(screen);

    while (pump_queue())
    {
        size_t nc = 0;
        uint32 op = (uint32) (opcount * scrubber);
        FragMapNode **nodes = df.fragmapManager.get_fragmap(&df, op, nc);


    } // while
} // render_loop


int main(int argc, char **argv)
{
    if (SDL_Init(SDL_INIT_VIDEO) == -1)
    {
        fprintf(stderr, "SDL_Init() failed: %s\n", SDL_GetError());
        return(1);
    } // if

    if ((screen = SDL_SetVideoMode(640, 480, 0, 0)) == NULL)
    {
        fprintf(stderr, "SDL_SetVideoMode() failed: %s\n", SDL_GetError());
        return(2);
    } // if

    for (int i = 1; i < argc; i++)
    {
        try
        {
            ProgressNotifyNoOp pn;
            DumpFile df(argv[i], pn);
            render_loop(argv[i], df);
        } // try

        catch (const char *err)
        {
            printf("Error processing %s: %s\n", argv[i], err);
        } // catch
    } // for

    return(0);
} // main

// end of jumparound.cpp ...

