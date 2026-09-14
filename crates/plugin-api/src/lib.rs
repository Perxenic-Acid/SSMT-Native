#![no_std]

use core::cmp::Ordering;
use core::ffi::c_char;
use core::mem::size_of;
use core::ptr::{null, null_mut};

pub const SSMT_PLUGIN_ABI_VERSION: u32 = 1;

#[repr(C)]
pub struct SsmtPluginInfo {
    pub struct_size: u32,
    pub abi_version: u32,

    pub name: *const c_char,
    pub version: *const c_char,
    pub author: *const c_char,
}

impl SsmtPluginInfo {
    pub const fn empty() -> Self {
        Self {
            struct_size: size_of::<Self>() as u32,
            abi_version: SSMT_PLUGIN_ABI_VERSION,
            name: null(),
            version: null(),
            author: null(),
        }
    }
    pub const fn new(
        name: *const c_char,
        version: *const c_char,
        author: *const c_char,
    ) -> Self {
        Self {
            struct_size: size_of::<Self>() as u32,
            abi_version: SSMT_PLUGIN_ABI_VERSION,

            name,
            version,
            author,
        }
    }
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
    ) -> SsmtStatus;

pub type SsmtLogLevel = u8;

pub const SSMT_LOG_INFO: SsmtLogLevel = 0;
pub const SSMT_LOG_WARNING: SsmtLogLevel = 1;
pub const SSMT_LOG_ERROR: SsmtLogLevel = 2;

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



#[cfg(test)]
mod tests {
    use super::*;
    use core::ffi::CStr;

    static TEST_NAME: &[u8] = b"SSMT Test Plugin\0";

    static TEST_VERSION: &[u8] = b"0.1.0\0";

    static TEST_AUTHOR: &[u8] = b"Perxenic Acid\0";

    #[test]
    fn plugin_info_layout_is_expected() {
        assert_eq!(size_of::<SsmtPluginInfo>(), 32);
    }

    #[test]
    fn plugin_info_strings_can_be_read() {
        let info = SsmtPluginInfo::new(
            TEST_NAME.as_ptr().cast(),
            TEST_VERSION.as_ptr().cast(),
            TEST_AUTHOR.as_ptr().cast(),
        );

        let name = unsafe { CStr::from_ptr(info.name) };

        let version =
            unsafe { CStr::from_ptr(info.version) };

        let author = unsafe { CStr::from_ptr(info.author) };

        assert_eq!(
            name.to_str().unwrap(),
            "SSMT Test Plugin"
        );

        assert_eq!(version.to_str().unwrap(), "0.1.0");

        assert_eq!(
            author.to_str().unwrap(),
            "Perxenic Acid"
        );
    }

    unsafe extern "C" fn test_query(
        host_abi_version: u32,
        out_info: *mut SsmtPluginInfo,
    ) -> SsmtStatus {
        if host_abi_version != SSMT_PLUGIN_ABI_VERSION {
            return SSMT_STATUS_UNSUPPORTED_ABI;
        }

        if out_info.is_null() {
            return SSMT_STATUS_INVALID_ARGUMENT;
        }

        SSMT_STATUS_OK
    }

    #[test]
    fn query_function_matches_abi_type() {
        let query: SsmtPluginQueryFn = test_query;

        let mut info = SsmtPluginInfo::empty();

        let status = unsafe {
            query(SSMT_PLUGIN_ABI_VERSION, &mut info)
        };

        assert_eq!(status, SSMT_STATUS_OK);
    }

    #[test]
    fn query_rejects_unsupported_abi() {
        let query: SsmtPluginQueryFn = test_query;

        let mut info = SsmtPluginInfo::empty();

        let status = unsafe { query(999, &mut info) };

        assert_eq!(status, SSMT_STATUS_UNSUPPORTED_ABI);
    }

    #[test]
    fn query_rejects_null_output() {
        let query: SsmtPluginQueryFn = test_query;

        let status = unsafe {
            query(
                SSMT_PLUGIN_ABI_VERSION,
                core::ptr::null_mut(),
            )
        };

        assert_eq!(status, SSMT_STATUS_INVALID_ARGUMENT);
    }
}
