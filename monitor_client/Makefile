
CC = gcc
DLL_CFLAGS = -O0 -fPIC -g -Wall -c -o
DLL_LDFLAGS = -shared -o
LD = gcc

HOOKLIB = malloc_monitor.so
HOOKLIBOBJS = malloc_hook_glibc.o malloc_monitor_client.o

.PHONY: all clean

all : $(HOOKLIB)

clean :
	rm -f $(HOOKLIB) $(HOOKLIBOBJS)

%.o : %.c
	$(CC) $(DLL_CFLAGS) $@ $<

$(HOOKLIB) : $(HOOKLIBOBJS)
	$(LD) $(DLL_LDFLAGS) $@ $(HOOKLIBOBJS)

# end of Makefile ...

