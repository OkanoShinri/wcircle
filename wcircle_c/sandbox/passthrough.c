// evpassthrough.c
//
// 使い方:
//   gcc evpassthrough.c -o evpassthrough `pkg-config --cflags --libs libevdev libevdev-uinput`
//   sudo ./evpassthrough /dev/input/eventX
//
// 元デバイスを grab して、そこから読み取ったイベントを
// ほぼそのまま uinput デバイスに流すだけのシンプル実装です。

#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>
#include <string.h>
#include <signal.h>

#include <linux/input-event-codes.h>
#include <linux/uinput.h>
#include <libevdev-1.0/libevdev/libevdev.h>
#include <libevdev-1.0/libevdev/libevdev-uinput.h>
// #include <libevdev-1.0/libevdev/libevdev-uinput.h>

static volatile sig_atomic_t g_stop = 0;

static void handle_sigint(int signo)
{
    (void)signo;
    g_stop = 1;
}

static void die(const char *msg, int err)
{
    if (err < 0) {
        fprintf(stderr, "%s: %s\n", msg, strerror(-err));
    } else {
        fprintf(stderr, "%s\n", msg);
    }
    exit(EXIT_FAILURE);
}

int main(int argc, char *argv[])
{
    if (argc != 2) {
        fprintf(stderr, "Usage: %s /dev/input/eventX\n", argv[0]);
        return EXIT_FAILURE;
    }

    const char *path = argv[1];
    int fd = -1;
    struct libevdev *dev = NULL;
    struct libevdev_uinput *uidev = NULL;
    int rc;

    // SIGINT (Ctrl-C) で安全に終了できるようにしておく
    struct sigaction sa;
    memset(&sa, 0, sizeof(sa));
    sa.sa_handler = handle_sigint;
    sigaction(SIGINT, &sa, NULL);

    // 元デバイスを開く
    fd = open(path, O_RDONLY | O_NONBLOCK);
    if (fd < 0) {
        perror("open");
        return EXIT_FAILURE;
    }

    rc = libevdev_new_from_fd(fd, &dev);
    if (rc < 0) {
        die("Failed to init libevdev", rc);
    }

    fprintf(stderr, "Input device name: \"%s\"\n", libevdev_get_name(dev));
    fprintf(stderr, "Input device ID: bus %#x vendor %#x product %#x\n",
            libevdev_get_id_bustype(dev),
            libevdev_get_id_vendor(dev),
            libevdev_get_id_product(dev));

    // 元デバイスを grab して、他プロセスへの生イベントを止める
    rc = libevdev_grab(dev, LIBEVDEV_GRAB);
    if (rc < 0) {
        die("Failed to grab device", rc);
    }
    fprintf(stderr, "Device grabbed\n");

    // 元デバイスの capabilities をそのままコピーして uinput デバイスを作成
    rc = libevdev_uinput_create_from_device(dev,
                                            LIBEVDEV_UINPUT_OPEN_MANAGED,
                                            &uidev);
    if (rc < 0) {
        die("Failed to create uinput device", rc);
    }

    const char *uinput_node = libevdev_uinput_get_devnode(uidev);
    fprintf(stderr, "Created virtual device at %s\n",
            uinput_node ? uinput_node : "(unknown)");

    // イベントループ
    fprintf(stderr, "Starting event passthrough (Ctrl-C to exit)\n");

    while (!g_stop) {
        struct input_event ev;
        int status = libevdev_next_event(dev,
                                         LIBEVDEV_READ_FLAG_NORMAL |
                                         LIBEVDEV_READ_FLAG_BLOCKING,
                                         &ev);
        if (ev.type!=0 || ev.code!=0 || ev.value!=0){
            printf("ev.type=%hu ev.code=%d ev.value=%d\n", ev.type, ev.code, ev.value);
        }
        if (status == LIBEVDEV_READ_STATUS_SUCCESS ||
            status == LIBEVDEV_READ_STATUS_SYNC) {
            // 取得したイベントをそのまま uinput デバイスに送る
            rc = libevdev_uinput_write_event(uidev, ev.type, ev.code, ev.value);
            if (rc < 0) {
                fprintf(stderr, "write_event failed: %s\n", strerror(-rc));
                break;
            }

            // EV_SYN の場合は同期として SYN_REPORT を送る必要があるが、
            // ev 自体が SYN ならそれをそのまま送っているので OK。
            // （libevdev_uinput_write_event は SYN もそのまま送る）
        } else if (status == -EAGAIN) {
            // 非ブロッキングで読む場合の一時的なエラーだが、
            // 今回は BLOCKING を指定しているので通常ここには来ない
            continue;
        } else {
            // その他のエラー時
            if (status < 0) {
                fprintf(stderr, "libevdev_next_event error: %s\n",
                        strerror(-status));
            } else {
                fprintf(stderr, "Unexpected status: %d\n", status);
            }
            break;
        }
    }

    fprintf(stderr, "Stopping...\n");

    // 後片付け
    if (uidev) {
        libevdev_uinput_destroy(uidev);
        uidev = NULL;
    }

    if (dev) {
        // grab 解除（destroy 時に勝手に close もされる）
        libevdev_grab(dev, LIBEVDEV_UNGRAB);
        libevdev_free(dev);
        dev = NULL;
    }

    if (fd >= 0) {
        close(fd);
    }

    return EXIT_SUCCESS;
}

