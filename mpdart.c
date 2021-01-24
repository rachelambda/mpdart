#include <stdlib.h>
#include <string.h>
#include <stdio.h>

#include <unistd.h>

#include <mpd/client.h>

char* mpd_host = 0;
unsigned mpd_port = 0;
unsigned mpd_timeout = 0;
struct mpd_connection* connection = 0;

char* mpd_db_dir = 0;

int main(int argc, char** argv) {

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
	
	if (!mpd_db_dir) {
		fprintf(stderr, "Please specify mpd music directory with -d\n");
		exit(1);
	}


	/* strip all '/'es from the end of it */
	{
		int i = 0;
		for (; mpd_db_dir[i]; i++);
		while (mpd_db_dir[--i] == '/');
		mpd_db_dir[i+1] = '\0';
	}
	
	connection = mpd_connection_new(mpd_host, mpd_port, mpd_timeout);

	if (!connection) {
		fprintf(stderr, "Unable to allocate memory for mpd_connection struct\n");
		exit(1);
	}

	if (mpd_connection_get_error(connection) != MPD_ERROR_SUCCESS) {
		fprintf(stderr, "%s\n", mpd_connection_get_error_message(connection));
		exit(1);
	}

	const int* version = mpd_connection_get_server_version(connection);
	printf("Connected to mpd server version %d.%d.%d\n", version[0], version[1], version[2]);

	int song_id;
	int old_song_id = -1;

	while (1) {

		struct mpd_song* song;

		mpd_send_current_song(connection);
		song = mpd_recv_song(connection);

		if (!song) {
			fprintf(stderr, "failed to get mpd song\n");
			/* TODO clear image */
			goto wait;
		}

		song_id = mpd_song_get_id(song);

		if (song_id != old_song_id) {
			/* TODO render image */
			printf("Now playing: '%s' by '%s' from '%s/%s'\n",
					mpd_song_get_tag(song, MPD_TAG_TITLE, 0), 
					mpd_song_get_tag(song, MPD_TAG_ARTIST, 0), 
					mpd_db_dir, mpd_song_get_uri(song));
		}

		old_song_id = song_id;

		mpd_song_free(song);

wait:
		mpd_send_idle_mask(connection, MPD_IDLE_PLAYER);
		while (!mpd_recv_idle(connection, 1))
			sleep(1);
	}

}
