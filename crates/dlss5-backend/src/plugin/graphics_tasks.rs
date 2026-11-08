#[derive(Debug, Clone, serde::Serialize)]
#[serde(rename_all = "camelCase")]
pub struct GraphicsProgress {
    pub phase: String,
    pub file: String,
    pub downloaded_bytes: u64,
    pub total_bytes: u64,
}
