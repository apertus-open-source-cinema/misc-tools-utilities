hdmi4k: hdmi4k.c cmdoptions.c patternnoise.c ufraw_routines.c
	gcc $^ -o hdmi4k -lm -O3 -Wall -std=gnu99 -g -fopenmp -march=native

clean:
	rm hdmi4k

MINGW_GCC=~/mingw-w32/bin/i686-w64-mingw32-gcc

hdmi4k.exe: hdmi4k.c cmdoptions.c patternnoise.c ufraw_routines.c
	$(MINGW_GCC) $^ -o hdmi4k.exe -lm -O3 -Wall -std=gnu99 -g
