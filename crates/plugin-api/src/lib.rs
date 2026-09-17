#![no_std]

pub mod d3d11;

use core::ffi::c_char;
use core::mem::size_of;
use core::ptr::null;

use crate::d3d11::SsmtPluginOnD3D11ReadyFn;

pub const SSMT_PLUGIN_ABI_VERSION: u32 = 2;

#[repr(C)]
pub struct SsmtPluginInfo {
    pub struct_size: u32,
    pub abi_version: u32,

    pub name: *const c_char,
    pub version: *const c_char,
    pub author: *const c_char,
}

impl SsmtPluginInfo {
    pub const fn query_buffer() -> Self {
        Self {
            struct_size: size_of::<Self>() as u32,
            abi_version: 0,
            name: null(),
            version: null(),
            author: null(),
        }
    }
    // pub const fn new(
    //     name: *const c_char,
    //     version: *const c_char,
    //     author: *const c_char,
    // ) -> Self {
    //     Self {
    //         struct_size: size_of::<Self>() as u32,
    //         abi_version: SSMT_PLUGIN_ABI_VERSION,

    //         name,
    //         version,
    //         author,
    //     }
    // }
}

pub type SsmtStatus = u32;

pub const SSMT_STATUS_OK: SsmtStatus = 0;
pub const SSMT_STATUS_INVALID_ARGUMENT: SsmtStatus = 1;
pub const SSMT_STATUS_UNSUPPORTED_ABI: SsmtStatus = 2;
pub const SSMT_STATUS_STRUCT_TOO_SMALL: SsmtStatus = 3;

pub type SsmtPluginQueryFn =
    unsafe extern "C" fn(
        host_abi_version: u32,

        out_info: *mut SsmtPluginInfo,

        out_api: *mut SsmtPluginApi,
    ) -> SsmtStatus;

pub type SsmtLogLevel = u8;

pub const SSMT_LOG_INFO: SsmtLogLevel = 0;
pub const SSMT_LOG_WARNING: SsmtLogLevel = 1;
pub const SSMT_LOG_ERROR: SsmtLogLevel = 2;

pub const SSMT_PLUGIN_INFO_BASE_SIZE: u32 =
    (core::mem::offset_of!(SsmtPluginInfo, author)
        + size_of::<*const c_char>()) as u32;

pub const SSMT_PLUGIN_API_BASE_SIZE: u32 =
    (core::mem::offset_of!(SsmtPluginApi, shutdown)
        + size_of::<Option<SsmtPluginShutdownFn>>())
        as u32;

#[repr(C)]
pub struct SsmtHostServices {
    pub struct_size: u32,
    pub abi_version: u32,

    pub log: unsafe extern "C" fn(
        level: SsmtLogLevel,
        message: *const c_char,
    ),
}

impl SsmtHostServices {
    pub const fn new(
        log: unsafe extern "C" fn(
            SsmtLogLevel,
            *const c_char,
        ),
    ) -> Self {
        Self {
            struct_size: size_of::<Self>() as u32,
            abi_version: SSMT_PLUGIN_ABI_VERSION,
            log,
        }
    }
}

pub type SsmtPluginInitializeFn =
    unsafe extern "C" fn(
        host: *const SsmtHostServices,
    ) -> SsmtStatus;

pub type SsmtPluginShutdownFn =
    unsafe extern "C" fn() -> SsmtStatus;

#[repr(C)]
pub struct SsmtPluginApi {
    pub struct_size: u32,
    pub abi_version: u32,

    pub initialize: Option<SsmtPluginInitializeFn>,

    pub shutdown: Option<SsmtPluginShutdownFn>,

    pub on_d3d11_ready: Option<SsmtPluginOnD3D11ReadyFn>,
}

impl SsmtPluginApi {
    pub const fn query_buffer() -> Self {
        Self {
            struct_size: size_of::<Self>() as u32,
            abi_version: 0,

            initialize: None,
            shutdown: None,
            on_d3d11_ready: None,
        }
    }
}
