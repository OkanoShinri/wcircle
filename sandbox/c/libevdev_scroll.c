#include <stdio.h>
#include <unistd.h>
#include <fcntl.h>
#include <error.h>
#include <string.h>
#include <libevdev-1.0/libevdev/libevdev.h>
#include <libevdev-1.0/libevdev/libevdev-uinput.h>

int main(void)
{
    const char *devnode = "/dev/input/event16";  /* タッチパッド本体 */
    int fd = open(devnode, O_RDONLY);
    if (fd < 0) {
        perror("open");
        return 1;
    }

    struct libevdev *dev = NULL;
    int rc = libevdev_new_from_fd(fd, &dev);
    if (rc < 0) {
        fprintf(stderr, "Failed to init libevdev (%s)\n", strerror(-rc));
        return 1;
    }

    printf("Original device: %s\n", libevdev_get_name(dev));

    /* --- REL_WHEEL を追加する --- */
    libevdev_enable_event_type(dev, EV_REL);
    libevdev_enable_event_code(dev, EV_REL, REL_WHEEL, NULL);

    /* --- uinput デバイス作成 --- */
    struct libevdev_uinput *uidev;
    rc = libevdev_uinput_create_from_device(dev, LIBEVDEV_UINPUT_OPEN_MANAGED, &uidev);
    if (rc < 0) {
        fprintf(stderr, "Failed to create uinput device (%s)\n", strerror(-rc));
        return 1;
    }

    printf("Created virtual device: %s\n",
           libevdev_uinput_get_devnode(uidev));

    /* --- ホイールスクロールを10回送る --- */
    for (int i = 0; i < 1000; i++) {
        printf("Sending wheel event %d/10\n", i + 1);

        libevdev_uinput_write_event(uidev, EV_REL, REL_WHEEL, 1);
        libevdev_uinput_write_event(uidev, EV_SYN, SYN_REPORT, 0);

        sleep(1);
    }

    libevdev_uinput_destroy(uidev);
    libevdev_free(dev);
    close(fd);

    printf("Done.\n");
    return 0;
}
