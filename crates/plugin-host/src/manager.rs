use std::{
    default, fs,
    path::{Path, PathBuf},
};

use crate::{LoadedPlugin, plugin};

pub struct PluginManager {
    plugins: Vec<LoadedPlugin>,
}

impl PluginManager {
    pub fn new() -> Self {
        Self { plugins: Vec::new() }
    }

    pub fn plugins(&self) -> &[LoadedPlugin] {
        &self.plugins
    }

    pub fn load_directory(
        &mut self,
        directory: &Path,
    ) -> std::io::Result<()> {
        let mut plugin_paths = Vec::new();

        for entry in fs::read_dir(directory)? {
            let entry = entry?;
            let path = entry.path();

            if !path.is_file() {
                continue;
            }

            let is_dll = path
                .extension()
                .and_then(|extension| extension.to_str())
                .is_some_and(|extension| {
                    extension.eq_ignore_ascii_case("dll")
                });

            if !is_dll {
                continue;
            }

            plugin_paths.push(path);
        }

        plugin_paths.sort();

        for path in plugin_paths {
            println!("Loading plugin: {}", path.display());

            match LoadedPlugin::load(&path) {
                Ok(plugin) => {
                    println!(
                        "Loaded plugin: {} {}",
                        plugin.metadata().name,
                        plugin.metadata().version
                    );

                    self.plugins.push(plugin);
                }

                Err(error) => {
                    eprintln!(
                        "Failed to load plugin {}: {error}",
                        path.display()
                    );
                }
            }
        }

        Ok(())
    }
}

impl Default for PluginManager {
    fn default() -> Self {
        Self::new()
    }
}
