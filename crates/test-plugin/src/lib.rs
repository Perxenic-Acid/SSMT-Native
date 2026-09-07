use ssmt_plugin_api::{
    SSMT_PLUGIN_ABI_VERSION, SSMT_STATUS_INVALID_ARGUMENT, SSMT_STATUS_OK, SSMT_STATUS_STRUCT_TOO_SMALL, SSMT_STATUS_UNSUPPORTED_ABI, SsmtPluginInfo, SsmtStatus,
};

const PLUGIN_NAME: &[u8] = b"SSMT Test Plugin\0";

const PLUGIN_VERSION: &[u8] = b"0.1.0\0";

const PLUGIN_AUTHOR: &[u8] = b"Perxenic Acid\0";

#[unsafe(no_mangle)]
pub unsafe extern "C" fn SSMTPlugin_Query(
    host_abi_version: u32,
    out_info: *mut SsmtPluginInfo,
) -> SsmtStatus {
    if host_abi_version != SSMT_PLUGIN_ABI_VERSION {
        return SSMT_STATUS_UNSUPPORTED_ABI;
    }

    if out_info.is_null() {
        return SSMT_STATUS_INVALID_ARGUMENT;
    }

    let info = unsafe { &mut *out_info };

    if info.struct_size < size_of::<SsmtPluginInfo>() as u32{
        return  SSMT_STATUS_STRUCT_TOO_SMALL;
    }

    info.abi_version = SSMT_PLUGIN_ABI_VERSION;

    info.name = PLUGIN_NAME.as_ptr().cast();
    info.version = PLUGIN_VERSION.as_ptr().cast();
    info.author = PLUGIN_AUTHOR.as_ptr().cast();

    SSMT_STATUS_OK
}
