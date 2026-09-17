use std::{
    cmp::Ordering,
    ffi::{CStr, c_char},
    path::Path,
};

use libloading::{Library, Symbol};

use ssmt_plugin_api::{
    SSMT_LOG_ERROR, SSMT_LOG_INFO, SSMT_LOG_WARNING,
    SSMT_PLUGIN_ABI_VERSION, SSMT_STATUS_INVALID_ARGUMENT,
    SSMT_STATUS_OK, SsmtHostServices, SsmtLogLevel,
    SsmtPluginApi, SsmtPluginInfo, SsmtPluginQueryFn,
    SsmtStatus,
};

#[derive(Debug)]
pub struct PluginMetadata {
    pub name: String,
    pub version: String,
    pub author: String,
}

pub struct LoadedPlugin {
    _library: Library,
    _host_services: Box<SsmtHostServices>,

    api: SsmtPluginApi,

    metadata: PluginMetadata,
}

impl LoadedPlugin {
    pub fn load(
        path: &Path,
    ) -> Result<Self, Box<dyn std::error::Error>> {
        let library = unsafe { Library::new(path)? };

        // dll 唯一按名查找入口
        let query: Symbol<SsmtPluginQueryFn> =
            unsafe { library.get(b"SSMTPlugin_Query\0")? };

        let mut info = SsmtPluginInfo::query_buffer();

        let mut api = SsmtPluginApi::query_buffer();

        let info_capacity = info.struct_size;

        let api_capacity = api.struct_size;

        let status = unsafe {
            query(
                SSMT_PLUGIN_ABI_VERSION,
                &mut info,
                &mut api,
            )
        };

        if info.struct_size != info_capacity {
            return Err(
                "Plugin modified SsmtPluginInfo.struct_size"
                    .into()
            );
        }

        if api.struct_size != api_capacity {
            return Err(
                "Plugin modified SsmtPluginApi.struct_size"
                    .into(),
            );
        }

        if info.abi_version != SSMT_PLUGIN_ABI_VERSION {
            return Err(
                format!(
                    "Plugin info ABI mismatch: host={}, plugin={}",
                    SSMT_PLUGIN_ABI_VERSION,
                    info.abi_version
                )
                .into()
            );
        }

        if api.abi_version != SSMT_PLUGIN_ABI_VERSION {
            return Err(
                format!(
                    "Plugin API ABI mismatch: host={}, plugin={}",
                    SSMT_PLUGIN_ABI_VERSION,
                    api.abi_version
                )
                .into()
            );
        }

        if status != SSMT_STATUS_OK {
            return Err(format!(
                "SSMTPlugin_Query failed: status={status}"
            )
            .into());
        }

        if info.name.is_null()
            || info.version.is_null()
            || info.author.is_null()
        {
            return Err("Plugin returned invalid metadata pointers.".into());
        }

        if api.initialize.is_none() {
            return Err("Plugin did not provide initialize callback".into());
        }

        if api.shutdown.is_none() {
            return Err(
                "Plugin did not provide shutdown callback"
                    .into(),
            );
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

        let host_services =
            Box::new(SsmtHostServices::new(host_log));

        let initialize = api
            .initialize
            .expect("initialize was checked above");

        let status =
            unsafe { initialize(host_services.as_ref()) };

        if status != SSMT_STATUS_OK {
            return Err(format!("SSMTPlugin_Initialize failed: status={status}").into());
        }

        Ok(Self {
            _library: library,
            _host_services: host_services,
            api,
            metadata,
        })
    }

    pub fn metadata(&self) -> &PluginMetadata {
        &self.metadata
    }
}

impl Drop for LoadedPlugin {
    fn drop(&mut self) {
        let Some(shutdown) = self.api.shutdown else {
            return;
        };
        let status = unsafe { (shutdown)() };

        if status != SSMT_STATUS_OK {
            crate::logger::write_line(&format!(
                "SsmtPlugin_Shutdown failed: status={status}"
            ));
        }
    }
}

unsafe extern "C" fn host_log(
    level: SsmtLogLevel,
    message: *const c_char,
) {
    if message.is_null() {
        return;
    }

    let message = unsafe { CStr::from_ptr(message) };

    let message = message.to_string_lossy();

    use crate::color_ansi::*;

    let prefix = match level {
        SSMT_LOG_INFO => {
            println!(
                "{BOLD}{GREEN}[Info]{RESET} {message}"
            );
            "[Info]"
        }

        SSMT_LOG_WARNING => {
            println!(
                "{BOLD}{YELLOW}[Warning]{RESET} {message}"
            );
            "[Warning]"
        }

        SSMT_LOG_ERROR => {
            eprintln!(
                "{BOLD}{RED}[Error]{RESET} {message}"
            );
            "[Error]"
        }

        _ => {
            println!(
                "{BOLD}{RED}[Unknown Log Type: {level}]{RESET} {message}"
            );
            "[Unknown]"
        }
    };

    crate::logger::write_line(&format!(
        "{prefix} {message}"
    ));
}

use ssmt_plugin_api::d3d11::SsmtD3D11Context;

impl LoadedPlugin {
    pub(crate) fn on_d3d11_ready(
        &self,
        context: &SsmtD3D11Context,
    ) -> Result<(), SsmtStatus> {
        let Some(callback) = self.api.on_d3d11_ready else {
            return Ok(());
        };

        let status = unsafe { callback(context) };

        if status == SSMT_STATUS_OK {
            Ok(())
        } else {
            Err(status)
        }
    }
}
