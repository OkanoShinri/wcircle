use serde::Deserialize;
use std::{fs, path::PathBuf};

#[derive(Debug, Deserialize)]
pub struct Config {
    pub device: String,
    pub r_min: f64,
    pub r_max: f64,
    pub sensitivity: f64,
    pub invert: bool,
    pub axis: String,
    pub x_min: f64,
    pub x_max: f64,
    pub y_min: f64,
    pub y_max: f64,

}

impl Default for Config {
    fn default() -> Self {
        Self {
            device: "/dev/input/event16".to_string(),
            r_min: 0.8,
            r_max: 1.0,
            sensitivity: 1.0,
            invert: false,
            axis: "vertical".to_string(),
            x_min: 1232.0,
            x_max: 5712.0,
            y_min: 1074.0,
            y_max: 4780.0,

        }
    }
}

impl Config {
    pub fn load() -> Self {
        let mut path = dirs::config_dir().unwrap_or_else(|| PathBuf::from("."));
        path.push("wcircle/config.toml");

        if let Ok(contents) = fs::read_to_string(&path) {
            match toml::from_str::<Config>(&contents) {
                Ok(cfg) => {
                    println!("Loaded config from {:?}", path);
                    cfg
                }
                Err(e) => {
                    eprintln!("⚠️ Error parsing {:?}: {}", path, e);
                    Self::default()
                }
            }
        } else {
            println!("No config file found, using defaults.");
            Self::default()
        }
    }
}
