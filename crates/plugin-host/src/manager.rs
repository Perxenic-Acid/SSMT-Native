use std::path::Path;

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

    pub fn load_plugins(
        &mut self,
        directory: &Path,
        plugin_names: &[String],
    ) {
        for plugin_name in plugin_names {
            let path = directory.join(plugin_name);

            crate::logger::write_line(&format!(
                "[PluginHost] Loading plugin: {}",
                path.display()
            ));

            match LoadedPlugin::load(&path) {
                Ok(plugin) => {
                    crate::logger::write_line(&format!(
                        "[PluginHost] Loaded plugin: {} {}",
                        plugin.metadata().name,
                        plugin.metadata().version,
                    ));

                    self.plugins.push(plugin);
                }
                Err(error) => {
                    crate::logger::write_line(&format!(
                        "[PluginHost] Failed to load plugin {}: {error}",
                        path.display()
                    ));
                }
            }
        }
    }
}

use ssmt_plugin_api::d3d11::{
    SsmtD3D11Context, SsmtPluginOnPresentFn,
    SsmtPresentContext,
};
impl PluginManager {
    pub fn notify_d3d11_ready(
        &self,
        context: &SsmtD3D11Context,
    ) {
        for plugin in &self.plugins {
            if let Err(status) =
                plugin.on_d3d11_ready(context)
            {
                crate::logger::write_line(&format!(
                    "[PluginHost] Plugin {} rejected D3D11 context: status={status}",
                    plugin.metadata().name
                ));
            }
        }
    }
}

impl Default for PluginManager {
    fn default() -> Self {
        Self::new()
    }
}

pub struct RenderDispatch {
    present: Box<[SsmtPluginOnPresentFn]>,
}

