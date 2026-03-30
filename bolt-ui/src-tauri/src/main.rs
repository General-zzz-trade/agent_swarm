#![cfg_attr(not(debug_assertions), windows_subsystem = "windows")]

use std::process::{Command, Child};
use std::sync::Mutex;

struct BoltProcess(Mutex<Option<Child>>);

fn find_bolt_binary() -> Option<String> {
    let candidates = vec![
        std::env::current_exe()
            .ok()
            .and_then(|p| p.parent().map(|d| d.join("bolt").to_string_lossy().to_string())),
        Some("bolt".to_string()),
        Some("/usr/local/bin/bolt".to_string()),
        Some("/usr/bin/bolt".to_string()),
    ];

    for candidate in candidates.into_iter().flatten() {
        if std::path::Path::new(&candidate).exists() || candidate == "bolt" {
            return Some(candidate);
        }
    }
    None
}

fn start_bolt_server(port: u16) -> Option<Child> {
    let binary = find_bolt_binary()?;
    let child = Command::new(&binary)
        .args(["api-server", "--port", &port.to_string()])
        .stdout(std::process::Stdio::null())
        .stderr(std::process::Stdio::null())
        .spawn()
        .ok()?;
    std::thread::sleep(std::time::Duration::from_millis(500));
    Some(child)
}

fn main() {
    let port: u16 = 19090;
    let bolt_child = start_bolt_server(port);
    if bolt_child.is_none() {
        eprintln!("Warning: Could not start bolt api-server.");
    }
    let bolt_state = BoltProcess(Mutex::new(bolt_child));

    tauri::Builder::default()
        .plugin(tauri_plugin_shell::init())
        .plugin(tauri_plugin_process::init())
        .manage(bolt_state)
        .run(tauri::generate_context!())
        .expect("error while running tauri application");
}

impl Drop for BoltProcess {
    fn drop(&mut self) {
        if let Some(ref mut child) = *self.0.lock().unwrap() {
            let _ = child.kill();
        }
    }
}
