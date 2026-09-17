mod color_ansi;
mod config;
mod logger;
mod manager;
mod plugin;

pub use manager::PluginManager;

pub use plugin::{LoadedPlugin, PluginMetadata};

use core::ffi::c_void;

use std::{
    path::Path,
    sync::{Mutex, OnceLock},
};

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

    let log_path = config_path
        .parent()
        .unwrap_or_else(|| Path::new("."))
        .join("SSMT-PluginHost.log");

    logger::initialize(&log_path)?;

    logger::write_line("[PluginHost] Starting.");

    let plugin_directory =
        config.resolve_plugin_directory(config_path);

    let mut manager = PluginManager::new();

    manager
        .load_plugins(&plugin_directory, &config.plugins);

    PLUGIN_MANAGER
        .set(Mutex::new(manager))
        .map_err(|_| "PluginHost is already initialized")?;

    logger::write_line(
        "[PluginHost] Started successfully.",
    );

    Ok(())
}

use ssmt_plugin_api::d3d11::SsmtD3D11Context;

#[unsafe(no_mangle)]
pub unsafe extern "system" fn SSMTPluginHost_OnD3D11Ready(
    device: *mut c_void,
    immediate_context: *mut c_void,
    swap_chain: *mut c_void,
) -> u32 {
    match std::panic::catch_unwind(|| {
        on_d3d11_ready(
            device,
            immediate_context,
            swap_chain,
        )
    }) {
        Ok(status) => status,

        Err(_) => {
            logger::write_line(
                "[PluginHost] Panic during D3D11Ready.",
            );

            0xFFFF_FFFF
        }
    }
}

fn on_d3d11_ready(
    device: *mut c_void,
    immediate_context: *mut c_void,
    swap_chain: *mut c_void,
) -> u32 {
    if device.is_null()
        || immediate_context.is_null()
        || swap_chain.is_null()
    {
        logger::write_line(
            "[PluginHost] D3D11Ready received null pointer.",
        );

        return 1;
    }

    let Some(manager) = PLUGIN_MANAGER.get() else {
        logger::write_line(
            "[PluginHost] D3D11Ready arrived before PluginManager initialization.",
        );

        return 2;
    };

    let Ok(manager) = manager.lock() else {
        logger::write_line(
            "[PluginHost] Failed to lock PluginManager during D3D11Ready.",
        );

        return 3;
    };

    logger::write_line(&format!(
        "[PluginHost] D3D11Ready: device={device:p}, context={immediate_context:p}, swap_chain={swap_chain:p}"
    ));

    let context = SsmtD3D11Context::new(
        device,
        immediate_context,
        swap_chain,
    );

    manager.notify_d3d11_ready(&context);

    0
}
