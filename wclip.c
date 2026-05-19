#include <fcntl.h>                // for open
#include <stdio.h>                // for perror, fprintf
#include <stdlib.h>               // for free, malloc, exit
#include <sys/mman.h>             // for mmap, munmap
#include <sys/types.h>            // for ssize_t
#include <unistd.h>               // for daemon, getopt, read
#include <wayland-client-core.h>  // for wl_display
#include "ext-data-control-v1.h"  // for ext_data_control_offer_v1_destroy

#include "wayland.h"
#include "config.h"

typedef struct flag_s{
	int fd;
	int (*mode)(wl_t*, struct flag_s);
} flag_t;

static ssize_t copy_fd_to_buf(int fd, data_t *buf);

static int copy(wl_t *wl_conn, flag_t f);
static int paste(wl_t *wl_conn, flag_t f);

static ssize_t
copy_fd_to_buf(int fd, data_t *buf) {
	ssize_t len = 0;
	while (buf->size < max_copy_size && (len = read(fd, buf->data + buf->size, max_copy_size - buf->size)) > 0) {
		buf->size += len;
		if (buf->size == max_copy_size)
			fprintf(stderr, "Maximum copy size reached, truncating\n");
	}
	if (len < 0)
		return len;
	munmap(buf->data + buf->size, max_copy_size - buf->size);
	return buf->size;
}

static int
copy(wl_t *wl_conn, flag_t f) {
	data_t *copy_buffer = NULL;

	if (!(copy_buffer = malloc(sizeof(data_t)))) {
		perror("Could not allocate memory");
		goto cleanup;
	}

	copy_buffer->size = 0;

	if ((copy_buffer->data = mmap(NULL, max_copy_size, PROT_READ | PROT_WRITE, MAP_SHARED | MAP_ANONYMOUS, -1, 0)) == MAP_FAILED) {
		perror("Could not map copy buffer");
		goto cleanup;
	}

	if (copy_fd_to_buf(f.fd, copy_buffer) < 0) {
		perror("Could not copy stdin to buffer");
		goto cleanup;
	}

	if (offer_data(wl_conn, copy_buffer)) {
		perror("Could not install Wayland listener");
		goto cleanup;
	}

	if (daemon(0, 0) < 0) {
		perror("Could not daemonize");
		goto cleanup;
	}

	while (wl_display_dispatch(wl_conn->display) > -1);

cleanup:
	if (copy_buffer) {
		if (copy_buffer->data)
			if (munmap(copy_buffer->data, max_copy_size))
				perror("And another error deleting unmapping");
		free(copy_buffer);
	}
	return -1;
}

static int
paste(wl_t *wl_conn, flag_t f) {
	struct ext_data_control_offer_v1 **offers = NULL;

	if (!(offers = malloc(sizeof(struct ext_data_control_offer_v1*) * 3))) {
		perror("Could not allocate memory");
		goto cleanup;
	}

	if (check_offers(wl_conn, offers) < 0) {
		perror("Could not install Wayland listener");
		goto cleanup;
	}

	if (wl_display_roundtrip(wl_conn->display) < 0) {
		perror("Could not process Wayland requests");
		goto cleanup;
	}

	if (!offers[1]) {
		fprintf(stderr, "Nothing is copied\n");
		goto cleanup;
	}

	ext_data_control_offer_v1_receive(offers[1], "text/plain", f.fd);

	if (wl_display_roundtrip(wl_conn->display) < 0) {
		perror("Could not process paste request");
		goto cleanup;
	}

	for (int i = 0; i < 3; i++)
		if (offers[i])
			ext_data_control_offer_v1_destroy(offers[i]);
	free(offers);
	return 0;

cleanup:
	if (offers) {
		for (int i = 0; i < 3; i++)
			if (offers[i])
				ext_data_control_offer_v1_destroy(offers[i]);
		free(offers);
	}
	return -1;
}

int
main(int argc, char *argv[]) {
	int opt = 0;
	flag_t f = {-1, &copy};
	wl_t *wl_conn = NULL;

	while ((opt = getopt(argc, argv, "io")) != -1) {
		switch (opt) {
			case 'i':
				f.mode = &copy;
				break;
			case 'o':
				f.mode = &paste;
				break;
			default:
				fprintf(stderr, "Usage: %s [-i|-o] [file]\n", argv[0]);
				return EXIT_FAILURE;
		}
	}

	if (optind < argc) {
		int flags = 0;
		if (optind + 1 < argc) {
			fprintf(stderr, "Usage: %s [-i|-o] [file]\n", argv[0]);
			return EXIT_FAILURE;
		}
		if (f.mode == &copy)
			flags = O_RDONLY;
		else
			flags = O_WRONLY | O_CREAT | O_TRUNC;
		if ((f.fd = open(argv[optind], flags)) < 0) {
			perror("Could not open file");
			goto cleanup;
		}
	} else {
		if (f.mode == &copy)
			f.fd = STDIN_FILENO;
		else
			f.fd = STDOUT_FILENO;
	}

	if (!(wl_conn = malloc(sizeof(wl_t)))) {
		perror("Could not allocate memory");
		goto cleanup;
	}

	if (open_connection(wl_conn)) {
		perror("Could not open Wayland connection");
		goto cleanup;
	}

	if (f.mode(wl_conn, f)) {
		goto cleanup;
	}

	close_connection(wl_conn);
	return EXIT_SUCCESS;

cleanup:
	if (f.fd != -1)
		close(f.fd);
	if (wl_conn)
		close_connection(wl_conn);
	return EXIT_FAILURE;
}
