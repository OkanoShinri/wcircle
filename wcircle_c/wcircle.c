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
    int    wheel_step;        // REL_WHEEL の1発あたり値（一般的には ±1、HiRes は別拡張で）
    int    grab_on_scroll;    // スクロール中は元デバイスをグラブ（1=true）
    int    wheel_hi_res;      // 高解像度を用いるか否か(1=REL_WHEEL_HI_RES, 0=REL_WHEEL)
    bool   invert_scroll;     // false=時計回りで下、true=時計回りで上
} config_t;

typedef struct {
    int infd;
    struct libevdev *dev;
    int uifd;
    int grabbed;

    // デバイス座標 -> 正規化用
    int x_min, x_max, y_min, y_max;
    double cx, cy; // 中心

    // 状態
    double last_angle;     // unwrap 済みの直前角
    double accum_angle;    // 累積角
    bool staying_in_area;      // 開始判定エリアに留まっているか(開始判定用)
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

static void emit_rel(int fd, int code, int value){
    emit_uinput_event(fd, EV_REL, code, value);
}

static void maybe_grab(app_t *a, int on){
    if (!a->cfg.grab_on_scroll) return;
    if (on){
        ioctl(libevdev_get_fd(a->dev), EVIOCGRAB, 1);
        a->grabbed = 1;
        
    } else {
        ioctl(libevdev_get_fd(a->dev), EVIOCGRAB, 0);
        a->grabbed = 0;
    }
}

static bool is_in_touch_area(int x, int y, app_t *a){
    double nx = (double)(x - a->x_min) / (double)(a->x_max - a->x_min) * 2.0 - 1.0;
    double ny = (double)(y - a->y_min) / (double)(a->y_max - a->y_min) * 2.0 - 1.0;
    double r = sqrt(nx*nx + ny*ny);
    return (r >= a->cfg.outer_ratio_min && r <= a->cfg.outer_ratio_max);
}

static double to_ang(int x, int y, app_t *a){
    double nx = ((double)x - a->x_min) / (double_t)(a->x_max - a->x_min) * 2.0 - 1.0;
    double ny = ((double)y - a->y_min) / (double)(a->y_max - a->y_min) *2.0 - 1.0;
    double ang = atan2(ny, nx);
    return ang;
}

static void update_xy_before_scroll(int x, int y, app_t *a){
    double ang = to_ang(x, y, a);
    double d = angle_diff(ang, a->last_angle);
    a->last_angle = a->last_angle + d; // unwrap
    a->accum_angle += d;

    if (!is_in_touch_area(x, y, a)){
        if (a->staying_in_area){
        LOG("Out of area. Scrolling will not begin.");
        }
        a->staying_in_area=false;
    }

    if (a->staying_in_area && fabs(a->accum_angle)>=a->cfg.start_arc_rad){
        a->scrolling=true;
        maybe_grab(a, 1);
        LOG("Scroll will start.");
    }
}

static void update_xy_while_scroll(int x, int y, app_t *a){
    double ang = to_ang(x, y, a);
    double d = angle_diff(ang, a->last_angle);
    a->last_angle = a->last_angle + d; // unwrap
    a->accum_angle += d;

    // 発火
    while (fabs(a->accum_angle) >= a->cfg.step_rad){
        int dir = (a->accum_angle > 0) ? -1 : 1;
        dir *= (a->cfg.invert_scroll) ? -1 : 1;
        int mode = (a->cfg.wheel_hi_res) ? REL_WHEEL_HI_RES : REL_WHEEL;
        emit_rel(a->uifd, mode, dir * a->cfg.wheel_step);
        emit_syn(a->uifd);
        LOG("dir * a->cfg.wheel_step: %d\n",dir * a->cfg.wheel_step);
        a->accum_angle += dir * a->cfg.step_rad;
    }
}
    

static void run(const char *device_path){
    app_t a = {0};
    a.cfg = (config_t){
        .outer_ratio_min = 0.70,
        .outer_ratio_max = 1.415,
        .start_arc_rad   = 18.0*DEG2RAD,
        .step_rad        = 18.0*DEG2RAD,
        .wheel_step      = 1,
        .grab_on_scroll  = 1,
        .wheel_hi_res    = 0,
        .invert_scroll   = false,
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

    double cx = (a.x_min + a.x_max) * 0.5;
    double cy = (a.y_min + a.y_max) * 0.5;

    a.uifd = setup_uinput();
    LOG("ready. device=%s center=(%.1f,%.1f)", device_path, cx, cy);

    int curr_x = (int)cx, curr_y = (int)cy;

    bool is_x_updated=false;
    bool is_y_updated=false;

    int event_type=0;
    int event_code=0;
    int event_value=0;

    typedef enum {
        NONE,                //0
        FIRST,               //1
        STARTED_IN_AREA,     //2
        STARTED_NOT_IN_AREA, //3
        SCROLLING,           //4
        END                  //5
    } event_state;
    event_state state=NONE; 
    
    // Event check loop
    while (1){
        // LOG("MODE=%d, grab=%d",state,a.grabbed);
        struct input_event ev;
        int rc = libevdev_next_event(a.dev, LIBEVDEV_READ_FLAG_NORMAL, &ev);
        if (rc == 0) {
            // LOG("[Event check loop] event recieved: ev.type=%hu ev.code=%d ev.value=%d", ev.type, ev.code, ev.value);
            event_type=ev.type;
            event_code=ev.code;
            event_value=ev.value;

            if (event_type == EV_KEY && event_code == BTN_TOUCH && event_value == 1) state=FIRST;
            if (event_type == EV_KEY && event_code == BTN_TOUCH && event_value == 0) state=END;

            if (event_type == EV_ABS && event_code == ABS_X) curr_x=event_value;
            if (event_type == EV_ABS && event_code == ABS_Y) curr_y=event_value;

            
            if (event_type == EV_SYN && event_code == SYN_REPORT && event_value == 0) {
                //イベント実行
                switch (state) {
                case FIRST:
                    if (is_in_touch_area(curr_x, curr_y, &a)){
                        state=STARTED_IN_AREA;
                        a.staying_in_area = true;
                        a.scrolling = false;
                        a.accum_angle = 0.0;
                        a.last_angle = to_ang(curr_x, curr_y, &a);
                        LOG("First touch detected, begin touch");
                    } else {
                        state=STARTED_NOT_IN_AREA;
                        LOG("First touch detected, but this is not in area. grab=%d",a.grabbed);
                    }
                    break;
                case STARTED_IN_AREA:
                    // 外周部で閾値以上回転したらスクロールスタート
                    update_xy_before_scroll(curr_x, curr_y, &a);
                    if (a.scrolling) {
                        state=SCROLLING;
                    }
                    break;
                case SCROLLING:
                    update_xy_while_scroll(curr_x, curr_y, &a);
                    break;
                case END:
                    state=FIRST;
                    maybe_grab(&a, 0);
                    LOG("end touch, grab=%d",a.grabbed);
                    break;
                default:
                    break;
                }
            }

            
            // } else if (ev.type == EV_SYN && ev.code == SYN_DROPPED){
            //     // 取りこぼし時は再同期
            //     libevdev_next_event(a.dev, LIBEVDEV_READ_FLAG_SYNC, &ev);
            // }
        } else if (rc == -EAGAIN){
            // 少し待つ
            usleep(1000);
        } else {
            // デバイス切断など
            LOG("libevdev rc=%d -> exit", rc);
            break;
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
