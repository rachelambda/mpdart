.POSIX:
include config.mk
LIBS=-lX11
all: mpdart config.mk
mpdart: mpdart.c
	$(CC) $(CONFIG) $(LIBS) $(CFLAGS) $(CXXFLAGS) -o $@ mpdart.c
clean:
	rm -f mpdart
install: mpdart
	install -Dm755 mpdart $(DESTDIR)$(PREFIX)/bin/mpdart
uninstall:
	rm -f $(DESTDIR)$(PREFIX)/bin/mpdart
