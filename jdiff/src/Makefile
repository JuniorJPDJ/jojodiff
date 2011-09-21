#
# Makefile
#

.DEFAULT: default

OBJS=JDebug.o JDiff.o JFileAhead.o JFileIStream.o JHashPos.o JMatchTable.o JOutAsc.o JOutBin.o JOutRgn.o main.o 

default:	linux
all: 		linux 
full:		clean linux
linux:		jdiff jptch
debug:		jdifd jptcd

CC=gcc
CPP=g++
CFLAGS=-Os -Wall 
NATIVE=-march=native

debug:DBG=-g -D_DEBUG

%.o: %.cpp
	$(CPP) $(CFLAGS) $(DBG) -o $@ -c $<

clean:
	rm -f jdiff jptch jdifd jptcd *.exe $(OBJS)

jdiff: $(OBJS)
	$(CPP) $(CFLAGS) $(DBG) $(OBJS) -o jdiff

jptch: jpatch.cpp
	$(CPP) $(CFLAGS) $(DBG) -o jptch jpatch.cpp

jdifd: $(OBJS)
	$(CPP) $(CFLAGS) $(DBG) $(OBJS) -o jdifd

jptcd: jpatch.cpp
	$(CPP) $(CFLAGS) $(DBG) -o jptcd jpatch.cpp


