use std::{
    fs::{File, OpenOptions},
    io::Write,
    path::Path,
    sync::{Mutex, OnceLock},
};

static LOG_FILE: OnceLock<Mutex<File>> = OnceLock::new();

pub fn initialize(
    path: &Path,
) -> Result<(), Box<dyn std::error::Error>> {
    let file = OpenOptions::new()
        .create(true)
        .append(true)
        .open(path)?;

    LOG_FILE.set(Mutex::new(file)).map_err(
        |_| "PluginHost logger is already initialized",
    )?;

    Ok(())
}

pub fn write_line(line: &str) {
    let Some(log_file) = LOG_FILE.get() else {
        return;
    };

    let Ok(mut file) = log_file.lock() else {
        return;
    };

    let _ = writeln!(file, "{line}");

    let _ = file.flush();
}
