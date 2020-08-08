#
# Simple Makefile for GCC/Linux
#

.DEFAULT: default

OBJS=JDebug.o JDiff.o JDefs.o JFileAhead.o JFileIStream.o JFileIStreamAhead.o JHashPos.o \
     JMatchTable.o JOutAsc.o JOutBin.o JOutRgn.o main.o 

default:	linux
all: 		linux 
full:		clean linux
linux:		jdiff jpatch
debug:		jdifd jpatcd

CC=gcc
CPP=g++
NATIVE=-march=native -m64
CFLAGS=$(NATIVE) -O2 -Wall 

debug:DBG=-g -D_DEBUG

%.o: %.cpp
	$(CPP) $(CFLAGS) $(DBG) -o $@ -c $<

clean:
	rm -f jdiff jpatch jdifd jptcd *.exe $(OBJS)

jdiff: $(OBJS)
	$(CPP) $(CFLAGS) $(DBG) $(OBJS) -o jdiff

jpatch: jpatch.cpp
	$(CPP) $(CFLAGS) $(DBG) -o jpatch jpatch.cpp

jdifd: $(OBJS)
	$(CPP) $(CFLAGS) $(DBG) $(OBJS) -o jdifd

jpatcd: jpatch.cpp
	$(CPP) $(CFLAGS) $(DBG) -o jptcd jpatch.cpp


