use serde::{Deserialize, Serialize};
use serde_json::Value;
use std::{
    collections::BTreeMap,
    fs,
    io::Write,
    path::{Path, PathBuf},
};

const VALUE_NAME: &str =
    "GraphicsSettings_Model_h2986158309";

#[derive(
    Debug,
    Clone,
    Copy,
    PartialEq,
    Eq,
    PartialOrd,
    Ord,
    Serialize,
    Deserialize,
)]
#[serde(rename_all = "lowercase")]
enum Client {
    Cn,
    Global,
}

impl Client {
    fn key(self) -> &'static str {
        match self {
            Self::Cn => "Software\\miHoYo\\崩坏：星穹铁道",
            Self::Global => {
                "Software\\Cognosphere\\Star Rail"
            }
        }
    }
}

#[derive(Debug, Default, Clone, Serialize, Deserialize)]
#[serde(transparent)]
struct Journal(BTreeMap<Client, Lease>);

#[derive(Debug, Clone, Serialize, Deserialize)]
#[serde(rename_all = "camelCase", deny_unknown_fields)]
struct Lease {
    original_fps: u32,
    owners: BTreeMap<String, PathBuf>,
}

fn client_from_app_info(
    info: &str,
) -> Result<Client, String> {
    let mut lines = info
        .trim_start_matches('\u{feff}')
        .lines()
        .map(str::trim);
    let company = lines.next().unwrap_or_default();
    let product = lines.next().unwrap_or_default();
    match (company, product) {
        ("miHoYo", "崩坏：星穹铁道") => Ok(Client::Cn),
        ("Cognosphere", "Star Rail") => Ok(Client::Global),
        _ => {
            Err("无法确认崩铁客户端地区，请检查 StarRail_Data/app.info；不会猜测注册表位置".into())
        }
    }
}

fn parse(data: &[u8]) -> Result<(Value, u32), String> {
    if data.is_empty() || data.len() > 4 * 1024 * 1024 {
        return Err("崩铁画质设置大小无效".into());
    }
    let end = data
        .iter()
        .position(|&byte| byte == 0)
        .unwrap_or(data.len());
    if data[end..].iter().any(|&byte| byte != 0) {
        return Err("崩铁画质设置包含未知尾部数据".into());
    }
    let root: Value = serde_json::from_slice(&data[..end])
        .map_err(|error| {
            format!("崩铁画质设置格式无效：{error}")
        })?;
    let fps = root
        .get("FPS")
        .and_then(Value::as_u64)
        .filter(|fps| [30, 60, 120].contains(fps))
        .ok_or("崩铁画质设置没有已知格式的 FPS 字段，请先进入游戏保存一次画质设置")?;
    Ok((root, fps as u32))
}

fn update_fps(
    data: &[u8],
    fps: u32,
) -> Result<Vec<u8>, String> {
    if ![30, 60, 120].contains(&fps) {
        return Err("崩铁 FPS 设置值无效".into());
    }
    let (mut root, _) = parse(data)?;
    root["FPS"] = Value::from(fps);
    let mut bytes = serde_json::to_vec(&root)
        .map_err(|error| error.to_string())?;
    bytes.push(0);
    Ok(bytes)
}

fn plan_enable(
    journal: &mut Journal,
    game: &str,
    executable: &Path,
    client: Client,
    data: &[u8],
) -> Result<Option<Vec<u8>>, String> {
    let (_, fps) = parse(data)?;
    // 已经由玩家设置为 120 的值不认领，关闭插件时也不将它改回 60。
    if fps == 120 && !journal.0.contains_key(&client) {
        return Ok(None);
    }
    let lease =
        journal.0.entry(client).or_insert_with(|| Lease {
            original_fps: fps,
            owners: BTreeMap::new(),
        });
    if fps != 120 {
        lease.original_fps = fps;
    }
    lease
        .owners
        .insert(game.to_owned(), executable.to_owned());
    if fps == 120 {
        Ok(None)
    } else {
        update_fps(data, 120).map(Some)
    }
}

fn plan_release(
    journal: &mut Journal,
    game: &str,
    client: Client,
    data: &[u8],
) -> Result<Option<Vec<u8>>, String> {
    let Some(lease) = journal.0.get_mut(&client) else {
        return Ok(None);
    };
    if lease.owners.remove(game).is_none()
        || !lease.owners.is_empty()
    {
        return Ok(None);
    }
    let original = lease.original_fps;
    let (_, current) = parse(data)?;
    journal.0.remove(&client);
    // 只恢复仍等于本插件写入值的 FPS，保留玩家后续修改和其余画质字段。
    if current == 120 && original != 120 {
        update_fps(data, original).map(Some)
    } else {
        Ok(None)
    }
}

