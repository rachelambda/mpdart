#include <dirent.h>
#include <stdarg.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>

#include <poll.h>
#include <sys/types.h>
#include <unistd.h>

#include <Imlib2.h>
#include <mpd/client.h>
#include <X11/Xlib.h>
#include <X11/Xutil.h>

/* TODO image metadata images */

/* mpd globals */
struct mpd_connection* connection = 0;
int mpd_fd = 0;
char* mpd_db_dir = 0;

Display* xdisplay;
int xscreen;
Visual* xvisual;
Colormap xcolormap;
int xdepth;
Window xwindow;
GC gc;

Imlib_Updates im_updates;
Imlib_Image im_buffer, im_image = 0;
Imlib_Color_Range range;
char* im_image_path;
int im_w, im_h;


void die(const char* msg) {
	fprintf(stderr, "FATAL: %s\n", msg);
	exit(1);
}

void warn(const char* msg) {
	fprintf(stderr, "WARNING: %s\n", msg);
}

/* error checking malloc */
void* xmalloc(size_t size) {
	void* ret = malloc(size);
	if (!ret)
		die("Unable to allocate memory");
	return ret;
}

/* returns char pointer instead of writing to predefined one */
char* asprintf(const char* fmt, ...) {
	va_list ap;

	va_start(ap, fmt);
	/* +1 for null byte */
	int len = vsnprintf(0, 0, fmt, ap) + 1;
	va_end(ap);

	char* ret = xmalloc(len);

	/* vsnprintf fucks up ap, so reopen it for the second call */
	va_start(ap, fmt);
	vsnprintf(ret, len, fmt, ap);
	va_end(ap);

	return ret;
}

/* currently only works once... why? */
void set_window_name(char* name) {
	int len = strlen(name);

	XTextProperty prop;

	Xutf8TextListToTextProperty(xdisplay, &name, 1, XUTF8StringStyle,
			&prop);
	XSetWMName(xdisplay, xwindow, &prop);
	XSetTextProperty(xdisplay, xwindow, &prop, XInternAtom(xdisplay, "_NET_WM_NAME", False));
	XFree(prop.value);

	XFlush(xdisplay);
}

void imlib_update(char* path) {

	if (im_image_path)
		free(im_image_path);
	im_image_path = path;

	if (im_image) {
		imlib_context_set_image(im_image);
		imlib_free_image();
	}

	if (!im_image_path) {
		warn("No image path");
		XClearWindow(xdisplay, xwindow);
		return;
	}

	im_image = imlib_load_image(im_image_path);

	if (!im_image) {
		warn("Unable to open image");
		XClearWindow(xdisplay, xwindow);
		return;
	}

	imlib_context_set_image(im_image);

	im_w = imlib_image_get_width();
	im_h = imlib_image_get_height();

	imlib_context_set_image(im_buffer);

}

void imlib_render(int up_w, int up_h) {

	imlib_blend_image_onto_image(im_image, 0,
			0, 0, im_w, im_h,
			0, 0, up_w, up_h);

	imlib_render_image_on_drawable(0, 0);

}

/* get currently playing song from mpd and update X window */
void update_mpd_song(void) {

	static int song_id;
	static int old_song_id = -1;

	struct mpd_song* song;

	mpd_send_current_song(connection);
	song = mpd_recv_song(connection);

	if (!song) {
		fprintf(stderr, "Failed to get song from mpd\n");
		imlib_update(0);
		set_window_name("None");
		return;
	}

	song_id = mpd_song_get_id(song);

	if (song_id != old_song_id) {
		char* pretty_name = asprintf("%s - %s",
				mpd_song_get_tag(song, MPD_TAG_TITLE, 0), 
				mpd_song_get_tag(song, MPD_TAG_ARTIST, 0));
		char* path = asprintf("%s/%s",
				mpd_db_dir, mpd_song_get_uri(song));

		set_window_name(pretty_name);
		printf("Now playing: '%s' from '%s'\n", pretty_name, path);

		/* equivalent to shell dirname="${path%/*}" */
		size_t len = strrchr(path, '/') - path;
		char* dirname = xmalloc(len + 1);
		strncpy(dirname, path, len);
		dirname[len] = '\0';

		/* account for .cue files */
		
		for (char* cue = strstr(dirname, ".cue");
				cue >= dirname; cue--) {
			if (*cue == '/') {
				*cue = '\0';
				break;
			}
		}

		DIR* dir = opendir(dirname);

		if (!dir) {
			warn("Unable to open dir");
		} else {
			struct dirent* ent;
			while (ent = readdir(dir)) {
				char* extension = strrchr(ent->d_name, '.');
				/* TODO add more extensions and match multiple extension
				   and select one based on list of strings such as cover COVER
				   art album etc */
				if (extension && (!strcmp(extension, ".jpg") || !strcmp(extension, ".png"))) {
					printf("Using '%s' as album art.\n", ent->d_name);
					imlib_update(asprintf("%s/%s", dirname, ent->d_name));
					break;
				}
			}
		}

		closedir(dir);

		free(pretty_name);
		free(path);

	}

	old_song_id = song_id;

	mpd_song_free(song);
}

