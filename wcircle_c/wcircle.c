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

#define DIE(...)  do { fprintf(stderr, __VA_ARGS__); fprintf(stderr, "\n"); exit(1);} while(0)
// #define LOG(...)  do { fprintf(stderr, __VA_ARGS__); fprintf(stderr, "\n"); } while(0)

#define LOG(...) do { \
    static unsigned long _log_counter = 0; \
    _log_counter++; \
    time_t t = time(NULL); \
    struct tm *tm_info = localtime(&t); \
    char time_buf[20]; \
    strftime(time_buf, sizeof(time_buf), "%Y-%m-%d %H:%M:%S", tm_info); \
    fprintf(stderr, "[%05lu] [%s] [DEBUG] ", _log_counter, time_buf); \
    fprintf(stderr, __VA_ARGS__); \
    fprintf(stderr, "\n"); \
} while(0)

#define DEG2RAD M_PI/180
#define RAD2DEG 180/M_PI

typedef struct {
    // 検出パラメータ（後で設定ファイル化）
    double outer_ratio_min;   // 外周リングの内側境界（中心からの比）例: 0.70
    double outer_ratio_max;   // 外周リングの外側境界（比）例: 1.05 (若干はみ出し許容)
    double start_arc_rad;     // スクロール開始判定: 累積角度 [rad]
    double step_rad;          // 1ホイール発火あたりの角度 [rad]（小さくすると高分解能）
    double hysteresis_rad;    // 停止判定のヒステリシス
    int    wheel_step;        // REL_WHEEL の1発あたり値（一般的には ±1、HiRes は別拡張で）
    int    grab_on_scroll;    // スクロール中は元デバイスをグラブ（1=true）
} config_t;

typedef struct {
    int infd;
    struct libevdev *dev;
    int uifd;
    int grabbed;

    // デバイス座標 -> 正規化用
    int x_min, x_max, y_min, y_max;
    double cx, cy; // 中心
    double r_max;  // 正規化半径（x/yスケール差をならすため、長辺半径を採用）

    // 状態
    bool touching;
    double last_angle;     // unwrap 済みの直前角
    double accum_angle;    // 累積角
    bool is_first_touch;      // 開始判定用
    bool scrolling;        // スクロールモード中か
    config_t cfg;
} app_t;

// 角度差分を [-pi, pi] に正規化
static inline double angle_diff(double a, double b){ 
    double d = a - b;
    while (d >  M_PI) d -= 2*M_PI;
    while (d < -M_PI) d += 2*M_PI;
    return d;
}

static int setup_uinput(){
    int fd = open("/dev/uinput", O_WRONLY | O_NONBLOCK);
    if (fd < 0) DIE("open /dev/uinput: %s", strerror(errno));

    // REL_WHEEL を持つ仮想デバイス
    if (ioctl(fd, UI_SET_EVBIT, EV_REL) < 0) DIE("UI_SET_EVBIT EV_REL");
    if (ioctl(fd, UI_SET_RELBIT, REL_WHEEL) < 0) DIE("UI_SET_RELBIT REL_WHEEL");
    if (ioctl(fd, UI_SET_RELBIT, REL_WHEEL_HI_RES) < 0) DIE("UI_SET_RELBIT REL_WHEEL_HI_RES");
    if (ioctl(fd, UI_SET_RELBIT, REL_HWHEEL) < 0) DIE("UI_SET_RELBIT REL_HWHEEL"); // 将来の水平対応用

    // 必須
    if (ioctl(fd, UI_SET_EVBIT, EV_SYN) < 0) DIE("UI_SET_EVBIT EV_SYN");

    struct uinput_setup usetup = {0};
    snprintf(usetup.name, sizeof(usetup.name), "wcircle-virtual-wheel");
    usetup.id.bustype = BUS_VIRTUAL;
    usetup.id.vendor  = 0x1234;
    usetup.id.product = 0x5678;
    usetup.id.version = 1;

    if (ioctl(fd, UI_DEV_SETUP, &usetup) < 0) DIE("UI_DEV_SETUP");
    if (ioctl(fd, UI_DEV_CREATE) < 0) DIE("UI_DEV_CREATE");
    // デバイスが出来るまで少し待機
    usleep(10000);
    return fd;
}

