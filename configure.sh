#!/bin/sh

if command -V pkg-config >/dev/null 2>&1; then
	printf '%s\n' "CONFIG=$(
	pkg-config libmpdclient --libs --cflags) $(
	pkg-config imlib2 --libs --cflags) $(
	pkg-config x11 --libs --cflags)" > config.mk
else
	printf '%s\n' "CONFIG=-lmpdclient -lImlib2 -lX11" > config.mk
fi
