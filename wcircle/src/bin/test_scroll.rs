use anyhow::Result;
use std::{thread, time::Duration};
use uinput::event::relative;

fn main() -> Result<()> {
    println!("Creating virtual scroll device...");

    // 仮想デバイスを作成
    let mut dev = uinput::default()?
        .name("wcircle test scroll")?
        // スクロールイベントを有効化（Wheel と HWheel の両方）
        .event(relative::Wheel::Vertical)?
        // .event(relative::HWheel)?
        // 一般的なボタンも登録しておくと認識されやすい
        .event(uinput::event::controller::Mouse::Left)?
        .create()?;

    println!("Device created. Sending scroll events in 3 seconds...");
    thread::sleep(Duration::from_secs(3));

    // 5回スクロール（下方向）
    for i in 0..5 {
        println!("scroll ↓ {}", i);
        dev.send(relative::Wheel::Vertical, 1)?;
        dev.synchronize()?;
        thread::sleep(Duration::from_millis(300));
    }

    // 少し待って上方向へ5回
    thread::sleep(Duration::from_secs(1));
    for i in 0..5 {
        println!("scroll ↑ {}", i);
        dev.send(relative::Wheel::Vertical, -1)?;
        dev.synchronize()?;
        thread::sleep(Duration::from_millis(300));
    }

    println!("Test done!");
    Ok(())
}

