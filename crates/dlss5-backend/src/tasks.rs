//! 下载任务归插件进程所有；宿主只启动有界的 JSON 请求，不解释业务或轮询文件。
use crate::{context, protocol};
use serde_json::{json, Value};
use std::{
    fs,
    io::Write,
    path::PathBuf,
    process::{Command, Stdio},
    time::{Duration, SystemTime, UNIX_EPOCH},
};

fn tasks_root() -> PathBuf {
    context::data_root().join("tasks")
}
fn directory(id: &str) -> Result<PathBuf, String> {
    if id.len() < 8
        || id.len() > 80
        || !id.bytes().all(|byte| {
            byte.is_ascii_hexdigit() || byte == b'-'
        })
    {
        return Err("任务 ID 无效".into());
    }
    Ok(tasks_root().join(id))
}
fn atomic_write(
    path: &std::path::Path,
    value: &Value,
) -> Result<(), String> {
    let temporary = path.with_extension(format!(
        "{}-{}.tmp",
        std::process::id(),
        SystemTime::now()
            .duration_since(UNIX_EPOCH)
            .unwrap()
            .as_nanos()
    ));
    let mut file = fs::OpenOptions::new()
        .write(true)
        .create_new(true)
        .open(&temporary)
        .map_err(|error| error.to_string())?;
    file.write_all(
        &serde_json::to_vec(value)
            .map_err(|error| error.to_string())?,
    )
    .and_then(|_| file.sync_all())
    .map_err(|error| error.to_string())?;
    drop(file);
    fs::rename(&temporary, path)
        .map_err(|error| error.to_string())
}
fn load(path: &std::path::Path) -> Result<Value, String> {
    let bytes = fs::read(path)
        .map_err(|error| error.to_string())?;
    if bytes.len() > 4 * 1024 * 1024 {
        return Err("任务记录过大".into());
    }
    serde_json::from_slice(&bytes)
        .map_err(|error| error.to_string())
}
pub fn start(payload: Value) -> Result<Value, String> {
    let operation = payload["operation"]
        .as_str()
        .ok_or("缺少任务操作")?;
    if !matches!(
        operation,
        "runtime.download"
            | "runtime.import"
            | "runtime.select"
            | "runtime.remove"
            | "route.install"
            | "route.restore"
    ) {
        return Err("不支持的任务操作".into());
    }
    fs::create_dir_all(tasks_root())
        .map_err(|error| error.to_string())?;
    let id = format!(
        "{:x}-{:x}",
        std::process::id(),
        SystemTime::now()
            .duration_since(UNIX_EPOCH)
            .unwrap()
            .as_nanos()
    );
    let root = directory(&id)?;
    fs::create_dir(&root)
        .map_err(|error| error.to_string())?;
    let request = json!({"protocolVersion":1,"context":context::value(),"payload":payload,"taskId":id});
    atomic_write(&root.join("request.json"), &request)?;
    let record = json!({"taskId":id,"gameName":context::value()["gameName"],"operation":operation,"status":"queued","createdAtMs":now(),"cancellable":operation=="runtime.download","progress":null,"result":null,"error":null});
    atomic_write(&root.join("state.json"), &record)?;
    let mut command = Command::new(
        std::env::current_exe()
            .map_err(|error| error.to_string())?,
    );
    command
        .arg("--task")
        .arg(root.join("request.json"))
        .stdin(Stdio::null())
        .stdout(Stdio::null())
        .stderr(Stdio::null());
    #[cfg(windows)]
    {
        use std::os::windows::process::CommandExt;
        command.creation_flags(0x08000000);
    }
    if let Err(error) = command.spawn() {
        let mut failed = record;
        failed["status"] = json!("failed");
        failed["error"] = json!(error.to_string());
        atomic_write(&root.join("state.json"), &failed)?;
        return Err(error.to_string());
    }
    Ok(record)
}
pub fn read(id: &str) -> Result<Value, String> {
    recover()?;
    let record = load(&directory(id)?.join("state.json"))?;
    if record["gameName"] != context::value()["gameName"] {
        return Err("任务不属于当前游戏".into());
    }
    Ok(record)
}
pub fn cancel(id: &str) -> Result<Value, String> {
    let record = read(id)?;
    if !matches!(
        record["status"].as_str(),
        Some("queued" | "running")
    ) {
        return Ok(record);
    }
    if record["cancellable"].as_bool() != Some(true) {
        return Err("当前任务不能安全中断".into());
    }
    fs::write(directory(id)?.join("cancel"), b"cancel")
        .map_err(|error| error.to_string())?;
    Ok(record)
}
pub fn active() -> Result<Value, String> {
    recover()?;
    let mut result = Vec::new();
    if tasks_root().exists() {
        for entry in fs::read_dir(tasks_root())
            .map_err(|error| error.to_string())?
        {
            let entry =
                entry.map_err(|error| error.to_string())?;
            if !entry
                .file_type()
                .map_err(|error| error.to_string())?
                .is_dir()
            {
                continue;
            }
            let record = load(
                &directory(
                    &entry.file_name().to_string_lossy(),
                )?
                .join("state.json"),
            )?;
            if record["gameName"]
                == context::value()["gameName"]
                && matches!(
                    record["status"].as_str(),
                    Some("queued" | "running")
                )
            {
                result.push(record);
            }
        }
    }
    Ok(Value::Array(result))
}
pub async fn stop_all() -> Result<(), String> {
    if !tasks_root().exists() {
        return Ok(());
    }
    recover()?;
    for entry in fs::read_dir(tasks_root())
        .map_err(|error| error.to_string())?
    {
        let entry =
            entry.map_err(|error| error.to_string())?;
        let id = entry
            .file_name()
            .to_string_lossy()
            .into_owned();
        let record =
            load(&directory(&id)?.join("state.json"))?;
        if record["gameName"]
            != context::value()["gameName"]
        {
            continue;
        }
        if matches!(
            record["status"].as_str(),
            Some("queued" | "running")
        ) {
            cancel(&id)?;
            for _ in 0..100 {
                if !matches!(
                    read(&id)?["status"].as_str(),
                    Some("queued" | "running")
                ) {
                    break;
                }
                tokio::time::sleep(Duration::from_millis(
                    100,
                ))
                .await;
            }
            if matches!(
                read(&id)?["status"].as_str(),
                Some("queued" | "running")
            ) {
                return Err(
                    "组件任务仍在退出，请稍后重试".into()
                );
            }
        }
    }
    Ok(())
}
fn now() -> u64 {
    SystemTime::now()
        .duration_since(UNIX_EPOCH)
        .unwrap_or_default()
        .as_millis() as u64
}
pub fn recover() -> Result<(), String> {
    if !tasks_root().exists() {
        return Ok(());
    }
    let mut system = sysinfo::System::new();
    system.refresh_processes_specifics(
        sysinfo::ProcessRefreshKind::new()
            .with_exe(sysinfo::UpdateKind::Always),
    );
    let own = std::env::current_exe()
        .and_then(|path| path.canonicalize())
        .map_err(|error| error.to_string())?;
    for entry in fs::read_dir(tasks_root())
        .map_err(|error| error.to_string())?
    {
        let entry =
            entry.map_err(|error| error.to_string())?;
        if !entry
            .file_type()
            .map_err(|error| error.to_string())?
            .is_dir()
        {
            continue;
        }
        let id = entry
            .file_name()
            .to_string_lossy()
            .into_owned();
        let path = directory(&id)?.join("state.json");
        let mut record = load(&path)?;
        if !matches!(
            record["status"].as_str(),
            Some("queued" | "running")
        ) {
            continue;
        }
        let active = record["workerPid"]
            .as_u64()
            .and_then(|pid| {
                system.process(sysinfo::Pid::from_u32(
                    pid as u32,
                ))
            })
            .and_then(|process| process.exe())
            .and_then(|path| path.canonicalize().ok())
            .is_some_and(|path| path == own);
        if !active
            && now().saturating_sub(
                record["createdAtMs"].as_u64().unwrap_or(0),
            ) > 30_000
        {
            record["status"] = json!("failed");
            record["error"] = json!(
                "任务进程已退出；已保留文件供下次事务恢复"
            );
            atomic_write(&path, &record)?;
        }
    }
    Ok(())
}

