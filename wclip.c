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
	struct ext_data_control_manager_v1 *data_control_manager;
	struct ext_data_control_device_v1 *data_control_device;
} wl_t;

typedef struct data_s {
	size_t size;
	char *data;
} data_t;

void
on_global_add(void *data, struct wl_registry *registry, unsigned int name, const char *iface, unsigned int ver) {
	wl_t *wl_conn = (wl_t*) data;
	if (!strcmp(iface, wl_seat_interface.name))
		wl_conn->seat = wl_registry_bind(registry, name, &wl_seat_interface, ver);
	else if (!strcmp(iface, ext_data_control_manager_v1_interface.name))
		wl_conn->data_control_manager = wl_registry_bind(registry, name, &ext_data_control_manager_v1_interface, ver);
}

void
on_global_remove(void *data, struct wl_registry *registry, unsigned int name) {
}

int
open_connection(wl_t *wl_conn) {
	struct wl_registry *registry = NULL;
	struct wl_registry_listener *listener = NULL;

	if (!(wl_conn->display = wl_display_connect(NULL))) {
		//perror("Could not connect to Wayland display");
		goto cleanup;
	}

	if (!(registry = wl_display_get_registry(wl_conn->display))) {
		//perror("Could not access Wayland registry");
		goto cleanup;
	}

	if (!(listener = malloc(sizeof(struct wl_registry_listener)))) {
		//perror("Could not allocate memory");
		goto cleanup;
	}

	listener->global = &on_global_add;
	listener->global_remove = &on_global_remove;

	if (wl_registry_add_listener(registry, listener, wl_conn) < 0) {
		//perror("Could not install Wayland registry listener");
		goto cleanup;
	}

	if (wl_display_roundtrip(wl_conn->display) < 0) {
		//perror("Could not process pending Wayland requests");
		goto cleanup;
	}

	wl_registry_destroy(registry);
	registry = NULL;
	free(listener);
	listener = NULL;

	if (!wl_conn->data_control_manager || !wl_conn->seat) {
		//fprintf(stderr, "No registered Wayland %s\n", (wl_conn->seat) ? "data device manager" : "seat");
		goto cleanup;
	}

	if (!(wl_conn->data_control_device = ext_data_control_manager_v1_get_data_device(wl_conn->data_control_manager, wl_conn->seat))) {
		//perror("Could not get Wayland data device");
		goto cleanup;
	}

	return 0;

cleanup:
	if (listener)
		free(listener);
	if (registry)
		wl_registry_destroy(registry);
	return -1;
}

void
close_connection(wl_t *wl_conn) {
	if (wl_conn->data_control_device)
		ext_data_control_device_v1_destroy(wl_conn->data_control_device);
	if (wl_conn->data_control_manager)
		ext_data_control_manager_v1_destroy(wl_conn->data_control_manager);
	if (wl_conn->seat)
		wl_seat_release(wl_conn->seat);
	if (wl_conn->display)
		wl_display_disconnect(wl_conn->display);
}

void
on_send(void *data, struct ext_data_control_source_v1* source, const char *mime_type, int fd) {
	data_t *buf = (data_t*) data;
	size_t offset = 0;
	ssize_t len = 0;

	while (offset < buf->size && (len = write(fd, buf->data + offset, buf->size - offset)) > -1)
		offset += len;
	close(fd);
}

void
on_cancel(void *data, struct ext_data_control_source_v1 *source) {
	data_t *buf = (data_t*) data;
	if (buf->data)
		munmap(buf->data, max_copy_size);

	ext_data_control_source_v1_destroy(source);

	// todo: maybe pass this around in data for explicit release?
	//close_connection(wl_conn);

	exit(EXIT_SUCCESS);
}

int
offer_data(wl_t *wl_conn, data_t *buf) {
	struct ext_data_control_source_v1 *source = NULL;
	struct ext_data_control_source_v1_listener *listener = NULL;

	if (!(source = ext_data_control_manager_v1_create_data_source(wl_conn->data_control_manager))) {
		goto cleanup;
	}

	if (!(listener = malloc(sizeof(struct ext_data_control_source_v1_listener)))) {
		goto cleanup;
	}

	listener->send = &on_send;
	listener->cancelled = &on_cancel;

	if (ext_data_control_source_v1_add_listener(source, listener, buf)) {
		goto cleanup;
	}

	ext_data_control_source_v1_offer(source, "text/plain");

	ext_data_control_device_v1_set_selection(wl_conn->data_control_device, source);

	return 0;

cleanup:
	if (listener)
		free(listener);
	if (source)
		ext_data_control_source_v1_destroy(source);
	return -1;
}

void on_mime(void *data, struct ext_data_control_offer_v1 *offer, const char *mime_type) {
	struct ext_data_control_offer_v1 **offers = (struct ext_data_control_offer_v1**) data;

	if (!strcmp("text/plain", mime_type)) {
		offers[0] = offer;
	}
}

void
on_offer(void *data, struct ext_data_control_device_v1 *device, struct ext_data_control_offer_v1 *id) {
	struct ext_data_control_offer_v1 **offers = (struct ext_data_control_offer_v1**) data;
	struct ext_data_control_offer_v1_listener *listener = NULL;

	if (!(listener = malloc(sizeof(struct ext_data_control_offer_v1_listener))))
		goto cleanup;

	listener->offer = &on_mime;

	if (ext_data_control_offer_v1_add_listener(id, listener, offers))
		goto cleanup;

	return;

cleanup:
	if (listener)
		free(listener);
}

void
on_selection(void *data, struct ext_data_control_device_v1 *device, struct ext_data_control_offer_v1 *id) {
	struct ext_data_control_offer_v1 **offers = (struct ext_data_control_offer_v1**) data;
	if (offers[0] == id) {
		if (offers[1])
			ext_data_control_offer_v1_destroy(offers[1]);
		offers[1] = id;
		offers[0] = NULL;
	} else {
		fprintf(stderr, "unexpected item in bagging area\n");
	}
}

void
on_finished(void *data, struct ext_data_control_device_v1 *device) {
}

void
on_primary_selection(void *data, struct ext_data_control_device_v1 *device, struct ext_data_control_offer_v1 *id) {
	struct ext_data_control_offer_v1 **offers = (struct ext_data_control_offer_v1**) data;
	if (offers[0] == id) {
		if (offers[2])
			ext_data_control_offer_v1_destroy(offers[2]);
		offers[2] = id;
		offers[0] = NULL;
	} else {
		fprintf(stderr, "unexpected item in primary bagging area\n");
	}
}

int
check_offers(wl_t *wl_conn, struct ext_data_control_offer_v1 **offers) {
	struct ext_data_control_device_v1_listener *listener = NULL;

	if (!(listener = malloc(sizeof(struct ext_data_control_device_v1_listener)))) {
		goto cleanup;
	}

	listener->data_offer = &on_offer;
	listener->selection = &on_selection;
	listener->finished = &on_finished;
	listener->primary_selection = &on_primary_selection;

	if (ext_data_control_device_v1_add_listener(wl_conn->data_control_device, listener, offers)) {
		goto cleanup;
	}

	return 0;

cleanup:
	if (listener)
		free(listener);
	return -1;
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