static void emit_rel(int fd, int code, int value){
    
    LOG("emit start");
    struct input_event ev;
    memset(&ev, 0, sizeof(ev));
    // clock_gettime(CLOCK_MONOTONIC, &ev.time);
    ev.type = EV_REL; ev.code = code; ev.value = value;
    if (write(fd, &ev, sizeof(ev)) < 0) perror("write REL");

    memset(&ev, 0, sizeof(ev));
    // clock_gettime(CLOCK_MONOTONIC, &ev.time);
    ev.type = EV_SYN; ev.code = SYN_REPORT; ev.value = 0;
    if (write(fd, &ev, sizeof(ev)) < 0) perror("write SYN");
    LOG("emit end");
}

static void maybe_grab(app_t *a, int on){
    if (!a->cfg.grab_on_scroll) return;
    if (on && !a->grabbed){
        if (ioctl(libevdev_get_fd(a->dev), EVIOCGRAB, 1) == 0){
            a->grabbed = 1;
        }
    } else if (!on && a->grabbed){
        ioctl(libevdev_get_fd(a->dev), EVIOCGRAB, 0);
        a->grabbed = 0;
    }
}

static void begin_touch(app_t *a){
    LOG("begin touch");
    a->touching = true;
    a->scrolling = false;
    a->accum_angle = 0.0;
    a->is_first_touch = true;
    a->last_angle  = 0.0; // 初期化は最初の座標更新時に
}

static void end_touch(app_t *a){
    a->touching = false;
    maybe_grab(a, 0);
    a->scrolling = false;
    LOG("end touch. touching=%d, is_first_touch=%d, last_angle=%.5f", a->touching, a->is_first_touch, a->last_angle);
}

static bool is_in_touch_area(double nx, double ny, app_t *a){
    double r = sqrt(nx*nx + ny*ny);
    double outer_min = a->cfg.outer_ratio_min;
    double outer_max = a->cfg.outer_ratio_max;
    return (r >= outer_min && r <= outer_max);
}

static void update_xy(app_t *a, int x, int y){
    // デバイス座標 -> 中心基準の正規化（楕円 -> 単位スクエア）
    double nx = (x - a->cx) / (double)(a->r_max);
    double ny = (y - a->cy) / (double)(a->r_max);
    double ang = atan2(ny, nx);

    // LOG("This is update_xy(). a->touching=%d, ang=%.1f", a->touching, ang);

    if (a->is_first_touch){
        // 1点目
        if (!is_in_touch_area(nx, ny, a)){
            return;
        }
        a->is_first_touch = false;
        a->last_angle = ang;
        LOG("This is first touch. x=%.5f, y=%.5f, ang=last angle=%.5f",nx,ny,ang);
        return;
    }

    double d = angle_diff(ang, a->last_angle);
    LOG("x=%.5f, y=%.5f, last angle=%.5f, current angle=%.5f, angle diff=%.5f.",nx, ny, a->last_angle, ang, d);
    a->last_angle = a->last_angle + d; // unwrap

    // 円周部のみをスクロール対象に

    // LOG("ang=%.1f", ang);

    // 角度変化量は半径にも少し依存させたいが、まずはリング範囲内のみ反応
    if (is_in_touch_area(nx, ny, a)){
        a->accum_angle += d;

        if (!a->scrolling && fabs(a->accum_angle) >= a->cfg.start_arc_rad){
            a->scrolling = true;
            maybe_grab(a, 1); // ここから元デバイスを握ってポインタ動作を止める
            LOG("scroll start");
        }

        if (a->scrolling){
            // 発火
            while (fabs(a->accum_angle) >= a->cfg.step_rad){
                int dir = (a->accum_angle > 0) ? 1 : -1;
                emit_rel(a->uifd, REL_WHEEL_HI_RES, dir * a->cfg.wheel_step);
                // LOG("dir * a->cfg.wheel_step: %d\n",dir * a->cfg.wheel_step);
                a->accum_angle -= dir * a->cfg.step_rad;
            }
        }
    } else {
        // 外周から外れたら停止方向に向かう（ヒステリシス）
        if (a->scrolling){
            if (fabs(a->accum_angle) < a->cfg.hysteresis_rad){
                a->scrolling = false;
                maybe_grab(a, 0);
                LOG("scroll stop (ring exit)");
            }
        }
    }
}

