use std::{
    ffi::{CStr, c_char},
    path::Path,
};

use libloading::{Library, Symbol};

use ssmt_plugin_api::{
    SSMT_LOG_ERROR, SSMT_LOG_INFO, SSMT_LOG_WARNING,
    SSMT_PLUGIN_ABI_VERSION, SSMT_STATUS_OK,
    SsmtHostServices, SsmtLogLevel, SsmtPluginApi,
    SsmtPluginInfo, SsmtPluginQueryFn,
    SsmtPluginShutdownFn,
};

#[derive(Debug)]
pub struct PluginMetadata {
    pub name: String,
    pub version: String,
    pub author: String,
}

// use libloading::Library;

pub struct LoadedPlugin {
    _library: Library,
    _host_services: Box<SsmtHostServices>,

    shutdown: SsmtPluginShutdownFn,

    metadata: PluginMetadata,
}

impl LoadedPlugin {
    pub fn load(
        path: &Path,
    ) -> Result<Self, Box<dyn std::error::Error>> {
        let library = unsafe { Library::new(path)? };

        let query: Symbol<SsmtPluginQueryFn> =
            unsafe { library.get(b"SSMTPlugin_Query\0")? };

        let mut info = SsmtPluginInfo::query_buffer();

        let mut api = SsmtPluginApi::query_buffer();

        let status = unsafe {
            query(
                SSMT_PLUGIN_ABI_VERSION,
                &mut info,
                &mut api,
            )
        };

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

        let initialize = api.initialize.ok_or(
            "Plugin did not provide initialize callback",
        )?;

        let shutdown = api.shutdown.ok_or(
            "Plugin did not provide shutdown callback",
        )?;

        let host_services =
            Box::new(SsmtHostServices::new(host_log));

        let status =
            unsafe { initialize(host_services.as_ref()) };

        if status != SSMT_STATUS_OK {
            return Err(format!("SSMTPlugin_Initialize failed: status={status}").into());
        }

        Ok(Self {
            _library: library,
            _host_services: host_services,
            shutdown,
            metadata,
        })
    }

    pub fn metadata(&self) -> &PluginMetadata {
        &self.metadata
    }
}

impl Drop for LoadedPlugin {
    fn drop(&mut self) {
        let status = unsafe { (self.shutdown)() };

        if status != SSMT_STATUS_OK {
            eprintln!(
                "SsmtPlugin_Shutdown failed: status={status}"
            );
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

    match level {
        SSMT_LOG_INFO => {
            println!(
                "{BOLD}{GREEN}[Info]{RESET} {message}"
            );
        }

        SSMT_LOG_WARNING => {
            println!(
                "{BOLD}{YELLOW}[Warning]{RESET} {message}"
            );
        }

        SSMT_LOG_ERROR => {
            eprintln!(
                "{BOLD}{RED}[Error]{RESET} {message}"
            );
        }

        _ => {
            println!(
                "{BOLD}{RED}[Unknown Log Type: {level}]{RESET} {message}"
            );
        }
    }
}
