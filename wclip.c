#include <stdio.h>                // for perror, fprintf, NULL, stderr
#include <stdlib.h>               // for free, malloc, exit
#include <sys/mman.h>             // for mmap, munmap
#include <sys/types.h>            // for ssize_t
#include <unistd.h>               // for daemon, getopt, read
#include <wayland-client-core.h>  // for wl_display
#include "ext-data-control-v1.h"  // for ext_data_control_offer_v1_destroy

#include "wayland.h"
#include "config.h"

static ssize_t copy_fd_to_buf(int fd, data_t *buf);

static int copy(wl_t *wl_conn);
static int paste(wl_t *wl_conn);

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
copy(wl_t *wl_conn) {
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

	if (copy_fd_to_buf(STDIN_FILENO, copy_buffer) < 0) {
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
paste(wl_t *wl_conn) {
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

	ext_data_control_offer_v1_receive(offers[1], "text/plain", STDOUT_FILENO);

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
	int (*mode)(wl_t*) = &copy;
	wl_t *wl_conn = NULL;

	while ((opt = getopt(argc, argv, "io")) != -1) {
		switch (opt) {
			case 'i':
				mode = &copy;
				break;
			case 'o':
				mode = &paste;
				break;
			default:
				fprintf(stderr, "Usage: %s [-i|-o]\n", argv[0]);
				return EXIT_FAILURE;
		}
	}

	if (!(wl_conn = malloc(sizeof(wl_t)))) {
		perror("Could not allocate memory");
		goto cleanup;
	}

	if (open_connection(wl_conn)) {
		perror("Could not open Wayland connection");
		goto cleanup;
	}

	if (mode(wl_conn)) {
		goto cleanup;
	}

	close_connection(wl_conn);
	return EXIT_SUCCESS;

cleanup:
	if (wl_conn)
		close_connection(wl_conn);
	return EXIT_FAILURE;
}
