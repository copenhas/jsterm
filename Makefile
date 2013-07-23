# This is a stub Makefile that invokes GNU make which will read the GNUmakefile
# instead of this file. This provides compatability on systems where GNU make is
# not the system 'make' (eg. most non-linux UNIXes).

all:
	cc main.c -Wall -O0 -g -lmozjs185-1.0 -lnspr4 -lstdc++
