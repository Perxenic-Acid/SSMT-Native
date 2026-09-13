mod color_ansi;
mod config;
mod manager;
mod plugin;

pub use manager::PluginManager;

pub use plugin::{LoadedPlugin, PluginMetadata};

use core::ffi::c_void;

use std::sync::{Mutex, OnceLock};

use crate::config::HostConfig;

static PLUGIN_MANAGER: OnceLock<Mutex<PluginManager>> =
    OnceLock::new();

#[unsafe(no_mangle)]
pub unsafe extern "system" fn SSMTPluginHost_Main(
    parameter: *mut c_void, // LPVOID parameter
) -> u32 // DWORD WINAPI
{
    match std::panic::catch_unwind(|| unsafe {
        start_host_from_parameter(parameter)
    }) {
        Ok(status) => status,

        Err(_) => {
            eprintln!(
                "[PluginHost] Fatal panic during startup."
            );

            0xFFFF_FFFF
        }
    }
}

unsafe fn start_host_from_parameter(
    parameter: *mut c_void,
) -> u32 {
    let config_path = parameter.cast::<u16>();
    if config_path.is_null() {
        return 1;
    }

    let config_path = match unsafe {
        config::wide_ptr_to_path(parameter.cast())
    } {
        Some(path) => path,
        None => return 1,
    };

    match start_host(&config_path) {
        Ok(()) => 0,
        Err(error) => {
            eprintln!(
                "[PluginHost] Failed to start: {error}"
            );

            2
        }
    }
}

fn start_host(
    config_path: &std::path::Path,
) -> Result<(), Box<dyn std::error::Error>> {
    let config = HostConfig::load(config_path)?;

    let plugin_directory =
        config.resolve_plugin_directory(config_path);

    let mut manager = PluginManager::new();

    manager
        .load_plugins(&plugin_directory, &config.plugins);

    PLUGIN_MANAGER
        .set(Mutex::new(manager))
        .map_err(|_| "PluginHost is already initialized")?;

    Ok(())
}
