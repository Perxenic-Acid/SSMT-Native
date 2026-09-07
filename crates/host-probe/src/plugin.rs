#[derive(debug)]
pub struct PluginMetadata {
    pub name: String,
    pub version: String,
    pub author: String,
}

use libloading::Library;

pub struct LoadedPlugin {
    library: Library,
    metadata: PluginMetadata,
}

use std::{ffi::CStr, path::Path};

use libloading::{Library, Symbol};

use ssmt_plugin_api::{SSMT_PLUGIN_ABI_VERSION, SSMT_STATUS_OK, SsmtPluginInfo, SsmtPluginQueryFn};

impl LoadedPlugin {
    pub fn load(path: &Path) -> Result<Self, Box<dyn std::error::Error>> {
        let library = unsafe { Library::new(path)? };

        let query: Symbol<SsmtPluginInfo> = unsafe { library.get(b"SSMTPlugin_Query\0")? };

        let mut info = SsmtPluginInfo::empty();

        let status = unsafe { query(SSMT_PLUGIN_ABI_VERSION, &mut info) };

        if status != SSMT_STATUS_OK {
            return Err(format!("SSMTPlugin_Query failed: status={status}").into());
        }

        if info.name.is_null() || info.version.is_null() || info.author.is_null() {
            return Err("Plugin returned invalid metadata pointers.".into());
        }

        let (name, version, author) = unsafe {
            (
                CStr::from_ptr(info.name),
                CStr::from_ptr(info.version),
                CStr::from_ptr(info.author),
            )
        };

        let metadata = PluginMetadata {
            name: name.to_str()?.to_owned(),
            version: version.to_str()?.to_owned(),
            author: author.to_str()?.to_owned(),
        };

        Ok(Self { library, metadata })
    }

    pub fn metadata(&self) -> &PluginMetadata {
        &self.metadata
    }
}
