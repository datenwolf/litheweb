.PHONY: all

all: bsdsocket bsdsocket_nhd

bsdsocket: bsdsocket.c ../picohttp.c ../picohttp.h
	$(CC) -std=c99 -DHOST_DEBUG -O0 -g3 -I../ -o bsdsocket ../picohttp.c bsdsocket.c
	
bsdsocket_nhd: bsdsocket.c ../picohttp.c ../picohttp.h
	$(CC) -std=c99 -O0 -g3 -I../ -o bsdsocket_nhd ../picohttp.c bsdsocket.c
