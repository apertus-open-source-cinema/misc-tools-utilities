UNAME_S := $(shell uname -m)
PREFIX := /usr/local
DESTDIR :=

ifeq ($(UNAME_S), armv7l)
	CCFLAGS=
else
	CCFLAGS= -m32
endif

raw2dng: raw2dng.c chdk-dng.c cmdoptions.c patternnoise.c metadata.c
	gcc $^ -o raw2dng $(CCFLAGS) -lm -O3 -Wall -std=gnu99 -g -fopenmp -march=native

clean:
	rm raw2dng

install: raw2dng
	mkdir -p $(DESTDIR)$(PREFIX)/bin
	cp raw2dng $(DESTDIR)$(PREFIX)/bin