#[cfg(windows)]
mod registry {
    use super::*;
    use std::ptr;
    use windows_sys::Win32::System::Registry::*;
    struct Key(HKEY);
    impl Drop for Key {
        fn drop(&mut self) {
            unsafe {
                RegCloseKey(self.0);
            }
        }
    }
    fn wide(value: &str) -> Vec<u16> {
        value.encode_utf16().chain(Some(0)).collect()
    }
    fn open(
        client: Client,
        write: bool,
    ) -> Result<Key, String> {
        let mut raw = ptr::null_mut();
        let result = unsafe {
            RegOpenKeyExW(
                HKEY_CURRENT_USER,
                wide(client.key()).as_ptr(),
                0,
                KEY_QUERY_VALUE
                    | if write { KEY_SET_VALUE } else { 0 },
                &mut raw,
            )
        };
        if result != 0 {
            return Err(format!(
                "无法打开崩铁画质设置（{result}），请先进入游戏保存一次设置"
            ));
        }
        Ok(Key(raw))
    }
    fn read_key(key: &Key) -> Result<Vec<u8>, String> {
        let (mut kind, mut size) = (0, 0);
        let name = wide(VALUE_NAME);
        let result = unsafe {
            RegQueryValueExW(
                key.0,
                name.as_ptr(),
                ptr::null(),
                &mut kind,
                ptr::null_mut(),
                &mut size,
            )
        };
        if result != 0
            || kind != REG_BINARY
            || size == 0
            || size > 4 * 1024 * 1024
        {
            return Err(format!("崩铁画质设置不是有效 REG_BINARY（{result}）"));
        }
        let mut bytes = vec![0; size as usize];
        let result = unsafe {
            RegQueryValueExW(
                key.0,
                name.as_ptr(),
                ptr::null(),
                &mut kind,
                bytes.as_mut_ptr(),
                &mut size,
            )
        };
        if result != 0 || kind != REG_BINARY {
            return Err(format!(
                "读取崩铁画质设置失败（{result}）"
            ));
        }
        bytes.truncate(size as usize);
        Ok(bytes)
    }
    pub(super) fn read(
        client: Client,
    ) -> Result<Vec<u8>, String> {
        read_key(&open(client, false)?)
    }
    pub(super) fn write(
        client: Client,
        original: &[u8],
        updated: &[u8],
    ) -> Result<(), String> {
        let key = open(client, true)?;
        if read_key(&key)? != original {
            return Err(
                "崩铁画质设置已被其他程序修改，本次不覆盖"
                    .into(),
            );
        }
        let result = unsafe {
            RegSetValueExW(
                key.0,
                wide(VALUE_NAME).as_ptr(),
                0,
                REG_BINARY,
                updated.as_ptr(),
                updated.len() as u32,
            )
        };
        if result != 0 {
            return Err(format!(
                "写入崩铁 FPS 失败（{result}）"
            ));
        }
        Ok(())
    }
}

#[cfg(not(windows))]
mod registry {
    use super::*;
    pub(super) fn read(
        _: Client,
    ) -> Result<Vec<u8>, String> {
        Err("崩铁设置仅支持 Windows".into())
    }
    pub(super) fn write(
        _: Client,
        _: &[u8],
        _: &[u8],
    ) -> Result<(), String> {
        Err("崩铁设置仅支持 Windows".into())
    }
}

pub fn ensure_stopped(
    executable: &Path,
) -> Result<(), String> {
    let canonical = executable
        .canonicalize()
        .map_err(|error| error.to_string())?;
    let mut system = sysinfo::System::new();
    system.refresh_processes_specifics(
        sysinfo::ProcessRefreshKind::new()
            .with_exe(sysinfo::UpdateKind::Always),
    );
    if system.processes().values().any(|process| {
        process
            .exe()
            .and_then(|path| path.canonicalize().ok())
            .as_ref()
            == Some(&canonical)
    }) {
        return Err("请先关闭游戏".into());
    }
    Ok(())
}

fn load_journal(data: &Path) -> Result<Journal, String> {
    match fs::read(data.join("ownership.json")) {
        Ok(bytes) => serde_json::from_slice(&bytes)
            .map_err(|error| {
                format!("FPS 恢复记录无效：{error}")
            }),
        Err(error)
            if error.kind()
                == std::io::ErrorKind::NotFound =>
        {
            Ok(Journal::default())
        }
        Err(error) => Err(error.to_string()),
    }
}

