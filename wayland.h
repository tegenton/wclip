#include <stddef.h>

struct ext_data_control_offer_v1;

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

int open_connection(wl_t *wl_conn);
void close_connection(wl_t *wl_conn);

int offer_data(wl_t *wl_conn, data_t *buf);
int check_offers(wl_t *wl_conn, struct ext_data_control_offer_v1 **offers);
