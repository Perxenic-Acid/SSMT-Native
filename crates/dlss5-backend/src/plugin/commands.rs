pub fn installed_plugin_registry(
) -> Result<Option<super::registry::PluginRegistry>, String>
{
    super::registry::PluginRegistry::from_default_location()
        .map(Some)
}
pub async fn read_graphics_status<T: Send + 'static>(
    operation: impl FnOnce() -> Result<T, String>
        + Send
        + 'static,
) -> Result<T, String> {
    tokio::task::spawn_blocking(operation)
        .await
        .map_err(|error| error.to_string())?
}
