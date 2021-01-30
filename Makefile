.POSIX:
.PHONY: clean install uninstall all debug
include config.mk
all: mpdart
mpdart: mpdart.c
	$(CC) $(CONFIG) $(CFLAGS) $(CPPFLAGS) -o $@ mpdart.c
debug: mpdart_debug
mpdart_debug: mpdart.c
	$(CC) $(CONFIG) -Og -g -DDEBUG -o $@ mpdart.c
clean:
	rm -f mpdart
install: mpdart
	install -Dm755 mpdart $(DESTDIR)$(PREFIX)/bin/mpdart
uninstall:
	rm -f $(DESTDIR)$(PREFIX)/bin/mpdart
