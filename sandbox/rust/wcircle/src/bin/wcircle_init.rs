use std::fs;
use std::io;
use std::path::PathBuf;
use dirs;

use std::process::Command;

fn detect_device_path() -> Option<String> {
    let output = Command::new("evtest")
        .output()
        .ok()?;
    let text = String::from_utf8_lossy(&output.stdout);
    println!("output of evtest: {}", text);

    let mut last_event_line = None;
    for line in text.lines() {
        if line.contains("Event: /dev/input/") {
            last_event_line = Some(line.to_string());
        }

        // 典型的なタッチパッド名のキーワード
        let keywords = ["TouchPad", "Synaptics", "Elantech", "ETPS", "ALPS"];
        if keywords.iter().any(|k| line.contains(k)) {
            if let Some(ev_line) = &last_event_line {
                if let Some(idx) = ev_line.find("/dev/input/") {
                    let dev = &ev_line[idx..];
                    return Some(dev.trim().to_string());
                }
            }
        }
    }

    None
}

fn main() -> io::Result<()> {
    let mut path = dirs::config_dir().unwrap_or_else(|| PathBuf::from("."));
    path.push("wcircle");
    fs::create_dir_all(&path)?;
    path.push("config.toml");

    if path.exists() {
        println!("⚠️ Config already exists at {:?}", path);

        // 既存ファイルがあり、--auto 指定なら上書き可能
        let args: Vec<String> = std::env::args().collect();
        if args.contains(&"--auto".to_string()) {
            println!("Attempting to auto-detect device...");
        } else {
            return Ok(());
        }
    }

    let detected = detect_device_path();
    let device_path = detected
        .as_ref()
        .map(|s| s.as_str())
        .unwrap_or("/dev/input/event17");
    println!("device path is {}", device_path);
    let config_text = format!(
        r#"
device = "{device_path}"
r_min = 0.8
r_max = 1.0
angle_step_deg = 6.0
invert = false
axis = "vertical"
x_min = 1232.0
x_max = 5712.0
y_min = 1074.0
y_max = 4780.0
"#,
        device_path = device_path
    );

    fs::write(&path, config_text.trim_start())?;
    if detected.is_some() {
        println!("✅ Created {:?} (auto-detected device: {})", path, device_path);
    } else {
        println!("✅ Created {:?} (device detection failed, used default)", path);
    }
 
     Ok(())
 }
