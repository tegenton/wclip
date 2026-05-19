#include <stdlib.h>
#include <stdio.h>
#include <sys/mman.h>
#include <unistd.h>

#include "ext-data-control-v1.h"

#include "wayland.h"
#include "config.h"

ssize_t
copy_fd_to_buf(int fd, data_t *buf) {
	ssize_t len = 0;
	while (buf->size < max_copy_size && (len = read(fd, buf->data + buf->size, max_copy_size - buf->size)) > 0) {
		buf->size += len;
		if (buf->size == max_copy_size)
			fprintf(stderr, "Maximum copy size reached, truncating\n");
	}
	return len;
}

int
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

int
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

	wl_display_dispatch(wl_conn->display);

	if (!offers[1]) {
		fprintf(stderr, "Nothing is copied\n");
		goto cleanup;
	}

	ext_data_control_offer_v1_receive(offers[1], "text/plain", STDOUT_FILENO);
	wl_display_roundtrip(wl_conn->display);

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
	int mode = 0, opt = 0;
	wl_t *wl_conn = NULL;

	while ((opt = getopt(argc, argv, "io")) != -1) {
		switch (opt) {
			case 'i':
				mode = 0;
				break;
			case 'o':
				mode = 1;
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

	if (mode == 0) {
		if (copy(wl_conn))
			goto cleanup;
	} else {
		if (paste(wl_conn))
			goto cleanup;
	}

	close_connection(wl_conn);
	return EXIT_SUCCESS;

cleanup:
	if (wl_conn)
		close_connection(wl_conn);
	return EXIT_FAILURE;
}