static void run(const char *device_path){
    app_t a = {0};
    a.cfg = (config_t){
        .outer_ratio_min = 0.50,
        .outer_ratio_max = 1.415,
        .start_arc_rad   = 5.0*DEG2RAD,
        .step_rad        = 5.0*DEG2RAD,
        .hysteresis_rad  = 2.0*DEG2RAD,
        .wheel_step      = 1,
        .grab_on_scroll  = 1,
    };

    a.infd = open(device_path, O_RDONLY | O_NONBLOCK);
    if (a.infd < 0) DIE("open input: %s", strerror(errno));
    if (libevdev_new_from_fd(a.infd, &a.dev) < 0) DIE("libevdev_new_from_fd");

    if (!libevdev_has_event_code(a.dev, EV_ABS, ABS_X) ||
        !libevdev_has_event_code(a.dev, EV_ABS, ABS_Y)) {
        DIE("This device has no ABS_X/ABS_Y (need a touchpad-like device)");
    }

    const struct input_absinfo *xi = libevdev_get_abs_info(a.dev, ABS_X);
    const struct input_absinfo *yi = libevdev_get_abs_info(a.dev, ABS_Y);
    a.x_min = xi->minimum; a.x_max = xi->maximum;
    a.y_min = yi->minimum; a.y_max = yi->maximum;

    a.cx = (a.x_min + a.x_max) * 0.5;
    a.cy = (a.y_min + a.y_max) * 0.5;
    // 長辺を基準に半径を決定（円形/楕円の両対応）
    double rx = (a.x_max - a.x_min) * 0.5;
    double ry = (a.y_max - a.y_min) * 0.5;
    a.r_max = fmax(rx, ry);

    a.uifd = setup_uinput();
    LOG("ready. device=%s center=(%.1f,%.1f) rmax=%.1f", device_path, a.cx, a.cy, a.r_max);

    int curr_x = (int)a.cx, curr_y = (int)a.cy;

    bool is_x_updated=false;
    bool is_y_updated=false;
    // Event check loop
    while (1){
        struct input_event ev;
        int rc = libevdev_next_event(a.dev, LIBEVDEV_READ_FLAG_NORMAL, &ev);
        if (rc == 0) {
            LOG("[Event check loop] event recieved: ev.type=%hu ev.code=%d ev.value=%d", ev.type, ev.code, ev.value);
            if (ev.type == EV_KEY/*3*/ && ev.code == BTN_TOUCH/*330(0x14a)*/){
                LOG("[Event check loop] begin/end touch event");
                if (ev.value == 1) {
                    begin_touch(&a);
                    is_x_updated = false;
                    is_y_updated = false;
                }
                else{
                    end_touch(&a);
                }
            } else if (a.touching && ev.type == EV_ABS/*3*/ ){
                // LOG("[Event check loop] update xy");
                if (ev.code == ABS_X/*0*/ ) {
                    curr_x = ev.value;
                    is_x_updated = true;
                    // LOG("[Event check loop] curr_x=%d",curr_x);
                }
                else if (ev.code == ABS_Y/*1*/ ) {
                    curr_y = ev.value;
                    is_y_updated = true;
                    // LOG("[Event check loop] curr_y=%d",curr_y);
                }
            } else if (ev.type == EV_SYN && ev.code == SYN_DROPPED){
                // 取りこぼし時は再同期
                libevdev_next_event(a.dev, LIBEVDEV_READ_FLAG_SYNC, &ev);
            }
        } else if (rc == -EAGAIN){
            // 少し待つ
            usleep(1000);
        } else {
            // デバイス切断など
            LOG("libevdev rc=%d -> exit", rc);
            break;
        }
        if (is_x_updated&&is_y_updated){
            // LOG("[Event check loop] update xy");
            is_x_updated=false;
            is_y_updated=false;
            update_xy(&a, curr_x, curr_y);
        }
    }

    maybe_grab(&a, 0);
    ioctl(a.uifd, UI_DEV_DESTROY);
    close(a.uifd);
    libevdev_free(a.dev);
    close(a.infd);
}

static void usage(const char *prog){
    fprintf(stderr, "Usage: %s /dev/input/eventX\n", prog);
}

int main(int argc, char **argv){
    if (argc < 2){ usage(argv[0]); return 1; }
    run(argv[1]);
    return 0;
}
