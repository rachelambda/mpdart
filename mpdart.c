#include <dirent.h>
#include <stdarg.h>
#include <stdbool.h>
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

#ifdef DEBUG
# undef DEBUG
# define DEBUG(msg) printf("%d: %s\n", __LINE__, msg)
#else
# define DEBUG(_)
#endif

/* let user define a defualt window size on the compiler command line if they want to */
#ifndef DEFAULTSIZE
#define DEFAULTSIZE 256
#endif

/* TODO image metadata images */

/* Image filename end priority list */
const char*  name_priority[] = { "front", "Front", "cover", "Cover", 0 };
const size_t name_priority_lengths[] = { 9, 9, 9, 9, 0 };
const char*  image_extensions[] = { ".png", ".jpg", 0 };

/* mpd globals */
struct mpd_connection* connection = 0;
int mpd_fd = 0;
char* mpd_db_dir;
char* mpd_host;
unsigned mpd_port;
unsigned mpd_timeout;

/* x globals */
Display* xdisplay;
int xscreen;
Visual* xvisual;
Colormap xcolormap;
int xdepth;
Window xwindow;
GC gc;
unsigned int ww = DEFAULTSIZE, wh = DEFAULTSIZE; 

/* imlib globals */
Imlib_Updates im_updates;
Imlib_Image im_image = 0;
Imlib_Color_Range range;
char* im_image_path;
int im_w = DEFAULTSIZE, im_h = DEFAULTSIZE;


/* print to stderr and exit */
void die(const char* msg) {
	fprintf(stderr, "FATAL: %s\n", msg);
	exit(1);
}

/* print to stderr */
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

/* error checking realloc */
void* xrealloc(void* ptr, size_t size) {
	void* ret = realloc(ptr, size);
	if (!ret)
		die("Unable to reallocate memory");
	return ret;
}

/* return 1 if a filename is an image name, otherwise return 0 */
/* this fucks up if you name a folder .jpg or .png, but at that point you had it coming */
int is_image_name(char* f) {
	char* extension = strrchr(f, '.');
	for (int n = 0; image_extensions[n]; n++) {
		if (!strcmp(extension, image_extensions[n]))
			return 1;
	}
	return 0;
}

/* returns the name most likely to refer to album art */
char* most_relevant_name(char** names, size_t name_cnt) {
	for (int n = 0; name_priority[n]; n++) {
		DEBUG(name_priority[n]);
		for (int i = 0; i < name_cnt; i++) {
			DEBUG(names[i]);
			if (strstr(names[i], name_priority[n]))
				return names[i];
		}
	}
	return names[0];
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

/* set the name of the x window */
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

/* path must be dynamically allocated */
void imlib_update(char* path) {

	/* if we already have a path we might want to free it */
	if (im_image_path) {
		/* if the path is the same, just free the new path and return */
		if (path && !strcmp(path, im_image_path)) {
			free(path);
			return;
		}
		/* otherwise just free the old path */
		free(im_image_path);
	}
	im_image_path = path;

	if (im_image) {
		imlib_context_set_image(im_image);
		imlib_free_image();
	}

	if (!im_image_path) {
		warn("No image path");
		XClearWindow(xdisplay, xwindow);
		XFlush(xdisplay);
		return;
	}

	im_image = imlib_load_image_immediately(im_image_path);

	if (!im_image) {
		warn("Unable to open image");
		DEBUG(im_image_path);
		XClearWindow(xdisplay, xwindow);
		return;
	}

	imlib_context_set_image(im_image);

	im_w = imlib_image_get_width();
	im_h = imlib_image_get_height();

}

void imlib_render(void) {

	DEBUG("Rendering image\n");
	if (!im_image || !im_image_path) {
		printf("No image to render\n");
		return;
	}

	
	DEBUG("Rendering onto drawable\n");
	imlib_render_image_on_drawable_at_size(0, 0, ww, wh);
	DEBUG("Returning from render\n");

}

/* get currently playing song from mpd and update X window */
void update_mpd_song(void) {

	static int song_id;
	static int old_song_id = -1;

	struct mpd_song* song;

	mpd_send_current_song(connection);
	song = mpd_recv_song(connection);

	if (!song) {
		warn("Failed to get song from mpd");
		imlib_update(0);
		set_window_name("None");
		return;
	}

	song_id = mpd_song_get_id(song);

	bool updated = false;

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
			size_t name_cnt = 0;
			size_t name_cap = 20;
			char** names = xmalloc(sizeof(char*) * name_cap);
			struct dirent* ent;
			while (ent = readdir(dir)) {
				if (is_image_name(ent->d_name)) {
					name_cnt++;
					if (name_cnt > name_cap) {
						name_cap *= 2;
						names = xrealloc(names, sizeof(char*) * name_cap);
					}
					names[name_cnt-1] = ent->d_name;
				}
			}

			if (name_cnt > 0) {
				char* name = most_relevant_name(names, name_cnt);
				name = asprintf("%s/%s", dirname, name);
				DEBUG(name);
				imlib_update(name);
				updated = true;
			}
			free(names);
		}

		if (!updated)
			imlib_update(0);

		closedir(dir);

		free(pretty_name);
		free(path);

	}

	old_song_id = song_id;

	mpd_song_free(song);
}

