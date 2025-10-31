use evdev::{Device, EventType};
use anyhow::Result;

fn main() -> Result<()> {
    // あなたのデバイスパスに変更（例: /dev/input/event12）
    let mut device = Device::open("/dev/input/event16")?;

    println!("Listening on: {}", device.name().unwrap_or("Unknown"));

    loop {
        for ev in device.fetch_events()? {
            if ev.event_type() == EventType::ABSOLUTE {
                match ev.code() {
                    0 => println!("ABS_X: {}", ev.value()),
                    1 => println!("ABS_Y: {}", ev.value()),
                    _ => {}
                }
            }
        }
    }
}
