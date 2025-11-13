// gcc -o ev_passthrough ev_passthrough.c -levdev
// 実行例: sudo ./ev_passthrough /dev/input/eventX
//
// 役割:
//   - 指定された evdev デバイス(eventX)を libevdev でオープン
//   - その capabilities から uinput 仮想デバイス(eventY)を作成
//   - eventX のイベントをすべて eventY にそのまま流す（パススルー）


#define _GNU_SOURCE
#include <errno.h>
#include <fcntl.h>
#include <linux/input-event-codes.h>
#include <linux/uinput.h>
#include <math.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/select.h>
#include <sys/time.h>
#include <time.h>
#include <unistd.h>

#include <libevdev-1.0/libevdev/libevdev.h>
static int running = 1;

static void handle_sigint(int sig)
{
    (void)sig;
    running = 0;
}

static void forward_event(struct libevdev_uinput *uidev,
                          const struct input_event *ev)
{
    int rc = libevdev_uinput_write_event(uidev, ev->type, ev->code, ev->value);
    if (rc < 0) {
        fprintf(stderr, "libevdev_uinput_write_event failed: %s\n",
                strerror(-rc));
    }
}

int main(int argc, char *argv[])
{
    if (argc < 2) {
        fprintf(stderr, "Usage: %s /dev/input/eventX\n", argv[0]);
        return 1;
    }

    const char *devnode = argv[1];
    int fd = open(devnode, O_RDONLY | O_NONBLOCK);
    if (fd < 0) {
        perror("open");
        return 1;
    }

    struct libevdev *dev = NULL;
    int rc = libevdev_new_from_fd(fd, &dev);
    if (rc < 0) {
        fprintf(stderr, "Failed to init libevdev (%s): %s\n",
                devnode, strerror(-rc));
        close(fd);
        return 1;
    }

    printf("Input device name: \"%s\"\n", libevdev_get_name(dev));
    printf("Input device ID: bus %#x vendor %#x product %#x\n",
           libevdev_get_id_bustype(dev),
           libevdev_get_id_vendor(dev),
           libevdev_get_id_product(dev));

    // 必要に応じてここで grab する：
    //   元デバイスのイベントを他のクライアントに流したくない場合は有効化。
    //
    // rc = libevdev_grab(dev, LIBEVDEV_GRAB);
    // if (rc < 0) {
    //     fprintf(stderr, "Failed to grab device: %s\n", strerror(-rc));
    // }

    // uinput デバイスを、元デバイスの capabilities から自動生成
    struct libevdev_uinput *uidev = NULL;
    rc = libevdev_uinput_create_from_device(dev,
                                            LIBEVDEV_UINPUT_OPEN_MANAGED,
                                            &uidev);
    if (rc < 0) {
        fprintf(stderr, "Failed to create uinput device: %s\n",
                strerror(-rc));
        libevdev_free(dev);
        close(fd);
        return 1;
    }

    printf("Created uinput device: %s\n",
           libevdev_uinput_get_devnode(uidev));
    printf("Passthrough %s -> %s\n", devnode,
           libevdev_uinput_get_devnode(uidev));

    signal(SIGINT, handle_sigint);

    struct input_event ev;
    while (running) {
        rc = libevdev_next_event(dev, LIBEVDEV_READ_FLAG_NORMAL, &ev);

        if (rc == LIBEVDEV_READ_STATUS_SUCCESS) {
            // 通常イベントをそのまま転送
            forward_event(uidev, &ev);
        } else if (rc == LIBEVDEV_READ_STATUS_SYNC) {
            // SYN_DROPPED 発生時の再同期ループ
            fprintf(stderr, "SYNC: dropped events, resyncing...\n");
            do {
                forward_event(uidev, &ev);
                rc = libevdev_next_event(dev,
                                         LIBEVDEV_READ_FLAG_SYNC,
                                         &ev);
            } while (rc == LIBEVDEV_READ_STATUS_SYNC);

            if (rc == LIBEVDEV_READ_STATUS_SUCCESS) {
                // 再同期終了後の通常イベントも転送
                forward_event(uidev, &ev);
            } else if (rc < 0 && rc != -EAGAIN) {
                fprintf(stderr,
                        "Error during sync: %s\n", strerror(-rc));
                break;
            }
        } else if (rc == -EAGAIN) {
            // まだイベントがない。少し待つ。
            usleep(1000);
            continue;
        } else if (rc < 0) {
            fprintf(stderr, "libevdev_next_event error: %s\n",
                    strerror(-rc));
            break;
        }
    }

    printf("Exiting...\n");

    libevdev_uinput_destroy(uidev);
    libevdev_free(dev);
    close(fd);
    return 0;
}
