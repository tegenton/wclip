#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <unistd.h>

#include <wayland-client.h>

#include "config.h"

typedef struct wl_s {
	struct wl_display *display;
	struct wl_seat *seat;
	struct wl_data_device *data_device;
	struct wl_data_device_manager *data_device_manager;
} wl_t;

typedef struct data_s {
	size_t size;
	void *data;
} data_t;

void
on_global_add(void *data, struct wl_registry *registry, unsigned int name, const char *iface, unsigned int ver) {
	wl_t *wl_conn = (wl_t*) data;
	if (!strcmp(iface, wl_data_device_manager_interface.name))
		wl_conn->data_device_manager = wl_registry_bind(registry, name, &wl_data_device_manager_interface, ver);
	else if (!strcmp(iface, wl_seat_interface.name))
		wl_conn->seat = wl_registry_bind(registry, name, &wl_seat_interface, ver);
}

int
open_connection(wl_t *wl_conn) {
	struct wl_registry *registry;
	struct wl_registry_listener registry_listener;

	if (!(wl_conn->display = wl_display_connect(NULL))) {
		perror("Could not connect to Wayland display");
		return 1;
	}

	if (!(registry = wl_display_get_registry(wl_conn->display))) {
		perror("Could not access Wayland registry");
		return 2;
	}

	registry_listener = (struct wl_registry_listener) {&on_global_add, NULL};

	if (wl_registry_add_listener(registry, &registry_listener, wl_conn) < 0) {
		perror("Could not install Wayland registry listener");
		return 3;
	}

	if (wl_display_roundtrip(wl_conn->display) < 0) {
		perror("Could not process pending Wayland requests");
		return 4;
	}

	wl_registry_destroy(registry);

	if (!wl_conn->data_device_manager || !wl_conn->seat) {
		fprintf(stderr, "No registered Wayland %s\n", (wl_conn->seat) ? "data device manager" : "seat");
		return 5;
	}

	if (!(wl_conn->data_device = wl_data_device_manager_get_data_device(wl_conn->data_device_manager, wl_conn->seat))) {
		perror("Could not get Wayland data device");
		return 6;
	}

	return 0;
}

void
close_connection(wl_t wl_conn) {
	if (wl_conn.display)
		wl_display_disconnect(wl_conn.display);
}

ssize_t
copy_fd_to_buf(int fd, data_t *buf) {
	ssize_t len = 0;
	size_t chunk_size = max_chunk_size;
	if (buf->size + chunk_size > max_copy_size)
		chunk_size = max_copy_size - buf->size;
	while (buf->size < max_copy_size && (len = read(fd, buf->data + buf->size, chunk_size)) > 0) {
		buf->size += len;
		if (buf->size == max_copy_size)
			fprintf(stderr, "Maximum copy size reached, truncating\n");
		else if (buf->size + chunk_size > max_copy_size)
			chunk_size = max_copy_size - buf->size;
	}
	return len;
}

int
main(int argc, char *argv[]) {
	int mode, opt;
	wl_t wl_conn;

	mode = 0; /* default to copying */
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

	wl_conn = (wl_t) {NULL, NULL, NULL, NULL};
	if (open_connection(&wl_conn) != 0) {
		close_connection(wl_conn);
		return EXIT_FAILURE;
	}

	if (mode == 0) {
		data_t copy_buffer = {0, NULL};
		/* copying */
		if ((copy_buffer.data = mmap(NULL, max_copy_size, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0)) == MAP_FAILED) {
			perror("Could not map copy buffer");
			close_connection(wl_conn);
			return EXIT_FAILURE;
		}
		if (copy_fd_to_buf(STDIN_FILENO, &copy_buffer) < 0) {
			perror("Could not copy stdin to buffer");
			if (munmap(copy_buffer.data, max_copy_size))
				perror("And another error deleting unmapping");
			close_connection(wl_conn);
			return EXIT_FAILURE;
		}

		//offer_data(wl_conn, &copy_buffer);
		printf("%s\n", (char*) copy_buffer.data);
	} else {
		printf("paste logic goes here\n");
		close_connection(wl_conn);
	}

	return EXIT_SUCCESS;
}
