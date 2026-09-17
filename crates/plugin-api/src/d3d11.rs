use crate::SsmtPluginApi;

use super::SsmtStatus;

use super::SSMT_PLUGIN_ABI_VERSION;

use core::mem::size_of;

use core::ffi::c_void;

#[repr(C)]
pub struct SsmtD3D11Context {
    pub struct_size: u32,
    pub abi_version: u32,

    // ID3D11Device*
    pub device: *mut c_void,
    // ID3D11DeviceContext*
    pub immediate_context: *mut c_void,
    // IDXGISwapChain*
    pub swap_chain: *mut c_void,
}

impl SsmtD3D11Context {
    pub const fn new(
        device: *mut c_void,
        immediate_context: *mut c_void,
        swap_chain: *mut c_void,
    ) -> Self {
        Self {
            struct_size: size_of::<Self>() as u32,
            abi_version: SSMT_PLUGIN_ABI_VERSION,
            device,
            immediate_context,
            swap_chain,
        }
    }
}

pub type SsmtPluginOnD3D11ReadyFn =
    unsafe extern "C" fn(
        context: *const SsmtD3D11Context,
    ) -> SsmtStatus;

pub const SSMT_PLUGIN_API_D3D11_READY_SIZE: u32 =
    (core::mem::offset_of!(SsmtPluginApi, on_d3d11_ready)
        + size_of::<Option<SsmtPluginOnD3D11ReadyFn>>())
        as u32;
