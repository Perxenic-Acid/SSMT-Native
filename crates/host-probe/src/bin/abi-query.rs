use std::{ffi::CStr, mem::size_of, path::PathBuf};

use libloading::{Library, Symbol};

use ssmt_plugin_api::{
    SSMT_PLUGIN_ABI_VERSION, SSMT_PLUGIN_API_BASE_SIZE,
    SSMT_PLUGIN_API_PRESENT_SIZE,
    SSMT_PLUGIN_INFO_BASE_SIZE, SSMT_STATUS_OK,
    SSMT_STATUS_STRUCT_TOO_SMALL,
    SSMT_STATUS_UNSUPPORTED_ABI, SsmtPluginApi,
    SsmtPluginInfo, SsmtPluginInitializeFn,
    SsmtPluginQueryFn, SsmtPluginShutdownFn,
    d3d11::{
        SSMT_PLUGIN_API_D3D11_READY_SIZE,
        SsmtPluginOnD3D11ReadyFn,
    },
};

#[repr(C)]
struct BasePluginApi {
    struct_size: u32,
    abi_version: u32,
    initialize: Option<SsmtPluginInitializeFn>,
    shutdown: Option<SsmtPluginShutdownFn>,
}

#[repr(C)]
struct BaseApiWithCanary {
    api: BasePluginApi,
    canary: u64,
}

#[repr(C)]
struct D3D11ReadyPluginApi {
    struct_size: u32,
    abi_version: u32,
    initialize: Option<SsmtPluginInitializeFn>,
    shutdown: Option<SsmtPluginShutdownFn>,
    on_d3d11_ready: Option<SsmtPluginOnD3D11ReadyFn>,
}

#[repr(C)]
struct D3D11ReadyApiWithCanary {
    api: D3D11ReadyPluginApi,
    canary: u64,
}

#[repr(C)]
struct CurrentApiWithCanary {
    api: SsmtPluginApi,
    canary: u64,
}

const CANARY: u64 = 0xA55A_DEAD_CAFE_BEEF;

fn main() -> Result<(), Box<dyn std::error::Error>> {
    let path = std::env::args_os()
        .nth(1)
        .map(PathBuf::from)
        .unwrap_or_else(|| {
            PathBuf::from(
                "target/debug/ssmt_test_plugin.dll",
            )
        });

    println!("Plugin: {}", path.display());

    let library = unsafe { Library::new(&path)? };
    let query: Symbol<SsmtPluginQueryFn> =
        unsafe { library.get(b"SSMTPlugin_Query\0")? };

    test_base_host(*query);
    test_d3d11_ready_host(*query);
    test_present_host(*query);
    test_too_small_api(*query);
    test_too_small_info(*query);
    test_wrong_abi(*query);

    println!();
    println!("All ABI query tests passed.");

    Ok(())
}

fn test_base_host(query: SsmtPluginQueryFn) {
    println!();
    println!("== base API host ==");

    assert_eq!(
        size_of::<BasePluginApi>() as u32,
        SSMT_PLUGIN_API_BASE_SIZE,
    );

    let mut info = SsmtPluginInfo::query_buffer();
    let mut storage = BaseApiWithCanary {
        api: BasePluginApi {
            struct_size: SSMT_PLUGIN_API_BASE_SIZE,
            abi_version: 0,
            initialize: None,
            shutdown: None,
        },
        canary: CANARY,
    };

    let status = unsafe {
        query(
            SSMT_PLUGIN_ABI_VERSION,
            &mut info,
            (&mut storage.api as *mut BasePluginApi)
                .cast::<SsmtPluginApi>(),
        )
    };

    assert_eq!(status, SSMT_STATUS_OK);
    assert_eq!(
        storage.api.abi_version,
        SSMT_PLUGIN_ABI_VERSION
    );
    assert!(storage.api.initialize.is_some());
    assert!(storage.api.shutdown.is_some());
    assert_eq!(storage.canary, CANARY);

    println!("base API size: {}", storage.api.struct_size);
    println!("canary intact: 0x{:016X}", storage.canary);
}

