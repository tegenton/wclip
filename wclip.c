#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <unistd.h>

#include <wayland-client.h>
#include "ext-data-control-v1.h"

#include "config.h"


typedef struct wl_s {
	struct wl_display *display;
	struct wl_seat *seat;
	struct wl_data_device_manager *data_device_manager;
	struct ext_data_control_manager_v1 *data_control_manager;
	struct wl_data_device *data_device;
} wl_t;

typedef struct data_s {
	size_t size;
	char *data;
} data_t;

void
on_global_add(void *data, struct wl_registry *registry, unsigned int name, const char *iface, unsigned int ver) {
	printf("%s ver %d\n", iface, ver);
	wl_t *wl_conn = (wl_t*) data;
	if (!strcmp(iface, wl_data_device_manager_interface.name))
		wl_conn->data_device_manager = wl_registry_bind(registry, name, &wl_data_device_manager_interface, ver);
	else if (!strcmp(iface, wl_seat_interface.name))
		wl_conn->seat = wl_registry_bind(registry, name, &wl_seat_interface, ver);
	else if (!strcmp(iface, ext_data_control_manager_v1_interface.name))
		wl_conn->data_control_manager = wl_registry_bind(registry, name, &ext_data_control_manager_v1_interface, ver);
}

void
on_global_remove(void *data, struct wl_registry *registry, unsigned int name) {
	fprintf(stderr, "removed: %d\n", name);
}

void
on_target(void *data, struct wl_data_source *wl_data_source, const char *mime_type) {
	fprintf(stderr, "target\n");
}

void
on_send(void *data, struct wl_data_source* wl_data_source, const char *mime_type, int fd) {
	fprintf(stderr, "send\n");
	data_t *buf = (data_t*) data;
	size_t offset = 0;
	ssize_t len;

	while (offset < buf->size && (len = write(fd, buf->data + offset, buf->size - offset) > -1))
		offset += len;
}

void
on_cancel(void *data, struct wl_data_source *wl_data_source) {
	fprintf(stderr, "cancel\n");
	data_t *buf = (data_t*) data;
	if (buf->data)
		munmap(buf->data, max_copy_size);

	// todo: maybe pass this around in data for explicit release?
	//close_connection(wl_conn);

	exit(EXIT_SUCCESS);
}

void
on_dnd_drop(void *data, struct wl_data_source *wl_data_source) {
	fprintf(stderr, "drop\n");
}

void
on_dnd_finish(void *data, struct wl_data_source *wl_data_source) {
	fprintf(stderr, "finish\n");
}

void
on_action(void *data, struct wl_data_source *wl_data_source, unsigned int action) {
	fprintf(stderr, "action\n");
}

int
open_connection(wl_t *wl_conn) {
	struct wl_registry *registry;
	struct wl_registry_listener *registry_listener;

	if (!(wl_conn->display = wl_display_connect(NULL))) {
		//perror("Could not connect to Wayland display");
		return 1;
	}

	if (!(registry = wl_display_get_registry(wl_conn->display))) {
		//perror("Could not access Wayland registry");
		return 2;
	}

	if (!(registry_listener = malloc(sizeof(struct wl_registry_listener)))) {
		//perror("Could not allocate memory");
		wl_registry_destroy(registry);
		return 3;
	}

	registry_listener->global = &on_global_add;
	registry_listener->global_remove = &on_global_remove;

	if (wl_registry_add_listener(registry, registry_listener, wl_conn) < 0) {
		//perror("Could not install Wayland registry listener");
		wl_registry_destroy(registry);
		free(registry_listener);
		return 4;
	}

	if (wl_display_roundtrip(wl_conn->display) < 0) {
		//perror("Could not process pending Wayland requests");
		wl_registry_destroy(registry);
		free(registry_listener);
		return 5;
	}

	wl_registry_destroy(registry);
	free(registry_listener);

	if (!wl_conn->data_device_manager || !wl_conn->seat) {
		//fprintf(stderr, "No registered Wayland %s\n", (wl_conn->seat) ? "data device manager" : "seat");
		return 6;
	}

	if (!(wl_conn->data_device = wl_data_device_manager_get_data_device(wl_conn->data_device_manager, wl_conn->seat))) {
		//perror("Could not get Wayland data device");
		return 7;
	}

	return 0;
}

void
close_connection(wl_t *wl_conn) {
	if (wl_conn->data_device)
		wl_data_device_release(wl_conn->data_device);
	if (wl_conn->data_device_manager)
		wl_data_device_manager_destroy(wl_conn->data_device_manager);
	if (wl_conn->seat)
		wl_seat_release(wl_conn->seat);
	if (wl_conn->display)
		wl_display_disconnect(wl_conn->display);
}

int
offer_data(wl_t *wl_conn, data_t *buf) {
	struct wl_data_source *data_source;
	struct wl_data_source_listener *data_source_listener;

	if (!(data_source = wl_data_device_manager_create_data_source(wl_conn->data_device_manager))) {
		return 1;
	}

	if (!(data_source_listener = malloc(sizeof(struct wl_data_source_listener)))) {
		return 2;
	}

	data_source_listener->target = &on_target;
	data_source_listener->send = &on_send;
	data_source_listener->cancelled = &on_cancel;
	data_source_listener->dnd_drop_performed = &on_dnd_drop;
	data_source_listener->dnd_finished = &on_dnd_finish;
	data_source_listener->action = &on_action;

	if (wl_data_source_add_listener(data_source, data_source_listener, buf)) {
		return 3;
	}

	wl_data_source_offer(data_source, "text/plain");

	wl_data_device_set_selection(wl_conn->data_device, data_source, 0);

	return 0;
}

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
main(int argc, char *argv[]) {
	int mode, opt;
	data_t *copy_buffer = NULL;
	wl_t *wl_conn = NULL;

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

	if (!(wl_conn = malloc(sizeof(wl_t)))) {
		perror("Could not allocate memory");
		goto cleanup;
	}

	if (open_connection(wl_conn)) {
		perror("Could not open Wayland connection");
		goto cleanup;
	}

	if (mode == 0) {
		/* copying */
		if (!(copy_buffer = malloc(sizeof(data_t)))) {
			perror("Could not allocate memory");
			goto cleanup;
		}

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

		int h;
		while ((h = wl_display_dispatch(wl_conn->display)) > -1)
			printf("dispatched %d events\n", h);

		goto cleanup;
	} else {
		printf("paste logic goes here\n");
		close_connection(wl_conn);
	}

	return EXIT_SUCCESS;

cleanup:
	if (copy_buffer) {
		if (copy_buffer->data)
			if (munmap(copy_buffer->data, max_copy_size))
				perror("And another error deleting unmapping");
		free(copy_buffer);
	}
	if (wl_conn) {
		close_connection(wl_conn);
		free(wl_conn);
	}
	return EXIT_FAILURE;
}
