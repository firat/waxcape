#include <libinput.h>
#include <libudev.h>
#include <fcntl.h>
#include <unistd.h>
#include <errno.h>
#include <stdio.h>
#include <poll.h>
#include <signal.h>
#include <string.h>
#include <stdbool.h>
#include <libevdev/libevdev.h>
#include <libevdev/libevdev-uinput.h>

static volatile sig_atomic_t stop = 0;
static struct libevdev_uinput* waxcape_keyboard;
static bool exclusive_grab = 0;
static int count = 0;

static void waxcape_handle_key_event(struct libinput_event* ev) {
    enum libinput_event_type ev_type = libinput_event_get_type(ev);
    struct libinput_device* dev = libinput_event_get_device(ev);

    struct libinput_event_keyboard* k = libinput_event_get_keyboard_event(ev);
    enum libinput_key_state state = libinput_event_keyboard_get_key_state(k);
    uint32_t key = libinput_event_keyboard_get_key(k);

    printf("Got event type: %d from %s - Key: %c, state: %d\n", ev_type,
           libinput_device_get_name(dev), key, state);

    libevdev_uinput_write_event(waxcape_keyboard, EV_KEY, key, state);
    libevdev_uinput_write_event(waxcape_keyboard, EV_SYN, SYN_REPORT, 0);
}

static bool is_waxcape_device(const char* device_name) {
    // return strcmp(uinput_name, device_name) == 0;
	return true;
}

static void waxcape_handle_events(struct libinput* li) {
    struct libinput_event* ev;
    libinput_dispatch(li);
    while ((ev = libinput_get_event(li)) != NULL) {
        struct libinput_device* dev = libinput_event_get_device(ev);
		printf("sysname: %s\n", libinput_device_get_sysname(dev));
		printf("name: %s\n", libinput_device_get_name(dev));

		
		printf("uinput syspath: %s\n", libevdev_uinput_get_syspath(waxcape_keyboard));
		printf("uinput devnode: %s\n", libevdev_uinput_get_devnode(waxcape_keyboard));

        struct udev_device* udev_dev = libinput_device_get_udev_device(dev);
		const char *dev_syspath = udev_device_get_syspath(udev_dev);
		struct udev_list_entry *udev_links = udev_device_get_devlinks_list_entry(udev_dev);
		do {
			printf("udev devlink: %s\n", udev_list_entry_get_value(udev_links));
		} while ((udev_links = udev_list_entry_get_next(udev_links)) != NULL);
		printf("udev devpath: %s\n", udev_device_get_devpath(udev_dev));
		printf("udev subsystem: %s\n", udev_device_get_subsystem(udev_dev));
		printf("udev devtype: %s\n", udev_device_get_devtype(udev_dev));
		printf("udev syspath: %s\n", udev_device_get_syspath(udev_dev));
		printf("udev sysname: %s\n", udev_device_get_sysname(udev_dev));
		printf("udev sysnum: %s\n", udev_device_get_sysnum(udev_dev));
		printf("udev devnode: %s\n", udev_device_get_syspath(udev_dev));
        enum libinput_event_type ev_type = libinput_event_get_type(ev);
        if (ev_type == LIBINPUT_EVENT_DEVICE_ADDED) {
            if (is_waxcape_device(dev_syspath) ||
                !libinput_device_has_capability(dev,
                                                LIBINPUT_DEVICE_CAP_KEYBOARD)) {

                printf("Ignoring device %s\n", libinput_device_get_name(dev));
            } else {

                /*

                                if (grab && *grab && ioctl(fd, EVIOCGRAB,
                   (void*)1) == -1) { fprintf(stderr, "Grab requested, but
                   failed for %s(%s)\n", path, strerror(errno));
                                }

                                struct libinput_device* dev =
                   libinput_event_get_device(ev); printf("Registered new device:
                               %s.\n", libinput_device_get_name(dev));
                                           */
            }
        }

        if (ev_type == LIBINPUT_EVENT_KEYBOARD_KEY) {
            waxcape_handle_key_event(ev);
        }

        libinput_event_destroy(ev);
        libinput_dispatch(li);
    }
}

static void waxcape_poll_and_handle_events_loop(struct libinput* li) {
    struct pollfd fds;

    fds.fd = libinput_get_fd(li);
    fds.events = POLLIN;
    fds.revents = 0;

    while (count++ < 10 && !stop && poll(&fds, 1, -1) > -1) {

        waxcape_handle_events(li);
    }
}

static void waxcape_sighandler(int signal, siginfo_t* siginfo, void* userdata) {
    stop = 1;
}

static int waxcape_register_signal_handler() {
    struct sigaction act;

    memset(&act, 0, sizeof(act));
    act.sa_sigaction = waxcape_sighandler;
    act.sa_flags = SA_SIGINFO;
    return sigaction(SIGINT, &act, NULL);
}

static bool waxcape_create_keyboard() {
    int err;
    struct libevdev* dev = libevdev_new();
    libevdev_set_name(dev, "Waxcape Keyboard");

    libevdev_enable_event_type(dev, EV_KEY);
    printf("Creating the new keyboard\n");
    libevdev_enable_event_code(dev, EV_KEY, KEY_A, NULL);

    err = libevdev_uinput_create_from_device(dev, LIBEVDEV_UINPUT_OPEN_MANAGED,
                                             &waxcape_keyboard);

    if (err != 0) {
        printf("Failed: %s\n", strerror(errno));
        return false;
    }

    return true;
}

static int open_restricted(const char* path, int flags, void* user_data) {
    int fd = open(path, flags);

    if (fd < 0)
        fprintf(stderr, "Failed to open %s (%s)\n", path, strerror(errno));

    return fd < 0 ? -errno : fd;
}

static void close_restricted(int fd, void* user_data) { close(fd); }

const static struct libinput_interface interface = {
    .open_restricted = open_restricted,
    .close_restricted = close_restricted,
};

static struct libinput* waxcape_create_libinput_context(struct udev* udev) {
    struct libinput* li;
    const char* seat = "seat0";
    li = libinput_udev_create_context(&interface, NULL, udev);
    if (!li) {
        fprintf(stderr, "Failed to create libinput context: %s\n",
                strerror(errno));
        return NULL;
    }

    libinput_udev_assign_seat(li, seat);
    return li;
}

int main(int argc, char** argv) {
    if (waxcape_register_signal_handler() == -1) {
        fprintf(stderr, "Failed to set up signal handling (%s)\n",
                strerror(errno));
        return -errno;
    }

    if (!waxcape_create_keyboard()) {
        return -errno;
    }
    struct udev* udev = udev_new();
    struct libinput* li = waxcape_create_libinput_context(udev);

    waxcape_poll_and_handle_events_loop(li);

    libevdev_uinput_destroy(waxcape_keyboard);
    libinput_unref(li);
    udev_unref(udev);
    printf("Done.\n");

    return 0;
}
