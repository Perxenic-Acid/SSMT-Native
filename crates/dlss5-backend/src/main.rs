mod api;
mod context;
mod gimi_settings;
mod plugin;
mod protocol;
mod tasks;

mod config {
    pub mod path_manager {
        pub struct PathManager;
        impl PathManager {
            pub fn ssmt_global_config_folder(
            ) -> std::path::PathBuf {
                crate::context::legacy_root()
            }
        }
    }
}
mod commands {
    pub(crate) use crate::gimi_settings;
    pub mod launch_logs {
        #[derive(Debug, Clone, serde::Serialize)]
        pub struct LaunchLogSource {
            pub name: String,
            pub path: std::path::PathBuf,
        }
    }
}
mod utils {
    pub mod independent_process {
        pub fn spawn(
            command: &mut std::process::Command,
        ) -> std::io::Result<std::process::Child> {
            #[cfg(windows)]
            {
                use std::os::windows::process::CommandExt;
                command.creation_flags(
                    0x08000000 | 0x01000000,
                );
                match command.spawn() {
                    Err(error)
                        if error.raw_os_error()
                            == Some(5) =>
                    {
                        command.creation_flags(0x08000000);
                    }
                    result => return result,
                }
            }
            command.spawn()
        }
    }
}
pub(crate) fn asset_lock()->Result<ssmt_plugin_graphics::managed_files::DirectoryTransaction,String>{
    let root = context::asset_root();
    std::fs::create_dir_all(&root)
        .map_err(|error| error.to_string())?;
    ssmt_plugin_graphics::managed_files::DirectoryTransaction::open(&root)
}

#[tokio::main]
async fn main() {
    use serde_json::{json, Value};
    use std::io::{Read, Write};
    let result: Result<Value, String> = async {
        let arguments: Vec<_> =
            std::env::args_os().collect();
        if arguments
            .get(1)
            .is_some_and(|argument| argument == "--cleanup")
        {
            let path = std::path::PathBuf::from(
                arguments.get(2).ok_or("缺少恢复记录")?,
            );
            let bytes = std::fs::read(&path)
                .map_err(|error| error.to_string())?;
            if bytes.len() > 4 * 1024 * 1024 {
                return Err("恢复记录过大".into());
            }
            let record: Value =
                serde_json::from_slice(&bytes)
                    .map_err(|error| error.to_string())?;
            context::initialize(record["context"].clone())?;
            let session = record["session"]
                .as_str()
                .ok_or("缺少恢复会话")?;
            if !session.bytes().all(|byte| {
                byte.is_ascii_hexdigit() || byte == b'-'
            }) {
                return Err("恢复会话无效".into());
            }
            let expected = context::data_root()
                .join("cleanup")
                .join(format!("{session}.json"));
            if path
                .canonicalize()
                .map_err(|error| error.to_string())?
                != expected
                    .canonicalize()
                    .map_err(|error| error.to_string())?
            {
                return Err("恢复文件不属于插件".into());
            }
            plugin::managed_stack::cleanup_helper(
                std::path::Path::new(
                    record["gameExecutable"]
                        .as_str()
                        .ok_or("缺少恢复目标")?,
                ),
                record["parentPid"]
                    .as_u64()
                    .ok_or("缺少恢复进程")?
                    as u32,
                session,
            )?;
            return Ok(Value::Null);
        }
        let task = arguments
            .get(1)
            .is_some_and(|argument| argument == "--task");
        let mut request: Value = if task {
            tasks::load_request(std::path::Path::new(
                arguments
                    .get(2)
                    .ok_or("缺少任务记录路径")?,
            ))?
        } else {
            let mut input = Vec::new();
            std::io::stdin()
                .take(4 * 1024 * 1024 + 1)
                .read_to_end(&mut input)
                .map_err(|error| error.to_string())?;
            if input.len() > 4 * 1024 * 1024 {
                return Err("后台请求过大".into());
            }
            serde_json::from_slice(&input)
                .map_err(|error| error.to_string())?
        };
        if request["protocolVersion"].as_u64() != Some(1) {
            return Err("不支持的后台协议".into());
        }
        if let Some(peers) =
            request["payload"].get("peers").cloned()
        {
            request["context"]["launchPeers"] = peers;
        }
        if let Some(executable) = request["payload"]
            .get("launchContext")
            .unwrap_or(&request["payload"])
            .get("gameExecutable")
            .cloned()
        {
            request["context"]["gameExecutable"] =
                executable;
        }
        context::initialize(request["context"].clone())?;
        if task {
            tasks::worker(request).await?;
            return Ok(Value::Null);
        }
        let method = request["method"]
            .as_str()
            .ok_or("缺少后台方法")?;
        // 控制任务的请求不能争抢工作进程持有的资源锁，否则无法取消下载。
        let _lock = if matches!(
            method,
            "task.read"
                | "task.active"
                | "task.cancel"
                | "cancel"
                | "disabled"
                | "recover"
                | "launch.cancel"
        ) {
            None
        } else {
            Some(asset_lock()?)
        };
        protocol::dispatch(method, &request["payload"])
            .await
    }
    .await;
    let output = match result {
        Ok(value) => json!({"ok":true,"value":value}),
        Err(error) => json!({"ok":false,"error":error}),
    };
    let _ = std::io::stdout()
        .write_all(output.to_string().as_bytes());
}
