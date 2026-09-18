

mod plugin;
mod color_ansi;

// #[path ="bin/abi-query.rs"]
// mod abi_query;

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
