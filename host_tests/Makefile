# Host (laptop) tests for the portable protocol code. Needs gcc or clang and make.
#   make          build and run the unit tests
#   make wav      build the adpcm_wav listening tool
CC ?= cc
CFLAGS ?= -std=c11 -O2 -Wall -Wextra -Werror -D_DEFAULT_SOURCE
PROTO = ../components/proto
INC = -I$(PROTO)/include

all: test

run_tests: run_tests.c test_adpcm.c test_jbuf.c test_packet.c $(PROTO)/*.c $(PROTO)/include/*.h
	$(CC) $(CFLAGS) $(INC) -o $@ run_tests.c test_adpcm.c test_jbuf.c test_packet.c $(PROTO)/adpcm.c $(PROTO)/jbuf.c $(PROTO)/packet.c -lm

test: run_tests
	./run_tests

wav: adpcm_wav

adpcm_wav: adpcm_wav.c $(PROTO)/adpcm.c
	$(CC) $(CFLAGS) $(INC) -o $@ adpcm_wav.c $(PROTO)/adpcm.c

clean:
	rm -f run_tests adpcm_wav

.PHONY: all test wav clean
