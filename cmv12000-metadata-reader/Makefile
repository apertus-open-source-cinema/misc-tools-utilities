PREFIX := /usr/local
DESTDIR :=

all: metadatareader.c
	gcc metadatareader.c -o metadatareader

clean:
	rm -rf *o metadatareader

install: metadatareader
	mkdir -p $(DESTDIR)$(PREFIX)/bin
	cp metadatareader $(DESTDIR)$(PREFIX)/bin
	cp read.sh $(DESTDIR)$(PREFIX)/bin/metadatareader-read.sh
