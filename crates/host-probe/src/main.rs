// use std::{env, path::PathBuf};

// use ssmt_plugin_host::{LoadedPlugin, PluginManager};

// fn main() -> Result<(), Box<dyn std::error::Error>> {
//     let plugin_directory = env::args_os()
//         .nth(1)
//         .map(PathBuf::from)
//         .unwrap_or_else(|| {
//             PathBuf::from(
//                 "Plugins",
//             )
//         });

//     println!("Plugin directory: {}", plugin_directory.display());

//     let mut manager = PluginManager::new();

//     manager.load_directory(&plugin_directory)?;

//     println!();
//     println!(
//         "{} plugin(s) loaded.",
//         manager.plugins().len()
//     );

//     for plugin in manager.plugins() {
//         let metadata = plugin.metadata();

//         println!(
//             "- {} {} by {}",
//             metadata.name,
//             metadata.version,
//             metadata.author,
//         )
//     }

//     Ok(())
// }

mod plugin;
mod color_ansi;

use std::{ffi::OsStr, os::windows::ffi::OsStrExt};

use ssmt_plugin_host::SSMTPluginHost_Main;

fn main() {
    let mut config_path: Vec<u16> =
        OsStr::new("SSMT-PluginHost.json")
            .encode_wide()
            .chain(Some(0))
            .collect();

    let status = unsafe {
        SSMTPluginHost_Main(config_path.as_mut_ptr().cast())
    };

    println!("PluginHost exit status: {status}");
}
