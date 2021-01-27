.POSIX:
include config.mk
all: mpdart config.mk
mpdart: mpdart.c
	$(CC) $(CONFIG) $(CFLAGS) $(CXXFLAGS) -o $@ mpdart.c
clean:
	rm -f mpdart
install: mpdart
	install -Dm755 mpdart $(DESTDIR)$(PREFIX)/bin/mpdart
uninstall:
	rm -f $(DESTDIR)$(PREFIX)/bin/mpdart
