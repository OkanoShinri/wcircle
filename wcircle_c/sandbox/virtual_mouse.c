#include <stdio.h>
// #include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <libevdev-1.0/libevdev/libevdev.h>
#include <libevdev-1.0/libevdev/libevdev-uinput.h>

/* =====================================
 *  仮想マウスを作成する関数
 * ===================================== */
struct libevdev_uinput* create_virtual_mouse(void)
{
    struct libevdev *dev = libevdev_new();
    struct libevdev_uinput *uidev;
    int rc;

    if (!dev) {
        fprintf(stderr, "Failed to create libevdev device\n");
        return NULL;
    }

    /* ------- デバイス情報設定 ------- */
    libevdev_set_name(dev, "Virtual Wheel Mouse");
    libevdev_set_id_bustype(dev, BUS_USB);
    libevdev_set_id_vendor(dev, 0x1234);
    libevdev_set_id_product(dev, 0x5678);
    libevdev_set_id_version(dev, 1);

    /* ------- 必須イベント設定 ------- */
    /* ボタン */
    libevdev_enable_event_type(dev, EV_KEY);
    libevdev_enable_event_code(dev, EV_KEY, BTN_LEFT, NULL);
    libevdev_enable_event_code(dev, EV_KEY, BTN_RIGHT, NULL);

    /* 移動とホイール */
    libevdev_enable_event_type(dev, EV_REL);
    libevdev_enable_event_code(dev, EV_REL, REL_X, NULL);
    libevdev_enable_event_code(dev, EV_REL, REL_Y, NULL);
    libevdev_enable_event_code(dev, EV_REL, REL_WHEEL, NULL);

    /* ------- uinput 仮想デバイス作成 ------- */
    rc = libevdev_uinput_create_from_device(dev,
        LIBEVDEV_UINPUT_OPEN_MANAGED,
        &uidev);

    libevdev_free(dev);

    if (rc != 0) {
        fprintf(stderr, "Failed to create uinput device: %s\n", strerror(-rc));
        return NULL;
    }

    return uidev;
}


/* =====================================
 *  メイン
 * ===================================== */
int main(void)
{
    struct libevdev_uinput *uidev;

    /* 仮想マウス作成 */
    uidev = create_virtual_mouse();
    if (!uidev) {
        fprintf(stderr, "Virtual mouse creation failed\n");
        return 1;
    }

    printf("Created virtual mouse at %s\n",
           libevdev_uinput_get_devnode(uidev));

    /* スクロールテスト */
    printf("Sending scroll event...\n");

    for (int i=0; i<100; i++) {
        libevdev_uinput_write_event(uidev, EV_REL, REL_WHEEL, -1);
        libevdev_uinput_write_event(uidev, EV_SYN, SYN_REPORT, 0);
        printf("send scroll\n");
        sleep(1);
        
    }
    printf("Scroll event sent.\n");

    /* クリーンアップ */
    sleep(1);
    libevdev_uinput_destroy(uidev);

    return 0;
}