/* check and handle errors */
void mpd_check_error(void) {
	switch (mpd_connection_get_error(connection)) {
		case MPD_ERROR_OOM:
		case MPD_ERROR_ARGUMENT:
		case MPD_ERROR_SYSTEM:
			die(mpd_connection_get_error_message(connection));
		case MPD_ERROR_SERVER:
		case MPD_ERROR_MALFORMED:
			warn(mpd_connection_get_error_message(connection));
		case MPD_ERROR_RESOLVER:
		case MPD_ERROR_CLOSED:
			while(mpd_connection_get_error(connection) != MPD_ERROR_SUCCESS) {
				warn("Unable to connect to mpd, retrying in 5 seconds");
				sleep(5);

				mpd_connection_free(connection);
				connection = mpd_connection_new(mpd_host, mpd_port, mpd_timeout);

				if (!connection)
					die("Unable to allocate memory for mpd_connection struct");
			}
			printf("Successfully connected to mpd again!\n");
	}
}

int main(int argc, char** argv) {

	/* get args from environment */
	mpd_db_dir =    getenv("MPDART_DIR");
	mpd_host =      getenv("MPDART_HOST");

	char* port =    getenv("MPDART_PORT");
	mpd_port =      port ? atoi(port) : 0;

	char* timeout = getenv("MPDART_TIMEOUT");
	mpd_timeout =   timeout ? atoi(timeout) : 0;

	/* parse args */
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
		die("Please specify mpd music directory with -d, or in the MPDART_DIR environment variable");

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

	const unsigned int* version = mpd_connection_get_server_version(connection);
	printf("Connected to mpd server version %d.%d.%d\n", version[0], version[1], version[2]);

	xdisplay = XOpenDisplay(0);
	if (!xdisplay)
		die("Cannot open display");

	xscreen   = XDefaultScreen(xdisplay);
	gc        = DefaultGC(xdisplay, xscreen);
	xvisual   = DefaultVisual(xdisplay, xscreen);
	xdepth    = DefaultDepth(xdisplay, xscreen);
	xcolormap = DefaultColormap(xdisplay, xscreen);

	Window xparent = XRootWindow(xdisplay, xscreen);

	unsigned x = 0, y = 0;
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

	XSelectInput(xdisplay, xwindow, ExposureMask
			| StructureNotifyMask
			| ButtonPressMask);
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

	/* get currently playing song before waiting for new ones */
	update_mpd_song();
	imlib_render();

	mpd_fd = mpd_connection_get_fd(connection);
	mpd_check_error();

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

	mpd_send_idle_mask(connection, MPD_IDLE_PLAYER);
	mpd_check_error();

	/* mpd event loop */
	while (1) {
		/* sleep for a day at a time */
		DEBUG("Sleeping\n");
		int ready_fds = poll(fds, 2, 86400);
		if (ready_fds < 0) {
			die("Error in poll");
		} else if (ready_fds > 0) {
			/* X event loop */
			if (fds[0].revents & POLLIN) {
				bool render = false;
				while (XPending(xdisplay)) {
					XEvent ev;
					XNextEvent(xdisplay, &ev);

					switch (ev.type) {
						/* close window */
						case ClientMessage:
							XCloseDisplay(xdisplay);
							die("Window Closed");
							break;
						/* redraw when off screen */
						case Expose:
							render = true;
							break;
						/* respond to resize */
						case ConfigureNotify:
							if (ww != ev.xconfigure.width || wh != ev.xconfigure.height) {
								ww = ev.xconfigure.width;
								wh = ev.xconfigure.height;
								render = true;
							}
							break;
						/* toggle pause on press */
						case ButtonPress:

							switch (ev.xbutton.button) {
								case Button1:
									DEBUG("Toggling pause\n");

									mpd_run_noidle(connection);
									mpd_check_error();

									/* deprecated but they provide nothing better so fuck them */
									mpd_run_toggle_pause(connection);
									mpd_check_error();

									mpd_send_idle_mask(connection, MPD_IDLE_PLAYER);
									mpd_check_error();
									break;
								case Button3:
									DEBUG("Exiting due to MB2 pree");
									exit(0);
									break;

							}
							break;
					}
				}
				if (render)
					imlib_render();
			}
			/* MPD event loop */
			if (fds[1].revents & POLLIN) {
				mpd_run_noidle(connection);
				mpd_check_error();

				update_mpd_song();
				imlib_render();

				mpd_send_idle_mask(connection, MPD_IDLE_PLAYER);
				mpd_check_error();
			}
		}
	}
}
