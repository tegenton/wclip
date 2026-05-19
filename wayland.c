#include <magic.h>                    // for magic_buffer
#include <stdlib.h>                   // for free, malloc, exit
#include <string.h>                   // for strcmp
#include <sys/mman.h>                 // for munmap
#include <sys/types.h>                // for ssize_t
#include <unistd.h>                   // for close, write
#include <wayland-client-core.h>      // for wl_display
#include <wayland-client-protocol.h>  // for wl_registry
#include <wayland-util.h>             // for wl_interface
#include "ext-data-control-v1.h"      // for ext_data_control

#include "wayland.h"

struct ext_data_control_device_v1;
struct ext_data_control_offer_v1;
struct ext_data_control_source_v1;
struct wl_registry;

static void on_global_add(void *data, struct wl_registry *registry, unsigned int name, const char *iface, unsigned int ver);
static void on_global_remove(void *data, struct wl_registry *registry, unsigned int name);

static void on_send(void *data, struct ext_data_control_source_v1* source, const char *mime, int fd);
static void on_cancel(void *data, struct ext_data_control_source_v1 *source);

static void on_mime(void *data, struct ext_data_control_offer_v1 *offer, const char *mime);

static void on_offer(void *data, struct ext_data_control_device_v1 *device, struct ext_data_control_offer_v1 *offer);
static void on_selection(void *data, struct ext_data_control_device_v1 *device, struct ext_data_control_offer_v1 *offer);
static void on_primary_selection(void *data, struct ext_data_control_device_v1 *device, struct ext_data_control_offer_v1 *offer);
static void on_finished(void *data, struct ext_data_control_device_v1 *device);

int open_connection(wl_t *wl_conn);
void close_connection(wl_t *wl_conn);

static char* check_mime(data_t *buf);

int offer_data(wl_t *wl_conn, data_t *buf);
int check_offers(wl_t *wl_conn, struct ext_data_control_offer_v1 **offers);

static void
on_global_add(void *data, struct wl_registry *registry, unsigned int name, const char *iface, unsigned int ver) {
	wl_t *wl_conn = (wl_t*) data;
	if (!strcmp(iface, wl_seat_interface.name))
		wl_conn->seat = wl_registry_bind(registry, name, &wl_seat_interface, ver);
	else if (!strcmp(iface, ext_data_control_manager_v1_interface.name))
		wl_conn->data_control_manager = wl_registry_bind(registry, name, &ext_data_control_manager_v1_interface, ver);
}

static void
on_global_remove(void *data, struct wl_registry *registry, unsigned int name) {
}

static void
on_send(void *data, struct ext_data_control_source_v1* source, const char *mime_type, int fd) {
	data_t *buf = (data_t*) data;
	size_t offset = 0;
	ssize_t len = 0;

	while (offset < buf->size && (len = write(fd, buf->data + offset, buf->size - offset)) > -1)
		offset += len;
	close(fd);
}

static void
on_cancel(void *data, struct ext_data_control_source_v1 *source) {
	data_t *buf = (data_t*) data;
	if (buf->data)
		munmap(buf->data, buf->size);
	free(buf);

	ext_data_control_source_v1_destroy(source);

	// todo: maybe pass this around in data for explicit release?
	//close_connection(wl_conn);

	exit(EXIT_SUCCESS);
}

static void
on_mime(void *data, struct ext_data_control_offer_v1 *offer, const char *mime) {
	struct ext_data_control_offer_v1 **offers = (struct ext_data_control_offer_v1**) data;

	offers[0] = offer;
}

static void
on_offer(void *data, struct ext_data_control_device_v1 *device, struct ext_data_control_offer_v1 *offer) {
	struct ext_data_control_offer_v1 **offers = (struct ext_data_control_offer_v1**) data;
	struct ext_data_control_offer_v1_listener *listener = NULL;

	if (!(listener = malloc(sizeof(struct ext_data_control_offer_v1_listener))))
		goto cleanup;

	listener->offer = &on_mime;

	if (ext_data_control_offer_v1_add_listener(offer, listener, offers))
		goto cleanup;

	return;

cleanup:
	if (listener)
		free(listener);
}

static void
on_selection(void *data, struct ext_data_control_device_v1 *device, struct ext_data_control_offer_v1 *offer) {
	struct ext_data_control_offer_v1 **offers = (struct ext_data_control_offer_v1**) data;
	if (offers[0] == offer) {
		if (offers[1])
			ext_data_control_offer_v1_destroy(offers[1]);
		offers[1] = offer;
		offers[0] = NULL;
	}
}

static void
on_primary_selection(void *data, struct ext_data_control_device_v1 *device, struct ext_data_control_offer_v1 *offer) {
	struct ext_data_control_offer_v1 **offers = (struct ext_data_control_offer_v1**) data;
	if (offers[0] == offer) {
		if (offers[2])
			ext_data_control_offer_v1_destroy(offers[2]);
		offers[2] = offer;
		offers[0] = NULL;
	}
}

static void
on_finished(void *data, struct ext_data_control_device_v1 *device) {
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
		//fprintf(stderr, "No registered Wayland %s\n", (wl_conn->seat) ? "data control manager" : "seat");
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

static char*
check_mime(data_t *buf) {
	magic_t cookie = NULL;
	const char *mime = NULL;
	char *mut_mime = NULL;

	if (!(cookie = magic_open(MAGIC_MIME_TYPE)))
		goto cleanup;

	if (magic_load(cookie, NULL))
		goto cleanup;

	if (!(mime = magic_buffer(cookie, buf->data, buf->size)))
		goto cleanup;

	if (!strcmp("text/plain", mime))
		mime = "text/plain;charset=utf-8";

	mut_mime = strdup(mime);
	magic_close(cookie);
	return mut_mime;

cleanup:
	if (cookie)
		magic_close(cookie);
	return NULL;
}

int
offer_data(wl_t *wl_conn, data_t *buf) {
	struct ext_data_control_source_v1 *source = NULL;
	struct ext_data_control_source_v1_listener *listener = NULL;
	char *mime = NULL;

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

	if (!(mime = check_mime(buf))) {
		goto cleanup;
	}

	ext_data_control_source_v1_offer(source, mime);
	free(mime);

	ext_data_control_device_v1_set_selection(wl_conn->data_control_device, source);

	return 0;

cleanup:
	if (listener)
		free(listener);
	if (source)
		ext_data_control_source_v1_destroy(source);
	return -1;
}

int
check_offers(wl_t *wl_conn, struct ext_data_control_offer_v1 **offers) {
	struct ext_data_control_device_v1_listener *listener = NULL;

	if (!(listener = malloc(sizeof(struct ext_data_control_device_v1_listener)))) {
		goto cleanup;
	}

	listener->data_offer = &on_offer;
	listener->selection = &on_selection;
	listener->primary_selection = &on_primary_selection;
	listener->finished = &on_finished;

	if (ext_data_control_device_v1_add_listener(wl_conn->data_control_device, listener, offers)) {
		goto cleanup;
	}

	return 0;

cleanup:
	if (listener)
		free(listener);
	return -1;
}


