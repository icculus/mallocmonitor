
DEFS += -Duint8="unsigned char" -Duint32="unsigned int"
DEFS += -Duint16="unsigned short" -Dtick_t="unsigned int"
DEFS += -Duint64="unsigned long long" -Dtick_t="unsigned int"
DEFS += -Ddumpptr="uint32" -Dtick_t="unsigned int"

#DEFS += -D_HAVE_ASM_BYTEORDER_H_=1

CC = gcc
#OPTS = -O0
OPTS = -O3 -falign-loops=16 -fomit-frame-pointer -mdynamic-no-pic -D_NDEBUG=1
#OPTS = -O3 -mcpu=i686 -march=pentium3 -fomit-frame-pointer -D_NDEBUG=1
CFLAGS = $(OPTS) -pipe -g -Wall -c $(DEFS) -fexceptions -o
LDFLAGS = -o
DLL_LDFLAGS = -shared -o
LD = g++

OBJS = dumpfile.o stats.o jumparound.o

STATS = stats
STATSOBJS = dumpfile.o stats.o

JUMPAROUND = jumparound
JUMPAROUNDOBJS = dumpfile.o jumparound.o

.PHONY: all clean

all : $(STATS) $(JUMPAROUND)

clean :
	rm -f $(STATS) $(OBJS)

%.o : %.cpp
	$(CC) $(CFLAGS) $@ $<

%.o : %.c
	$(CC) $(CFLAGS) $@ $<

$(STATS) : $(STATSOBJS)
	$(LD) $(LDFLAGS) $@ $(STATSOBJS)

$(JUMPAROUND) : $(JUMPAROUNDOBJS)
	$(LD) $(LDFLAGS) $@ $(JUMPAROUNDOBJS)

# end of Makefile ...

