use ssmt_plugin_api::{
    SSMT_STATUS_OK,
    d3d11::{SsmtPluginOnPresentFn, SsmtPresentContext},
};

pub struct RenderDispatch {
    present: Box<[PresentCallback]>,
}

struct PresentCallback {
    callback: SsmtPluginOnPresentFn,

    plugin_name: Box<str>,

    error_reported: std::sync::atomic::AtomicBool,
}

impl RenderDispatch {
    pub fn from_plugins(
        plugins: &[crate::LoadedPlugin],
    ) -> Self {
        let present = plugins
            .iter()
            .filter_map(|plugin| {
                plugin.present_callback().map(|callback| {
                    PresentCallback {
                        callback,
                        plugin_name: plugin
                            .metadata()
                            .name
                            .clone()
                            .into_boxed_str(),
                        error_reported: std::sync::atomic::AtomicBool::new(false),
                    }
                })
            })
            .collect::<Vec<_>>()
            .into_boxed_slice();

        Self { present }
    }

    pub fn dispatch_present(
        &self,
        context: &SsmtPresentContext,
    ) {
        for entry in &self.present {
            let status =
                unsafe { (entry.callback)(context) };

            if status != SSMT_STATUS_OK {
                if !entry.error_reported.swap(
                    true,
                    std::sync::atomic::Ordering::Relaxed,
                ) {
                    crate::logger::write_line(&format!(
                        "[PluginHost] Plugin {} returned status={} from on_present",
                        entry.plugin_name, status
                    ));
                }
            }
        }
    }
}
