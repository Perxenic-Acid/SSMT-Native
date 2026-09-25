use std::path::Path;

use crate::{
    LoadedPlugin, render_dispatch::RenderDispatch,
};

pub struct PluginManager {
    plugins: Vec<LoadedPlugin>,
    render_dispatch: RenderDispatch,
}

impl PluginManager {
    pub fn new() -> Self {
        Self {
            plugins: Vec::new(),
            render_dispatch: RenderDispatch::from_plugins(
                &[],
            ),
        }
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

        self.render_dispatch =
            RenderDispatch::from_plugins(&self.plugins);
    }
}

use ssmt_plugin_api::d3d11::{
    SsmtD3D11Context, SsmtPresentContext,
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

    pub fn dispatch_present(
        &self,
        context: &SsmtPresentContext,
    ) {
        self.render_dispatch.dispatch_present(context);
    }
}

impl Default for PluginManager {
    fn default() -> Self {
        Self::new()
    }
}
