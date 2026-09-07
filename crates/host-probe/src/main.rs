mod plugin;

use std::{env, path::PathBuf};

use plugin::LoadedPlugin;

fn main() -> Result<(), Box<dyn std::error::Error>> {
    // println!("Hello, world!");

    let plugin_path = env::args_os()
        .nth(1)
        .map(PathBuf::from)
        .unwrap_or_else(|| PathBuf::from("target/debug/ssmt_test_plugin.dll"));

    println!("Loading plugin: {}", plugin_path.display());
    { // let library = unsafe { Library::new(&plugin_path)? };

        // let query: Symbol<SsmtPluginQueryFn> = unsafe { library.get(b"SSMTPlugin_Query\0")? };

        // let mut info = SsmtPluginInfo::empty();

        // let status = unsafe { query(SSMT_PLUGIN_ABI_VERSION, &mut info) };

        // if status != SSMT_STATUS_OK {
        //     return Err(format!("SSMTPlugin_Query failed: status={status}").into());
        // }

        // if info.name.is_null() || info.version.is_null() || info.author.is_null() {
        //     return Err("Plugin returned invalid metadata pointers".into());
        // }

        // let (name, version, author) = unsafe {
        //     (
        //         CStr::from_ptr(info.name),
        //         CStr::from_ptr(info.version),
        //         CStr::from_ptr(info.author),
        //     )
        // };

        // println!("Name: {}", name.to_str()?);
        // println!("Version: {}", version.to_str()?);
        // println!("Author: {}", author.to_str()?);

        // drop(library);
    }

    let plugin = LoadedPlugin::load(&plugin_path)?;

    let metadata = plugin.metadata();

    println!("Name: {}", metadata.name);
    println!("Version: {}", metadata.version);
    println!("Author: {}", metadata.author);

    Ok(())
}
