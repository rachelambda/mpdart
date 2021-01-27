#!/bin/sh

if command -V pkg-config >/dev/null 2>&1; then
	printf '%s\n' "CONFIG=$(pkg-config libmpdclient --libs --cflags || exit
	) $(pkg-config imlib2 --libs --cflags || exit
	) -lX11" > config.mk
else
	printf '%s\n' "CONFIG=-lmpdclient -lImlib2 -lX11" > config.mk
fi
