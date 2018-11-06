#include <libinput.h>
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
static bool debug = 0;

enum waxcape_state { NOT_PRESSED, PRESSED, CTRL_PRESSED };

static enum waxcape_state caps_state = NOT_PRESSED;

static void print_key_name(int32_t key, int state) {
	char *key_name;
	char key_arr [1024];
	sprintf(key_arr, "%d", key);
	switch (key) {
		case KEY_CAPSLOCK:
			key_name = "Caps Lock";
			break;
		case KEY_LEFTCTRL:
			key_name = "Left Ctrl";
			break;
		case KEY_ESC:
			key_name = "Escape";
			break;
		default:
			key_name = key_arr;
	}


	printf("Key: %s, state: %d\n", key_name, state);
}


static void waxcape_handle_key_event(struct libinput_event* ev) {
    enum libinput_event_type ev_type = libinput_event_get_type(ev);
    if (ev_type == LIBINPUT_EVENT_KEYBOARD_KEY) {
        struct libinput_event_keyboard* k =
            libinput_event_get_keyboard_event(ev);
        enum libinput_key_state state =
            libinput_event_keyboard_get_key_state(k);
        uint32_t key = libinput_event_keyboard_get_key(k);

		if (debug) {
			print_key_name(key, state);
		}

        switch (caps_state) {
        case NOT_PRESSED:
            if (key == KEY_CAPSLOCK) {
                caps_state = PRESSED;
            } else {
                // Just report the key event, since capslock is not pressed.
                libevdev_uinput_write_event(waxcape_keyboard, EV_KEY, key,
                                            state);
                libevdev_uinput_write_event(waxcape_keyboard, EV_SYN,
                                            SYN_REPORT, 0);
            }
            break;
        case PRESSED:
            if (key == KEY_CAPSLOCK) {
                if (!state) {
                    // capslock is released without another key, report escape
                    libevdev_uinput_write_event(waxcape_keyboard, EV_KEY,
                                                KEY_ESC, 1);
                    libevdev_uinput_write_event(waxcape_keyboard, EV_KEY,
                                                KEY_ESC, 0);
                    libevdev_uinput_write_event(waxcape_keyboard, EV_SYN,
                                                SYN_REPORT, 0);
                    caps_state = NOT_PRESSED;
                }
            } else {
                // If another key is pressed when CAPS_LOCK is pressed, we send
                // CAPS as ctrl
                libevdev_uinput_write_event(waxcape_keyboard, EV_KEY,
                                            KEY_LEFTCTRL, 1);
                libevdev_uinput_write_event(waxcape_keyboard, EV_KEY, key,
                                            state);
                libevdev_uinput_write_event(waxcape_keyboard, EV_SYN,
                                            SYN_REPORT, 0);
                caps_state = CTRL_PRESSED;
            }
            break;
        case CTRL_PRESSED:
            if (key == KEY_CAPSLOCK) {
                if (!state) {
                    // Release the ctrl
                    libevdev_uinput_write_event(waxcape_keyboard, EV_KEY,
                                                KEY_LEFTCTRL, 0);
                    libevdev_uinput_write_event(waxcape_keyboard, EV_SYN,
                                                SYN_REPORT, 0);
                    caps_state = NOT_PRESSED;
                }
            } else {
				// Report key event without changing state since CTRL is not released.
				libevdev_uinput_write_event(waxcape_keyboard, EV_KEY, key,
                                            state);
                libevdev_uinput_write_event(waxcape_keyboard, EV_SYN,
                                            SYN_REPORT, 0);

			}
            break;
        }
    }
}

static void waxcape_handle_events(struct libinput* li) {
    struct libinput_event* ev;
    libinput_dispatch(li);
    while ((ev = libinput_get_event(li)) != NULL) {
        waxcape_handle_key_event(ev);
        libinput_event_destroy(ev);
        libinput_dispatch(li);
    }
}

static void waxcape_poll_and_handle_events_loop(struct libinput* li) {
    struct pollfd fds;

    fds.fd = libinput_get_fd(li);
    fds.events = POLLIN;
    fds.revents = 0;

    while (!stop && poll(&fds, 1, -1) > -1) {

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

static int open_restricted(const char* path, int flags, void* user_data) {
    int fd = open(path, flags);

    if (fd < 0)
        fprintf(stderr, "Failed to open %s (%s)\n", path, strerror(errno));
    if (ioctl(fd, EVIOCGRAB, (void*)1) == -1) {
        fprintf(stderr, "Grab requested, but failed for %s (%s)\n", path,
                strerror(errno));
    }

    return fd < 0 ? -errno : fd;
}

static void close_restricted(int fd, void* user_data) { close(fd); }

const static struct libinput_interface interface = {
    .open_restricted = open_restricted,
    .close_restricted = close_restricted,
};

void print_usage() { printf("USAGE: waxcape <device>\n"); }

int main(int argc, char** argv) {
    if (argc != 2) {
        print_usage();
        return 1;
    }

    if (waxcape_register_signal_handler() == -1) {
        fprintf(stderr, "Failed to set up signal handling (%s)\n",
                strerror(errno));
        return 1;
    }

    const char* src_keyboard = argv[1];
    int src_fd = open(src_keyboard, O_RDONLY);
    int rc = 0;
    if (src_fd == -1) {
        fprintf(stderr, "Failed to open device %s: %s\n", src_keyboard,
                strerror(errno));
        return 1;
    }
    struct libevdev* evdev;
    rc = libevdev_new_from_fd(src_fd, &evdev);
    if (rc < 0) {
        fprintf(stderr, "Failed to create evdev device: %d %s\n", -rc,
                strerror(-rc));
        return 1;
    }

    struct libevdev_uinput* foo;
    rc = libevdev_uinput_create_from_device(evdev, LIBEVDEV_UINPUT_OPEN_MANAGED,
                                            &foo);
    if (rc < 0) {
        fprintf(stderr, "Failed to create waxcape keyboard: %d %s\n", -rc,
                strerror(-rc));
        return 1;
    }

    waxcape_keyboard = foo;

    // We don't need the evdev and fd once we create the uinput instance.
    libevdev_free(evdev);
    close(src_fd);

    struct libinput* li;
    li = libinput_path_create_context(&interface, NULL);
    if (!li) {
        fprintf(stderr, "Failed to create libinput context: %s\n",
                strerror(errno));
        return 1;
    }

	// Sleep 1 second before capturing the keyboard so the enter key doesn't stay stuck.
    sleep(1);
    if (!libinput_path_add_device(li, src_keyboard)) {
        fprintf(stderr, "Failed to capture input: %s\n", src_keyboard);
        return 1;
    }

    waxcape_poll_and_handle_events_loop(li);

    // Clean up
    libinput_unref(li);
    libevdev_uinput_destroy(waxcape_keyboard);
    printf("Done.\n");

    return 0;
}
