CC = g++
CFLAGS = -std=c++11 -Wextra -Wall
TAR = tar
TARFLAGS = cvf
TARNAME = ex2.tar
TARSRCS = uthreads.cpp SingleThread.h Makefile README

all: uthreads.o
	ar rcs libuthreads.a uthreads.o

uthreads.o: uthreads.cpp uthreads.h SingleThread.h
	$(CC) $(CFLAGS) uthreads.cpp -c &&\
	$(CC) $(CFLAGS) SingleThread.h -c



clean:
	rm uthreads.o libuthreads.a ex2.tar

tar:
	$(TAR) $(TARFLAGS) $(TARNAME) $(TARSRCS)

.PHONY: clean, all, tar