fn test_d3d11_ready_host(query: SsmtPluginQueryFn) {
    println!();
    println!("== D3D11Ready API host ==");

    assert_eq!(
        size_of::<D3D11ReadyPluginApi>() as u32,
        SSMT_PLUGIN_API_D3D11_READY_SIZE,
    );

    let mut info = SsmtPluginInfo::query_buffer();
    let mut storage = D3D11ReadyApiWithCanary {
        api: D3D11ReadyPluginApi {
            struct_size: SSMT_PLUGIN_API_D3D11_READY_SIZE,
            abi_version: 0,
            initialize: None,
            shutdown: None,
            on_d3d11_ready: None,
        },
        canary: CANARY,
    };

    let status = unsafe {
        query(
            SSMT_PLUGIN_ABI_VERSION,
            &mut info,
            (&mut storage.api as *mut D3D11ReadyPluginApi)
                .cast::<SsmtPluginApi>(),
        )
    };

    assert_eq!(status, SSMT_STATUS_OK);
    assert_eq!(
        storage.api.abi_version,
        SSMT_PLUGIN_ABI_VERSION
    );
    assert!(storage.api.initialize.is_some());
    assert!(storage.api.shutdown.is_some());
    assert!(storage.api.on_d3d11_ready.is_some());
    assert_eq!(storage.canary, CANARY);

    println!(
        "D3D11Ready API size: {}",
        storage.api.struct_size
    );
    println!("canary intact: 0x{:016X}", storage.canary);
}

fn test_present_host(query: SsmtPluginQueryFn) {
    println!();
    println!("== Present API host ==");

    assert_eq!(
        size_of::<SsmtPluginApi>() as u32,
        SSMT_PLUGIN_API_PRESENT_SIZE,
    );

    let mut info = SsmtPluginInfo::query_buffer();
    let mut storage = CurrentApiWithCanary {
        api: SsmtPluginApi::query_buffer(),
        canary: CANARY,
    };
    let api_capacity = storage.api.struct_size;

    let status = unsafe {
        query(
            SSMT_PLUGIN_ABI_VERSION,
            &mut info,
            &mut storage.api,
        )
    };

    assert_eq!(status, SSMT_STATUS_OK);
    assert_eq!(storage.api.struct_size, api_capacity);
    assert_eq!(
        storage.api.abi_version,
        SSMT_PLUGIN_ABI_VERSION
    );
    assert!(storage.api.initialize.is_some());
    assert!(storage.api.shutdown.is_some());
    assert!(storage.api.on_d3d11_ready.is_some());
    assert!(storage.api.on_present.is_some());
    assert_eq!(storage.canary, CANARY);

    assert!(!info.name.is_null());
    assert!(!info.version.is_null());
    assert!(!info.author.is_null());

    let name = unsafe { CStr::from_ptr(info.name) };
    let version = unsafe { CStr::from_ptr(info.version) };
    let author = unsafe { CStr::from_ptr(info.author) };

    println!(
        "metadata: {} {} by {}",
        name.to_string_lossy(),
        version.to_string_lossy(),
        author.to_string_lossy(),
    );
    println!(
        "Present API size: {}",
        storage.api.struct_size
    );
    println!("canary intact: 0x{:016X}", storage.canary);
}

fn test_too_small_api(query: SsmtPluginQueryFn) {
    println!();
    println!("== too-small API ==");

    let mut info = SsmtPluginInfo::query_buffer();
    let mut api = SsmtPluginApi::query_buffer();
    api.struct_size = SSMT_PLUGIN_API_BASE_SIZE - 1;

    let status = unsafe {
        query(SSMT_PLUGIN_ABI_VERSION, &mut info, &mut api)
    };

    assert_eq!(status, SSMT_STATUS_STRUCT_TOO_SMALL);
    println!("correctly rejected: {status}");
}

fn test_too_small_info(query: SsmtPluginQueryFn) {
    println!();
    println!("== too-small info ==");

    let mut info = SsmtPluginInfo::query_buffer();
    let mut api = SsmtPluginApi::query_buffer();
    info.struct_size = SSMT_PLUGIN_INFO_BASE_SIZE - 1;

    let status = unsafe {
        query(SSMT_PLUGIN_ABI_VERSION, &mut info, &mut api)
    };

    assert_eq!(status, SSMT_STATUS_STRUCT_TOO_SMALL);
    println!("correctly rejected: {status}");
}

fn test_wrong_abi(query: SsmtPluginQueryFn) {
    println!();
    println!("== wrong ABI ==");

    let mut info = SsmtPluginInfo::query_buffer();
    let mut api = SsmtPluginApi::query_buffer();

    let status = unsafe {
        query(
            SSMT_PLUGIN_ABI_VERSION + 1,
            &mut info,
            &mut api,
        )
    };

    assert_eq!(status, SSMT_STATUS_UNSUPPORTED_ABI);
    println!("correctly rejected: {status}");
}
