UNAME_S := $(shell uname -m)

ifeq ($(UNAME_S), armv7l)
	CCFLAGS=
else
	CCFLAGS= -m32
endif
	
raw2dng: raw2dng.c chdk-dng.c cmdoptions.c patternnoise.c metadata.c
	gcc $^ -o raw2dng $(CCFLAGS) -lm -O3 -Wall -std=gnu99 -g -fopenmp -march=native

clean:
	rm raw2dng

dng2raw: dng2raw.c
	gcc dng2raw.c $(CCFLAGS) -O2 -Wall -o dng2raw

install: raw2dng
	cp raw2dng /usr/local/bin
