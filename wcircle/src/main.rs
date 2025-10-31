use evdev::{Device, EventType};
use uinput::event::relative;
use anyhow::Result;
use std::f64::consts::PI;
use std::thread;
use std::time::Duration;

mod config;
use config::Config;


fn main() -> Result<()> {
    let cfg = Config::load();
    println!("Using device: {}", cfg.device);

    let mut device = Device::open(&cfg.device)?;
    let r_min = cfg.r_min;
    let r_max = cfg.r_max;
    let invert = cfg.invert;
    let angle_step = cfg.angle_step_deg;
    let _vertical = cfg.axis == "vertical";
    let x_min = cfg.x_min;
    let x_max = cfg.x_max;
    let y_min = cfg.y_min;
    let y_max = cfg.y_max;

    let mut last_x = None;
    let mut last_y = None;
    let mut last_theta = None;
    let mut theta_acc = 0.0;

    // 仮想スクロールデバイスを作成
    let mut udev = uinput::default()?
        .name("wcircle virtual scroll")?
        .event(relative::Wheel::Vertical)?
        .create()?;

    println!("wcircle: Listening on /dev/input/event16 ...");

    loop {
        for ev in device.fetch_events()? {
            if ev.event_type() == EventType::ABSOLUTE {
                match ev.code() {
                    0 => last_x = Some(ev.value() as f64),
                    1 => last_y = Some(ev.value() as f64),
                    _ => {}
                }
            }

            // X/Y両方が揃ったタイミングで処理
            if let (Some(x), Some(y)) = (last_x, last_y) {
                let nx = (x - x_min) / (x_max - x_min) * 2.0 - 1.0; // -1〜1に正規化
                let ny = (y - y_min) / (y_max - y_min) * 2.0 - 1.0;
                let r = (nx * nx + ny * ny).sqrt();

                // リング帯の外周判定
                if r >= r_max && r <= r_min {
                    let theta = ny.atan2(nx); // -π〜π
                    if let Some(prev_theta) = last_theta {
                        let mut dtheta: f64 = theta - prev_theta;

                        // -π〜πをまたぐ場合の補正
                        if dtheta > PI {
                            dtheta -= 2.0 * PI;
                        } else if dtheta < -PI {
                            dtheta += 2.0 * PI;
                        }

                        theta_acc += dtheta.to_degrees();

                        // しきい超過時にスクロール送出
                        while theta_acc.abs() >= angle_step {
                            let scroll_dir = if invert {-1} else {1};
                            let mut delta = scroll_dir;
                            if theta_acc > 0.0 {
                                // 時計回り → 下方向（正スクロール）
                                udev.send(relative::Wheel::Vertical, delta)?;
                                println!("scroll ↓");
                            } else {
                                delta *= -1;
                                // 反時計回り → 上方向
                                udev.send(relative::Wheel::Vertical, delta)?;
                                println!("scroll ↑");
                            }
                            theta_acc -= angle_step * (scroll_dir as f64);
                        }
                    }
                    last_theta = Some(theta);
                } else {
                    // リング外 → 状態リセット
                    last_theta = None;
                    theta_acc = 0.0;
                }

                last_x = None;
                last_y = None;
            }
        }

        // CPU負荷を軽減
        thread::sleep(Duration::from_millis(2));
    }
}