fn save_journal(
    data: &Path,
    journal: &Journal,
) -> Result<(), String> {
    let temporary = data.join(format!(
        "ownership-{}.tmp",
        std::process::id()
    ));
    let mut file = fs::OpenOptions::new()
        .create_new(true)
        .write(true)
        .open(&temporary)
        .map_err(|error| error.to_string())?;
    let result = (|| {
        file.write_all(
            &serde_json::to_vec(journal)
                .map_err(|error| error.to_string())?,
        )
        .map_err(|error| error.to_string())?;
        file.sync_all()
            .map_err(|error| error.to_string())?;
        drop(file);
        fs::rename(&temporary, data.join("ownership.json"))
            .map_err(|error| error.to_string())
    })();
    if result.is_err() {
        let _ = fs::remove_file(temporary);
    }
    result
}

pub fn execute(
    context: &Value,
    method: &str,
) -> Result<Value, String> {
    let game = context
        .get("gameName")
        .and_then(Value::as_str)
        .ok_or("缺少游戏名称")?;
    let data = PathBuf::from(
        context
            .get("dataRoot")
            .and_then(Value::as_str)
            .ok_or("缺少插件数据目录")?,
    );
    fs::create_dir_all(&data)
        .map_err(|error| error.to_string())?;
    let mut options = fs::OpenOptions::new();
    options.create(true).truncate(false).write(true);
    #[cfg(windows)]
    {
        use std::os::windows::fs::OpenOptionsExt;
        options.share_mode(0);
    }
    // 跨后台进程持有文件句柄，退出或崩溃后由系统释放，避免遗留锁文件阻断恢复。
    let _lock = options
        .open(data.join("ownership.lock"))
        .map_err(|error| {
        format!("其他设置事务正在执行：{error}")
    })?;
    if !data.join("ownership.json").exists() {
        if let Some(legacy) = context
            .pointer("/legacySettings/starRailFpsOwnership")
        {
            let journal: Journal =
                serde_json::from_value(legacy.clone())
                    .map_err(|error| {
                        format!(
                            "旧 FPS 恢复记录无效：{error}"
                        )
                    })?;
            save_journal(&data, &journal)?;
        }
    }
    let mut journal = load_journal(&data)?;
    if matches!(method, "disabled" | "cancel" | "recover") {
        for client in
            journal.0.keys().copied().collect::<Vec<_>>()
        {
            let Some(executable) =
                journal.0[&client].owners.get(game)
            else {
                continue;
            };
            ensure_stopped(executable)?;
            let original = registry::read(client)?;
            let mut updated_journal = journal.clone();
            if let Some(updated) = plan_release(
                &mut updated_journal,
                game,
                client,
                &original,
            )? {
                registry::write(
                    client, &original, &updated,
                )?;
            }
            save_journal(&data, &updated_journal)?;
            journal = updated_journal;
        }
        return Ok(Value::Null);
    }
    if method != "prepare" {
        return Err("未知后台方法".into());
    }
    if context.get("gamePreset").and_then(Value::as_str)
        != Some("SRMI")
    {
        return Ok(Value::Null);
    }
    let settings = super::settings::read(context)?;
    if settings
        .pointer("/gameplay/fpsUnlock")
        .and_then(Value::as_bool)
        != Some(true)
    {
        drop(_lock);
        return execute(context, "disabled");
    }
    let executable = PathBuf::from(
        context
            .get("gameExecutable")
            .and_then(Value::as_str)
            .ok_or("缺少游戏程序")?,
    );
    if !executable.file_name().is_some_and(|name| {
        name.to_string_lossy()
            .eq_ignore_ascii_case("StarRail.exe")
    }) {
        return Err("只支持 StarRail.exe".into());
    }
    ensure_stopped(&executable)?;
    let info = executable
        .parent()
        .ok_or("无效游戏目录")?
        .join("StarRail_Data/app.info");
    if fs::metadata(&info)
        .map_err(|error| error.to_string())?
        .len()
        > 4096
    {
        return Err("客户端信息过大".into());
    }
    let client = client_from_app_info(
        &fs::read_to_string(info)
            .map_err(|error| error.to_string())?,
    )?;
    let original = registry::read(client)?;
    if let Some(updated) = plan_enable(
        &mut journal,
        game,
        &executable,
        client,
        &original,
    )? {
        let backup = data.join(format!(
            "graphics-{}-{}.bin",
            std::process::id(),
            std::time::SystemTime::now()
                .duration_since(std::time::UNIX_EPOCH)
                .unwrap_or_default()
                .as_nanos()
        ));
        let mut file = fs::OpenOptions::new()
            .write(true)
            .create_new(true)
            .open(backup)
            .map_err(|error| error.to_string())?;
        file.write_all(&original)
            .and_then(|_| file.sync_all())
            .map_err(|error| error.to_string())?;
        save_journal(&data, &journal)?;
        registry::write(client, &original, &updated)?;
    } else {
        save_journal(&data, &journal)?;
    }
    Ok(Value::Null)
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    #[ignore = "设置 SSMT_STAR_RAIL_APP_INFO 后只读检查真实客户端与注册表，FPS 改动仅在内存中回归"]
    fn reads_real_settings_and_checks_roundtrip_without_registry_writes(
    ) {
        let path = PathBuf::from(
            std::env::var_os("SSMT_STAR_RAIL_APP_INFO")
                .expect("app.info path"),
        );
        let client = client_from_app_info(
            &fs::read_to_string(path).unwrap(),
        )
        .unwrap();
        let data = registry::read(client).unwrap();
        let (before, fps) = parse(&data).unwrap();
        let mut journal = Journal::default();
        let updated = plan_enable(
            &mut journal,
            "只读验证",
            Path::new("StarRail.exe"),
            client,
            &data,
        )
        .unwrap();
        if let Some(updated) = updated {
            assert_eq!(parse(&updated).unwrap().1, 120);
            let restored = plan_release(
                &mut journal,
                "只读验证",
                client,
                &updated,
            )
            .unwrap()
            .unwrap();
            assert_eq!(
                parse(&restored).unwrap(),
                (before, fps)
            );
        }
        assert_eq!(registry::read(client).unwrap(), data);
    }

    #[test]
    fn selects_client_from_game_metadata_without_guessing_existing_keys(
    ) {
        assert_eq!(
            client_from_app_info(
                "miHoYo\r\n崩坏：星穹铁道\r\n"
            )
            .unwrap(),
            Client::Cn
        );
        assert_eq!(
            client_from_app_info("Cognosphere\nStar Rail")
                .unwrap(),
            Client::Global
        );
        assert!(
            client_from_app_info("miHoYo\n原神").is_err()
        );
        assert!(client_from_app_info("unknown\nStar Rail")
            .is_err());
    }

    #[test]
    fn fps_update_and_restore_preserve_other_graphics_and_unicode_fields(
    ) {
        let data = "{\"FPS\":60,\"ShadowQuality\":4,\"备注\":\"保留\"}\0".as_bytes();
        let mut journal = Journal::default();
        let updated = plan_enable(
            &mut journal,
            "测试崩铁",
            Path::new("D:/StarRail.exe"),
            Client::Cn,
            data,
        )
        .unwrap()
        .unwrap();
        let (mut current, fps) = parse(&updated).unwrap();
        assert_eq!(fps, 120);
        assert_eq!(current["备注"], "保留");
        current["ShadowQuality"] = Value::from(2);
        let current = serde_json::to_vec(&current).unwrap();
        let restored = plan_release(
            &mut journal,
            "测试崩铁",
            Client::Cn,
            &current,
        )
        .unwrap()
        .unwrap();
        let (restored, fps) = parse(&restored).unwrap();
        assert_eq!(fps, 60);
        assert_eq!(restored["ShadowQuality"], 2);
        assert!(journal.0.is_empty());
    }

    #[test]
    fn preexisting_120_and_later_user_edits_are_never_reverted(
    ) {
        let mut journal = Journal::default();
        assert!(plan_enable(
            &mut journal,
            "A",
            Path::new("StarRail.exe"),
            Client::Global,
            b"{\"FPS\":120}\0"
        )
        .unwrap()
        .is_none());
        assert!(journal.0.is_empty());
        plan_enable(
            &mut journal,
            "A",
            Path::new("StarRail.exe"),
            Client::Global,
            b"{\"FPS\":60}",
        )
        .unwrap();
        assert!(plan_release(
            &mut journal,
            "A",
            Client::Global,
            b"{\"FPS\":30}"
        )
        .unwrap()
        .is_none());
        assert!(journal.0.is_empty());
    }

    #[test]
    fn shared_client_restores_only_after_its_last_game_instance_disables(
    ) {
        let mut journal = Journal::default();
        let updated = plan_enable(
            &mut journal,
            "A",
            Path::new("StarRail.exe"),
            Client::Cn,
            b"{\"FPS\":30}",
        )
        .unwrap()
        .unwrap();
        plan_enable(
            &mut journal,
            "B",
            Path::new("StarRail.exe"),
            Client::Cn,
            &updated,
        )
        .unwrap();
        assert!(plan_release(
            &mut journal,
            "A",
            Client::Cn,
            &updated
        )
        .unwrap()
        .is_none());
        let restored = plan_release(
            &mut journal,
            "B",
            Client::Cn,
            &updated,
        )
        .unwrap()
        .unwrap();
        assert_eq!(parse(&restored).unwrap().1, 30);
        for data in [
            b"{}".as_slice(),
            b"{\"FPS\":240}",
            b"{\"FPS\":\"60\"}",
            b"{\"FPS\":60}\0unexpected",
        ] {
            assert!(parse(data).is_err());
        }
    }
}
