use std::{env, path::PathBuf};

use ssmt_plugin_host::{LoadedPlugin, PluginManager};

fn main() -> Result<(), Box<dyn std::error::Error>> {
    let plugin_directory = env::args_os()
        .nth(1)
        .map(PathBuf::from)
        .unwrap_or_else(|| {
            PathBuf::from(
                "Plugins",
            )
        });

    println!("Plugin directory: {}", plugin_directory.display());

    let mut manager = PluginManager::new();

    manager.load_directory(&plugin_directory)?;


    println!();
    println!(
        "{} plugin(s) loaded.",
        manager.plugins().len()
    );

    for plugin in manager.plugins() {
        let metadata = plugin.metadata();

        println!(
            "- {} {} by {}",
            metadata.name,
            metadata.version,
            metadata.author,
        )
    }

    Ok(())
}