#[cfg(windows)]
struct HostProcess(
    Option<std::os::windows::io::OwnedHandle>,
);
#[cfg(not(windows))]
struct HostProcess;
impl HostProcess {
    fn open() -> Result<Self, String> {
        #[cfg(windows)]
        {
            use std::os::windows::io::FromRawHandle;
            let Some(pid) =
                context::value()["hostProcessId"].as_u64()
            else {
                return Ok(Self(None));
            };
            let handle = unsafe {
                windows_sys::Win32::System::Threading::OpenProcess(0x00100000,0,pid as u32)
            };
            if handle.is_null() {
                return Err("SSMT 进程已退出".into());
            }
            Ok(Self(Some(unsafe {
                std::os::windows::io::OwnedHandle::from_raw_handle(handle as _)
            })))
        }
        #[cfg(not(windows))]
        {
            Ok(Self)
        }
    }
    fn alive(&self) -> bool {
        #[cfg(windows)]
        {
            use std::os::windows::io::AsRawHandle;
            return self.0.as_ref().is_none_or(|handle|unsafe {windows_sys::Win32::System::Threading::WaitForSingleObject(handle.as_raw_handle() as _,0)}==258);
        }
        #[cfg(not(windows))]
        {
            true
        }
    }
}

pub async fn worker(request: Value) -> Result<(), String> {
    let id =
        request["taskId"].as_str().ok_or("缺少任务 ID")?;
    let root = directory(id)?;
    let mut record = read(id)?;
    record["status"] = json!("running");
    record["workerPid"] = json!(std::process::id());
    atomic_write(&root.join("state.json"), &record)?;
    let state =
        std::sync::Arc::new(std::sync::Mutex::new(record));
    let progress_state = state.clone();
    let progress_path = root.join("state.json");
    let result = {
        let operation = async {
            let _lock = crate::asset_lock()?;
            let host = HostProcess::open()?;
            let operation = protocol::operation(
                &request["payload"],
                move |progress| {
                    if let Ok(mut record) =
                        progress_state.lock()
                    {
                        record["progress"] =
                            serde_json::to_value(progress)
                                .unwrap_or(Value::Null);
                        let _ = atomic_write(
                            &progress_path,
                            &record,
                        );
                    }
                },
            );
            tokio::pin!(operation);
            tokio::select! {
                result=&mut operation=>result,
                _=async {loop {if !host.alive() {break;}tokio::time::sleep(Duration::from_millis(100)).await;}}=>Err("SSMT 进程已退出".into())
            }
        };
        tokio::pin!(operation);
        if request["payload"]["operation"].as_str()
            == Some("runtime.download")
        {
            tokio::select! {
                result=&mut operation=>result,
                _=async {loop {if root.join("cancel").exists() {break;}tokio::time::sleep(Duration::from_millis(100)).await;}}=>Err("任务已取消".into()),
                _=tokio::time::sleep(Duration::from_secs(30*60))=>Err("组件任务超时".into())
            }
        } else {
            operation.await
        }
    };
    let mut record =
        state.lock().map_err(|_| "任务记录锁损坏")?;
    match result {
        Ok(value) => {
            record["status"] = json!("succeeded");
            record["result"] = value;
        }
        Err(error) => {
            record["status"] =
                json!(if root.join("cancel").exists() {
                    "cancelled"
                } else {
                    "failed"
                });
            record["error"] = json!(error);
        }
    }
    atomic_write(&root.join("state.json"), &record)
}

pub fn load_request(
    path: &std::path::Path,
) -> Result<Value, String> {
    let request = load(path)?;
    let id =
        request["taskId"].as_str().ok_or("缺少任务 ID")?;
    if id.len() < 8
        || id.len() > 80
        || !id.bytes().all(|byte| {
            byte.is_ascii_hexdigit() || byte == b'-'
        })
    {
        return Err("任务 ID 无效".into());
    }
    let root = request["context"]["dataRoot"]
        .as_str()
        .ok_or("缺少任务数据目录")?;
    let expected = PathBuf::from(root)
        .join("tasks")
        .join(id)
        .join("request.json");
    if path
        .canonicalize()
        .map_err(|error| error.to_string())?
        != expected
            .canonicalize()
            .map_err(|error| error.to_string())?
    {
        return Err("任务文件不属于声明的数据目录".into());
    }
    Ok(request)
}