int main(int argc, char** argv) {

	/* parse args */
	char* mpd_host = 0;
	unsigned mpd_port = 0;
	unsigned mpd_timeout = 0;

	while (*++argv) {
		if (!strcmp(*argv, "-d"))
			mpd_db_dir = *++argv;
		else if (!strcmp(*argv, "-h"))
			mpd_host = *++argv;
		else if (!strcmp(*argv, "-p") && argv[1])
			mpd_port = atoi(*++argv);
		else if (!strcmp(*argv, "-t") && argv[1])
			mpd_timeout = atoi(*++argv);
	}
	
	if (!mpd_db_dir)
		die("Please specify mpd music directory with -d");

	/* strip all '/'es from the end of mpd_db_dir */
	{
		int i = 0;
		for (; mpd_db_dir[i]; i++);
		while (mpd_db_dir[--i] == '/');
		mpd_db_dir[i+1] = '\0';
	}
	
	/* set up mpd connection */
	connection = mpd_connection_new(mpd_host, mpd_port, mpd_timeout);

	if (!connection)
		die("Unable to allocate memory for mpd_connection struct");

	if (mpd_connection_get_error(connection) != MPD_ERROR_SUCCESS)
		die(mpd_connection_get_error_message(connection));

	const int* version = mpd_connection_get_server_version(connection);
	printf("Connected to mpd server version %d.%d.%d\n", version[0], version[1], version[2]);

	/* setup x */
	XInitThreads();

	xdisplay = XOpenDisplay(0);
	if (!xdisplay)
		die("Cannot open display");

	xscreen   = XDefaultScreen(xdisplay);
	gc        = DefaultGC(xdisplay, xscreen);
	xvisual   = DefaultVisual(xdisplay, xscreen);
	xdepth    = DefaultDepth(xdisplay, xscreen);
	xcolormap = DefaultColormap(xdisplay, xscreen);

	Window xparent = XRootWindow(xdisplay, xscreen);

	unsigned int ww = 256, wh = 256, x = 0, y = 0;
	unsigned int border_width = 0;
	/* are these two needed when border_width is 0? */
	unsigned int border_color = BlackPixel(xdisplay, xscreen);
	unsigned int background_color = WhitePixel(xdisplay, xscreen);

	xwindow = XCreateSimpleWindow(
			xdisplay,
			xparent,
			x,
			y,
			ww,
			wh,
			border_width,
			border_color,
			background_color);

	XSizeHints* size_hints = XAllocSizeHints();

	if (!size_hints)
		die("Unable to allocate memory");

	size_hints->flags = PAspect | PMinSize | PMaxSize;
	size_hints->min_width = 64;
	size_hints->min_height = 64;
	size_hints->max_width = 1024;
	size_hints->max_height = 1024;
	size_hints->min_aspect.x = 1;
	size_hints->max_aspect.x = 1;
	size_hints->min_aspect.y = 1;
	size_hints->max_aspect.y = 1;

	XSetWMNormalHints(xdisplay, xwindow, size_hints);

	XFree(size_hints);

	XSelectInput(xdisplay, xwindow, ExposureMask | StructureNotifyMask);
	XMapWindow(xdisplay, xwindow);
	set_window_name("mpdart");

	Atom wm_delete = XInternAtom(xdisplay, "WM_DELETE_WINDOW", True);
	XSetWMProtocols(xdisplay, xwindow, &wm_delete, 1);

	/* setup Imlib */
	imlib_set_cache_size(2048 * 1024);
	imlib_set_color_usage(128);
	imlib_context_set_dither(1);
	imlib_context_set_display(xdisplay);
	imlib_context_set_visual(xvisual);
	imlib_context_set_colormap(xcolormap);
	imlib_context_set_drawable(xwindow);

	im_updates = imlib_updates_init();

	/* 1024 is max size */
	im_buffer = imlib_create_image(1024, 1024);

	if (!im_buffer) {
		die("Unable to create buffer");
		XClearWindow(xdisplay, xwindow);
	}


	/* get currently playing song before waiting for new ones */
	update_mpd_song();

	mpd_fd = mpd_connection_get_fd(connection);

	int xfd = ConnectionNumber(xdisplay);

	struct pollfd fds[2] = {
		{
			.fd = xfd,
			.events = POLLIN
		},
		{
			.fd = mpd_fd,
			.events = POLLIN
		}
	};

	if(!mpd_send_idle_mask(connection, MPD_IDLE_PLAYER))
		die("Unable to send idle to mpd");

	/* mpd event loop */
	while (1) {
		/* sleep for a day at a time */
		int ready_fds = poll(fds, 2, 86400);
		if (ready_fds < 0) {
			die("Error in poll");
		} else if (ready_fds > 0) {
			/* X event loop */
			if (fds[0].revents & POLLIN) {
				while (XPending(xdisplay)) {
					XEvent ev;
					XNextEvent(xdisplay, &ev);

					switch (ev.type) {
						case ClientMessage:
							XCloseDisplay(xdisplay);
							die("Window Closed");
							break; // ?
						case ConfigureNotify:
							ww = ev.xconfigure.width;
							wh = ev.xconfigure.height;
							break;
						/* case Expose: */
						/* 	ww = ev.xexpose.width; */
						/* 	wh = ev.xexpose.height; */
						/* 	break; */
					}
				}
				imlib_render(ww, wh);
			}
			/* MPD event loop */
			if (fds[1].revents & POLLIN) {
				mpd_run_noidle(connection);
				update_mpd_song();
				imlib_render(ww, wh);
				mpd_send_idle_mask(connection, MPD_IDLE_PLAYER);
			}
		}
	}
}
