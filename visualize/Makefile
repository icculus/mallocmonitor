
DEFS += -Duint8="unsigned char" -Duint32="unsigned int"
DEFS += -Duint64="unsigned long long" -Dtick_t="unsigned int"

CC = gcc
CFLAGS = -O0 -pipe -g -Wall -c $(DEFS) -fexceptions -o
LDFLAGS = -o
DLL_LDFLAGS = -shared -o
LD = g++

OBJS = dumpfile.o stats.o

STATS = stats
STATSOBJS = dumpfile.o stats.o

.PHONY: all clean

all : $(STATS)

clean :
	rm -f $(STATS) $(OBJS)

%.o : %.cpp
	$(CC) $(CFLAGS) $@ $<

%.o : %.c
	$(CC) $(CFLAGS) $@ $<

$(STATS) : $(STATSOBJS)
	$(LD) $(LDFLAGS) $@ $(STATSOBJS)

# end of Makefile ...

