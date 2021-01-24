#!/bin/sh

if command -V pkg-config >/dev/null 2>&1; then
	printf '%s\n' "CONFIG=$(pkg-config libmpdclient --libs --cflags)" > config.mk
else
	printf '%s\n' "CONFIG=-lmpdclient" > config.mk
fi
