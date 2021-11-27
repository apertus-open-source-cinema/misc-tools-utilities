UNAME_S := $(shell uname -m)
PREFIX := /usr/local
DESTDIR :=

ifeq ($(UNAME_S), armv7l)
	CCFLAGS=
else
	CCFLAGS= 
endif

rgb-merge: frame-merge.c 
	gcc $^ -o frame-merge $(CCFLAGS) -O3

clean:
	rm frame-merge

install: frame-merge
	mkdir -p $(DESTDIR)$(PREFIX)/bin
	cp frame-merge $(DESTDIR)$(PREFIX)/bin
