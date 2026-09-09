mod color_ansi;
mod manager;
mod plugin;

pub use manager::PluginManager;

pub use plugin::{LoadedPlugin, PluginMetadata};

#[unsafe(no_mangle)]
pub unsafe extern "system" fn SSMTPluginHost_Main(
    config_path: *const u16,
) -> u32 {
    if config_path.is_null() { 1 } else { 0 }
}
