// gcc -o clone_tp clone_tp.c
// sudo ./clone_tp /dev/input/eventX

#include <stdio.h>
#include <string.h>
#include <fcntl.h>
#include <unistd.h>
#include <linux/input.h>
#include <linux/uinput.h>
#include <sys/ioctl.h>

#include <libevdev-1.0/libevdev/libevdev.h>
#include <linux/input-event-codes.h>
#define BITS_PER_LONG (sizeof(long) * 8)
#define NBITS(x) ((((x)-1)/BITS_PER_LONG)+1)
#define OFF(x)  ((x)%BITS_PER_LONG)
#define BIT(x)  (1UL<<OFF(x))
#define LONG(x) ((x)/BITS_PER_LONG)
#define TEST_BIT(arr, bit)  (arr[LONG(bit)] & BIT(bit))

static void enable_capabilities_from_eventX(int src_fd, int ufd) {
    unsigned long evbit[NBITS(EV_MAX)] = {0};

    // Get supported event types
    ioctl(src_fd, EVIOCGBIT(0, EV_MAX), evbit);

    // For each event type...
    for (int ev = 0; ev < EV_MAX; ev++) {
        if (!TEST_BIT(evbit, ev))
            continue;

        // Enable event type on uinput
        ioctl(ufd, UI_SET_EVBIT, ev);

        unsigned long codebit[NBITS(KEY_MAX)] = {0};

        // Get all event codes inside this event type
        ioctl(src_fd, EVIOCGBIT(ev, KEY_MAX), codebit);

        for (int code = 0; code < KEY_MAX; code++) {
            if (!TEST_BIT(codebit, code))
                continue;

            switch (ev) {
                case EV_KEY:
                    ioctl(ufd, UI_SET_KEYBIT, code);
                    break;
                case EV_REL:
                    ioctl(ufd, UI_SET_RELBIT, code);
                    break;
                case EV_ABS: {
                    struct input_absinfo abs;
                    if (ioctl(src_fd, EVIOCGABS(code), &abs) == 0) {
                        // Copy ABS range and other info
                        struct uinput_abs_setup abs_setup = {
                            .code = code,
                            .absinfo = abs
                        };
                        ioctl(ufd, UI_ABS_SETUP, &abs_setup);
                    }
                } break;
                case EV_MSC:
                    ioctl(ufd, UI_SET_MSCBIT, code);
                    break;
                case EV_SW:
                    ioctl(ufd, UI_SET_SWBIT, code);
                    break;
                case EV_LED:
                    ioctl(ufd, UI_SET_LEDBIT, code);
                    break;
                case EV_SND:
                    ioctl(ufd, UI_SET_SNDBIT, code);
                    break;

                default:
                    // Unknown / unsupported type: ignore
                    break;
            }
        }
    }
}

static void emit_syn(int fd){
    struct input_event ev;

    memset(&ev, 0, sizeof(ev));
    // clock_gettime(CLOCK_MONOTONIC, &ev.time);
    ev.type = EV_SYN; ev.code = SYN_REPORT; ev.value = 0;
    if (write(fd, &ev, sizeof(ev)) < 0) perror("write SYN");
}

static void emit_uinput_event(int fd, int type, int code, int value){
    struct input_event ev;
    memset(&ev, 0, sizeof(ev));
    // clock_gettime(CLOCK_MONOTONIC, &ev.time);
    ev.type = type; ev.code = code; ev.value = value;
    if (write(fd, &ev, sizeof(ev)) < 0) perror("write REL");
    emit_syn(fd);
}

int main(int argc, char **argv) {
    if (argc < 2) {
        fprintf(stderr, "Usage: %s /dev/input/eventX\n", argv[0]);
        return 1;
    }

    const char *src = argv[1];

    int src_fd = open(src, O_RDONLY);
    if (src_fd < 0) {
        perror("open(src)");
        return 1;
    }

    // Read device information
    struct input_id id;
    ioctl(src_fd, EVIOCGID, &id);

    char name[256] = "cloned_touchpad";
    ioctl(src_fd, EVIOCGNAME(sizeof(name)), name);

    // Open /dev/uinput
    int ufd = open("/dev/uinput", O_WRONLY | O_NONBLOCK);
    if (ufd < 0) {
        perror("open(uinput)");
        return 1;
    }

    // Basic device setup
    struct uinput_setup usetup;
    memset(&usetup, 0, sizeof(usetup));
    snprintf(usetup.name, UINPUT_MAX_NAME_SIZE, "%s_clone", name);
    usetup.id = id;

    enable_capabilities_from_eventX(src_fd, ufd);

    // Create the device
    ioctl(ufd, UI_DEV_SETUP, &usetup);
    ioctl(ufd, UI_DEV_CREATE);

    printf("Created uinput device '%s_clone'\n", name);
    printf("Capabilities copied from %s\n", src);
    printf("Device ready. Press Ctrl+C to exit.\n");

    // Keep the process alive
    while (1) {
        struct input_event ev;
        int rc = libevdev_next_event(a.dev, LIBEVDEV_READ_FLAG_NORMAL, &ev);
        if (rc == 0) {
    if (write(fd, &ev, sizeof(ev)) < 0) perror("write REL");

        }
    }

    ioctl(ufd, UI_DEV_DESTROY);
    close(ufd);
    close(src_fd);
    return 0;
}

