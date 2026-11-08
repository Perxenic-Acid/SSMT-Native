pub mod commands;
pub mod graphics_tasks;
pub mod registry;
pub mod settings;
pub use ssmt_plugin_graphics::managed_reshade;
pub use ssmt_plugin_graphics::validate_package_relative_path;
pub mod dlss5;
pub mod dlss5_hot_runtime;
pub mod dlss5_policy;
pub mod gimi_hot_runtime;
pub mod managed_cold_files;
pub mod managed_reshade_journal;
pub mod managed_stack;
pub mod resource_cache;

pub mod hot_reshade {
    pub use ssmt_plugin_graphics::reshade_state::{
        save_config, PreparedConfig,
    };
    use std::path::Path;
    pub fn compose(
        registry: &super::registry::PluginRegistry,
        root: &Path,
        required: &[String],
        ini: &str,
        preset: &str,
    ) -> Result<PreparedConfig, String> {
        ssmt_plugin_graphics::reshade_state::compose(
            registry.contributions.clone(),
            root,
            required,
            ini,
            preset,
        )
    }
}
pub mod hoyoshade {
    pub const HOYOSHADE_PLUGIN_ID: &str =
        "ssmt.hoyoshade.bridge";
    pub const HOYOSHADE_DEPENDENCY_ID: &str = "hoyoshade";
}
