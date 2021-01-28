mpdart
======

A simple mpd client which only displays the cover art for the current song.

depends
-------

libX11
libmpdclient
libim2
pkg-config (optional build time dependency)

building
--------

	./configure.sh
	make
	make install # supports PREFIX and DESTDIR

usage
-----

There are three flags:

	mpdart -d musicdir -h host -p port

Though only -d is needed:

	mpdart -d ~/music

Once mpdart is running you can press the window to toggle mpd's pause/play.

contributing
------------

### pull requests
If you like github you can contribute by sending in a pull request at
https://github.com/depsterr/mpdart

### patches
If you don't like github you can contribute by emailing me at
depsterr at protonmail dot com with the output of

	git format-patch

after you've commited your changes to your local tree.

credits
-------

Big thanks to my friend wooosh for helping me debug and develop this :)