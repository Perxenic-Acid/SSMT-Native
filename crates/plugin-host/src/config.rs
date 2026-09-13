use std::path::PathBuf;

use serde::Deserialize;

#[derive(Debug, Deserialize)]
#[serde(rename_all = "camelCase")]
pub struct HostConfig {
    pub plugin_directory: PathBuf,
    pub plugins: Vec<String>,
}

use std::{
    ffi::OsString, os::windows::ffi::OsStringExt, slice,
};

pub unsafe fn wide_ptr_to_path(
    ptr: *const u16,
) -> Option<PathBuf> {
    if ptr.is_null() {
        return None;
    }

    let mut len = 0usize;

    while unsafe { *ptr.add(len) } != 0 {
        len += 1;
    }

    let wide = unsafe { slice::from_raw_parts(ptr, len) };

    Some(PathBuf::from(OsString::from_wide(wide)))
}

type Er = Box<dyn std::error::Error>;

impl HostConfig {
    pub fn load(
        path: &std::path::Path,
    ) -> Result<Self, Er> {
        let content = std::fs::read_to_string(path)?;

        let config = serde_json::from_str(&content)?;

        Ok(config)
    }
}
impl HostConfig {
    pub fn resolve_plugin_directory(
        &self,
        config_path: &std::path::Path,
    ) -> PathBuf {
        if self.plugin_directory.is_absolute() {
            return self.plugin_directory.clone();
        }

        let base = config_path
            .parent()
            .unwrap_or_else(|| std::path::Path::new("."));

        base.join(&self.plugin_directory)
    }
}
