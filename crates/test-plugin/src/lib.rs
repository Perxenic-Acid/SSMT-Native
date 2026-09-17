use ssmt_plugin_api::{
    SSMT_LOG_ERROR, SSMT_LOG_INFO, SSMT_PLUGIN_ABI_VERSION,
    SSMT_PLUGIN_API_BASE_SIZE, SSMT_PLUGIN_INFO_BASE_SIZE,
    SSMT_STATUS_INVALID_ARGUMENT, SSMT_STATUS_OK,
    SSMT_STATUS_STRUCT_TOO_SMALL,
    SSMT_STATUS_UNSUPPORTED_ABI, SsmtHostServices,
    SsmtPluginApi, SsmtPluginInfo, SsmtStatus,
    d3d11::SSMT_PLUGIN_API_D3D11_READY_SIZE,
};

use core::{
    mem::size_of,
    ptr::null_mut,
    sync::atomic::{AtomicPtr, Ordering},
};

static HOST: AtomicPtr<SsmtHostServices> =
    AtomicPtr::new(null_mut());

const PLUGIN_NAME: &[u8] = b"SSMT Test Plugin\0";

const PLUGIN_VERSION: &[u8] = b"0.1.0\0";

const PLUGIN_AUTHOR: &[u8] = b"Perxenic Acid\0";

#[unsafe(no_mangle)]
pub unsafe extern "C" fn SSMTPlugin_Query(
    host_abi_version: u32,
    out_info: *mut SsmtPluginInfo,
    out_api: *mut SsmtPluginApi,
) -> SsmtStatus {
    if host_abi_version != SSMT_PLUGIN_ABI_VERSION {
        return SSMT_STATUS_UNSUPPORTED_ABI;
    }

    if out_info.is_null() || out_api.is_null() {
        return SSMT_STATUS_INVALID_ARGUMENT;
    }

    let info_capacity =
        unsafe { out_info.cast::<u32>().read() };

    let api_capacity =
        unsafe { out_api.cast::<u32>().read() };

    if info_capacity < SSMT_PLUGIN_INFO_BASE_SIZE {
        return SSMT_STATUS_STRUCT_TOO_SMALL;
    }

    if api_capacity < SSMT_PLUGIN_API_BASE_SIZE {
        return SSMT_STATUS_STRUCT_TOO_SMALL;
    }

    unsafe {
        core::ptr::addr_of_mut!((*out_info).abi_version)
            .write(SSMT_PLUGIN_ABI_VERSION);

        core::ptr::addr_of_mut!((*out_info).name)
            .write(PLUGIN_NAME.as_ptr().cast());

        core::ptr::addr_of_mut!((*out_info).author)
            .write(PLUGIN_AUTHOR.as_ptr().cast());

        core::ptr::addr_of_mut!((*out_api).abi_version)
            .write(SSMT_PLUGIN_ABI_VERSION);

        core::ptr::addr_of_mut!((*out_api).initialize)
            .write(Some(plugin_initialize));

        core::ptr::addr_of_mut!((*out_api).shutdown)
            .write(Some(plugin_shutdown));
    }

    // optional
    if api_capacity >= SSMT_PLUGIN_API_D3D11_READY_SIZE {
        unsafe {
            core::ptr::addr_of_mut!(
                (*out_api).on_d3d11_ready
            )
            .write(Some(plugin_on_d3d11_ready));
        }
    }

    SSMT_STATUS_OK
}

static INIT_MESSAGE: &[u8] =
    b"Hello from SSMT Test Plugin.\0";

// static TEST_ERROR_MESSAGE: &[u8] = b"Oh No, I'm died!";

unsafe extern "C" fn plugin_initialize(
    host: *const SsmtHostServices,
) -> SsmtStatus {
    if host.is_null() {
        return SSMT_STATUS_INVALID_ARGUMENT;
    }

    HOST.store(host.cast_mut(), Ordering::Release);

    let host = unsafe { &*host };

    unsafe {
        (host.log)(
            SSMT_LOG_INFO,
            INIT_MESSAGE.as_ptr().cast(),
        );
    }

    SSMT_STATUS_OK
}

static SHUTDOWN_MESSAGE: &[u8] =
    b"SSMT Test Plugin shutting down.\0";

unsafe extern "C" fn plugin_shutdown() -> SsmtStatus {
    let host = HOST.swap(null_mut(), Ordering::AcqRel);

    if !host.is_null() {
        let host = unsafe { &*host };

        unsafe {
            (host.log)(
                SSMT_LOG_INFO,
                SHUTDOWN_MESSAGE.as_ptr().cast(),
            );
        }
    }

    SSMT_STATUS_OK
}

use ssmt_plugin_api::d3d11::SsmtD3D11Context;

static D3D11_READY_MESSAGE: &[u8] =
    b"D3D11 environment is ready.\0";

unsafe extern "C" fn plugin_on_d3d11_ready(
    context: *const SsmtD3D11Context,
) -> SsmtStatus {
    if context.is_null() {
        return SSMT_STATUS_INVALID_ARGUMENT;
    }

    let context = unsafe { &*context };

    if context.device.is_null()
        || context.immediate_context.is_null()
        || context.swap_chain.is_null()
    {
        return SSMT_STATUS_INVALID_ARGUMENT;
    }

    let host = HOST.load(Ordering::Acquire);

    if !host.is_null() {
        let host = unsafe { &*host };

        unsafe {
            (host.log)(
                SSMT_LOG_INFO,
                D3D11_READY_MESSAGE.as_ptr().cast(),
            );
        }
    }

    SSMT_STATUS_OK
}
