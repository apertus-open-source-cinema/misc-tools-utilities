PREFIX := /usr/local
DESTDIR :=

hdmi4k: hdmi4k.c cmdoptions.c
	gcc $^ -o hdmi4k -lm -O3 -Wall -std=gnu99 -g -fopenmp -march=native

clean:
	rm hdmi4k

install: hdmi4k
	mkdir -p $(DESTDIR)$(PREFIX)/bin
	cp hdmi4k $(DESTDIR)$(PREFIX)/bin
	cp render.sh $(DESTDIR)$(PREFIX)/bin/hdmi4k-render.sh

MINGW_GCC=~/mingw-w32/bin/i686-w64-mingw32-gcc

hdmi4k.exe: hdmi4k.c cmdoptions.c
	$(MINGW_GCC) $^ -o hdmi4k.exe -lm -O3 -Wall -std=gnu99 -g
