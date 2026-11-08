use serde_json::{json, Value};

// 原神会同时保存运行时画质和性能预设；只修改其中一份会在下次启动时被覆盖。
// 与 XXMI GIMI 一致，关闭动态角色分辨率，避免 Mod 使用不同 LOD 的骨骼/顶点缓冲。
#[cfg(test)]
fn disable_dynamic_character_resolution(
    data: &[u8],
) -> Result<Option<Vec<u8>>, String> {
    update_graphics_settings(data, true, None)
}

fn update_graphics_settings(
    data: &[u8],
    disable_dynamic: bool,
    fsr2: Option<bool>,
) -> Result<Option<Vec<u8>>, String> {
    let end = data
        .iter()
        .position(|&byte| byte == 0)
        .unwrap_or(data.len());
    let mut root: Value = serde_json::from_slice(
        &data[..end],
    )
    .map_err(|error| {
        format!("原神画质设置格式无法读取：{error}")
    })?;
    let mut changed = false;
    for (field, list, key, value_key, disabled) in [
        (
            "graphicsData",
            "customVolatileGrades",
            "key",
            "value",
            1,
        ),
        (
            "globalPerfData",
            "saveItems",
            "entryType",
            "index",
            0,
        ),
    ] {
        let raw = root
            .get(field)
            .and_then(Value::as_str)
            .ok_or_else(|| {
            format!("原神画质设置缺少 {field}")
        })?;
        let mut settings: Value = serde_json::from_str(raw)
            .map_err(|error| {
                format!(
                    "原神 {field} 格式无法读取：{error}"
                )
            })?;
        let entries = settings
            .get_mut(list)
            .and_then(Value::as_array_mut)
            .ok_or_else(|| {
                format!("原神 {field} 缺少 {list}")
            })?;
        let mut found = false;
        let mut field_changed = false;
        for entry in entries.iter_mut().filter(|entry| {
            disable_dynamic
                && entry.get(key) == Some(&json!(21))
        }) {
            found = true;
            let current = entry
                .get(value_key)
                .and_then(Value::as_u64)
                .ok_or_else(|| format!("原神动态角色分辨率 {value_key} 格式未知"))?;
            if current != disabled {
                if current
                    != if disabled == 1 { 2 } else { 1 }
                {
                    return Err("原神动态角色分辨率取值未知，未修改游戏设置".to_string());
                }
                entry[value_key] = json!(disabled);
                field_changed = true;
            }
        }
        if disable_dynamic && !found {
            let mut entry =
                json!({key: 21, value_key: disabled});
            if field == "globalPerfData" {
                entry["itemVersion"] =
                    json!("OSRELWin5.0.0");
            }
            entries.push(entry);
            field_changed = true;
        }
        if let Some(require_fsr2) = fsr2 {
            // Collapse 的原神设置模型：AA 为 entryType=8，Off/FSR2/SMAA 的 index 为 0/1/2，
            // graphicsData 的 value 比 index 大一。Feeder 自行重建，统一关闭游戏抗锯齿。
            let offset =
                if field == "graphicsData" { 1 } else { 0 };
            let mut found = false;
            for entry in
                entries.iter_mut().filter(|entry| {
                    entry.get(key) == Some(&json!(8))
                })
            {
                found = true;
                let current = entry
                    .get(value_key)
                    .and_then(Value::as_u64)
                    .ok_or("原神抗锯齿设置格式未知")?;
                if !(offset..=offset + 2).contains(&current)
                {
                    return Err("原神抗锯齿取值未知，未修改游戏设置".into());
                }
                let desired =
                    offset + u64::from(require_fsr2);
                if desired != current {
                    entry[value_key] = json!(desired);
                    field_changed = true;
                }
            }
            if !found {
                let mut entry = json!({key: 8, value_key: offset + u64::from(require_fsr2)});
                if field == "globalPerfData" {
                    entry["itemVersion"] =
                        json!("OSRELWin5.0.0");
                }
                entries.push(entry);
                field_changed = true;
            }
        }
        if field_changed {
            root[field] = Value::String(
                serde_json::to_string(&settings)
                    .map_err(|error| error.to_string())?,
            );
            changed = true;
        }
    }
    if !changed {
        return Ok(None);
    }

    // 保持官方注册表记录的 ASCII JSON 编码，包括非 ASCII 字符的 UTF-16 转义。
    let text = serde_json::to_string(&root)
        .map_err(|error| error.to_string())?;
    let mut encoded = String::new();
    for ch in text.chars() {
        if ch.is_ascii() {
            encoded.push(ch);
        } else {
            use std::fmt::Write;
            for unit in ch.encode_utf16(&mut [0; 2]) {
                write!(&mut encoded, "\\u{unit:04x}")
                    .map_err(|error| error.to_string())?;
            }
        }
    }
    let mut bytes = encoded.into_bytes();
    bytes.push(0);
    Ok(Some(bytes))
}

#[derive(serde::Serialize, serde::Deserialize)]
struct Ownership {
    original: Vec<u8>,
    applied: Vec<u8>,
}

// 仅还原仍等于本插件写入值的 AA 字段；其他画质及玩家后续修改必须保留。
fn restore_owned(
    record: &Ownership,
    current: &[u8],
) -> Result<Option<Vec<u8>>, String> {
    fn parse(bytes: &[u8]) -> Result<Value, String> {
        serde_json::from_slice(
            bytes
                .split(|byte| *byte == 0)
                .next()
                .unwrap_or_default(),
        )
        .map_err(|error| error.to_string())
    }
    let original = parse(&record.original)?;
    let applied = parse(&record.applied)?;
    let mut result = parse(current)?;
    let mut changed = false;
    for (field, list, key, value_key) in [
        (
            "graphicsData",
            "customVolatileGrades",
            "key",
            "value",
        ),
        (
            "globalPerfData",
            "saveItems",
            "entryType",
            "index",
        ),
    ] {
        let settings =
            |root: &Value| -> Result<Value, String> {
                serde_json::from_str(
                    root[field]
                        .as_str()
                        .ok_or("画质记录缺少字段")?,
                )
                .map_err(|error| error.to_string())
            };
        let before = settings(&original)?;
        let written = settings(&applied)?;
        let mut now = settings(&result)?;
        let find = |root: &Value| {
            root[list]
                .as_array()
                .and_then(|entries| {
                    entries.iter().find(|entry| {
                        entry[key] == json!(8)
                    })
                })
                .cloned()
        };
        let (Some(written_aa), Some(current_aa)) =
            (find(&written), find(&now))
        else {
            continue;
        };
        if written_aa[value_key] != current_aa[value_key] {
            continue;
        }
        let entries = now[list]
            .as_array_mut()
            .ok_or("画质记录缺少列表")?;
        if let Some(before_aa) = find(&before) {
            if let Some(entry) = entries
                .iter_mut()
                .find(|entry| entry[key] == json!(8))
            {
                if entry[value_key] != before_aa[value_key]
                {
                    entry[value_key] =
                        before_aa[value_key].clone();
                    changed = true;
                }
            }
        } else {
            entries.retain(|entry| entry[key] != json!(8));
            changed = true;
        }
        if changed {
            result[field] =
                json!(serde_json::to_string(&now)
                    .map_err(|error| error.to_string())?);
        }
    }
    if !changed {
        return Ok(None);
    }
    let text = serde_json::to_string(&result)
        .map_err(|error| error.to_string())?;
    let mut encoded = String::new();
    for ch in text.chars() {
        if ch.is_ascii() {
            encoded.push(ch);
        } else {
            use std::fmt::Write;
            for unit in ch.encode_utf16(&mut [0; 2]) {
                write!(&mut encoded, "\\u{unit:04x}")
                    .map_err(|error| error.to_string())?;
            }
        }
    }
    let mut bytes = encoded.into_bytes();
    bytes.push(0);
    Ok(Some(bytes))
}

#[cfg(windows)]
fn configure(
    target: &str,
    fsr2: Option<bool>,
) -> Result<(), String> {
    use std::{fs, io::Write, path::Path, ptr};
    use windows_sys::Win32::System::Registry::*;

    let executable = Path::new(target);
    if !executable.is_file() {
        return Err("原神游戏程序不存在".to_string());
    }
    let game_key = match executable
        .file_name()
        .and_then(|part| part.to_str())
        .unwrap_or("")
        .to_ascii_lowercase()
        .as_str()
    {
        "yuanshen.exe" => "原神",
        "genshinimpact.exe" => "Genshin Impact",
        _ => return Err("GIMI 设置只适用于 YuanShen.exe / GenshinImpact.exe".to_string()),
    };
    // 注册表由同一客户端的所有安装目录共享，不能在另一份客户端运行时恢复或写入。
    let name =
        executable.file_name().unwrap().to_string_lossy();
    let mut processes = sysinfo::System::new();
    processes.refresh_processes_specifics(
        sysinfo::ProcessRefreshKind::new(),
    );
    if processes.processes().values().any(|process| {
        process.name().eq_ignore_ascii_case(&name)
    }) {
        return Err("请关闭同一客户端的全部游戏实例后再调整抗锯齿设置".into());
    }
    let wide = |text: &str| {
        text.encode_utf16()
            .chain(Some(0))
            .collect::<Vec<_>>()
    };
    let key_name =
        wide(&format!("SOFTWARE\\miHoYo\\{game_key}"));
    let value_name = wide("GENERAL_DATA_h2389025596");
    let mut raw_key = ptr::null_mut();
    let error = unsafe {
        RegOpenKeyExW(
            HKEY_CURRENT_USER,
            key_name.as_ptr(),
            0,
            KEY_QUERY_VALUE | KEY_SET_VALUE,
            &mut raw_key,
        )
    };
    if error != 0 {
        return Err(format!(
            "无法打开原神设置，请先通过官方启动器运行一次游戏（{error}）"
        ));
    }
    struct Key(HKEY);
    impl Drop for Key {
        fn drop(&mut self) {
            unsafe {
                RegCloseKey(self.0);
            }
        }
    }
    let key = Key(raw_key);
    let read = || -> Result<Vec<u8>, String> {
        let (mut kind, mut size) = (0, 0);
        let error = unsafe {
            RegQueryValueExW(
                key.0,
                value_name.as_ptr(),
                ptr::null(),
                &mut kind,
                ptr::null_mut(),
                &mut size,
            )
        };
        if error != 0
            || kind != REG_BINARY
            || size == 0
            || size > 4 * 1024 * 1024
        {
            return Err(format!("原神画质设置不是有效的 REG_BINARY（{error}）"));
        }
        let mut bytes = vec![0; size as usize];
        let error = unsafe {
            RegQueryValueExW(
                key.0,
                value_name.as_ptr(),
                ptr::null(),
                &mut kind,
                bytes.as_mut_ptr(),
                &mut size,
            )
        };
        if error != 0 || kind != REG_BINARY {
            return Err(format!(
                "无法读取原神画质设置（{error}）"
            ));
        }
        bytes.truncate(size as usize);
        Ok(bytes)
    };
    let original = read()?;
    let ownership_root =
        crate::context::data_root().join("registry");
    fs::create_dir_all(&ownership_root)
        .map_err(|error| error.to_string())?;
    let transaction=ssmt_plugin_graphics::managed_files::DirectoryTransaction::open(&ownership_root)?;
    let record_name = format!("{game_key}.json");
    let record_path = ownership_root.join(&record_name);
    let changes = |value: Option<Vec<u8>>| {
        std::collections::BTreeMap::from([(
            record_name.clone(),
            value,
        )])
    };
    let updated = if let Some(fsr2) = fsr2 {
        let Some(updated) = update_graphics_settings(
            &original,
            false,
            Some(fsr2),
        )?
        else {
            return Ok(());
        };
        let record = Ownership {
            original: original.clone(),
            applied: updated.clone(),
        };
        // 所有权先于注册表写入持久化，进程中断后仍能恢复。
        transaction.commit(changes(Some(
            serde_json::to_vec(&record)
                .map_err(|error| error.to_string())?,
        )))?;
        updated
    } else {
        let bytes = match fs::read(&record_path) {
            Ok(bytes) => bytes,
            Err(error)
                if error.kind()
                    == std::io::ErrorKind::NotFound =>
            {
                return Ok(())
            }
            Err(error) => return Err(error.to_string()),
        };
        let record: Ownership =
            serde_json::from_slice(&bytes)
                .map_err(|error| error.to_string())?;
        let Some(updated) =
            restore_owned(&record, &original)?
        else {
            transaction.commit(changes(None))?;
            return Ok(());
        };
        updated
    };
    let folder = crate::context::data_root()
        .join("GameSettingsBackups")
        .join("GIMI")
        .join(game_key);
    fs::create_dir_all(&folder)
        .map_err(|error| error.to_string())?;
    let backup = folder.join(format!(
        "GENERAL_DATA-{}.bin",
        chrono::Utc::now()
            .timestamp_nanos_opt()
            .unwrap_or_default()
    ));
    fs::OpenOptions::new()
        .write(true)
        .create_new(true)
        .open(&backup)
        .and_then(|mut file| file.write_all(&original))
        .map_err(|error| {
            format!("无法备份原神设置：{error}")
        })?;
    if read()? != original {
        return Err(
            "原神设置已被其他程序修改，请关闭游戏后重试"
                .to_string(),
        );
    }
    let error = unsafe {
        RegSetValueExW(
            key.0,
            value_name.as_ptr(),
            0,
            REG_BINARY,
            updated.as_ptr(),
            updated.len() as u32,
        )
    };
    if error != 0 {
        return Err(format!(
            "无法写入原神画质设置（{error}）"
        ));
    }
    if fsr2.is_none() {
        transaction.commit(changes(None))?;
    }
    eprintln!(
        "[DLSS5] 原神抗锯齿设置已更新：FSR2={fsr2:?}；原始设置备份：{}", backup.display()
    );
    Ok(())
}

pub(crate) fn configure_dlss5(
    target: &str,
    fsr2: bool,
) -> Result<(), String> {
    #[cfg(windows)]
    {
        restore_dlss5(target)?;
        configure(target, Some(fsr2))
    }
    #[cfg(not(windows))]
    {
        let _ = (target, fsr2);
        Err("原神画质设置仅适用于 Windows".into())
    }
}

pub(crate) fn restore_dlss5(
    target: &str,
) -> Result<(), String> {
    if !crate::plugin::gimi_hot_runtime::applies(
        std::path::Path::new(target),
    ) {
        return Ok(());
    }
    // 没有所有权记录时不读取游戏注册表，也不要求游戏已运行过。
    let game = if std::path::Path::new(target)
        .file_name()
        .is_some_and(|name| {
            name.to_string_lossy()
                .eq_ignore_ascii_case("YuanShen.exe")
        }) {
        "原神"
    } else {
        "Genshin Impact"
    };
    if !crate::context::data_root()
        .join("registry")
        .join(format!("{game}.json"))
        .exists()
    {
        return Ok(());
    }
    crate::plugin::gimi_hot_runtime::ensure_stopped(
        std::path::Path::new(target),
    )?;
    #[cfg(windows)]
    {
        configure(target, None)
    }
    #[cfg(not(windows))]
    {
        Err("原神画质设置仅适用于 Windows".into())
    }
}

#[cfg(test)]
mod tests {
    use super::*;
    #[test]
    fn ownership_restore_preserves_other_settings_and_later_user_aa_changes(
    ) {
        let original = fixture(
            json!([{"key":8,"value":3}]),
            json!([{"entryType":8,"index":2}]),
        );
        let applied = update_graphics_settings(
            &original,
            false,
            Some(false),
        )
        .unwrap()
        .unwrap();
        let record = Ownership {
            original: original.clone(),
            applied: applied.clone(),
        };
        let mut current: Value = serde_json::from_slice(
            &applied[..applied.len() - 1],
        )
        .unwrap();
        current["other"] = json!("玩家后续设置");
        let current = serde_json::to_vec(&current).unwrap();
        let restored = restore_owned(&record, &current)
            .unwrap()
            .unwrap();
        let root: Value = serde_json::from_slice(
            &restored[..restored.len() - 1],
        )
        .unwrap();
        assert_eq!(root["other"], json!("玩家后续设置"));
        let graphics: Value = serde_json::from_str(
            root["graphicsData"].as_str().unwrap(),
        )
        .unwrap();
        assert_eq!(
            graphics["customVolatileGrades"][0]["value"],
            3
        );
        let user_aa = update_graphics_settings(
            &applied,
            false,
            Some(true),
        )
        .unwrap()
        .unwrap();
        assert!(restore_owned(&record, &user_aa)
            .unwrap()
            .is_none());
        assert!(restore_owned(&record, &original)
            .unwrap()
            .is_none());
    }
    #[test]
    fn ownership_restore_removes_only_aa_records_created_by_plugin(
    ) {
        let original = fixture(json!([]), json!([]));
        let applied = update_graphics_settings(
            &original,
            false,
            Some(true),
        )
        .unwrap()
        .unwrap();
        let restored = restore_owned(
            &Ownership {
                original: original.clone(),
                applied: applied.clone(),
            },
            &applied,
        )
        .unwrap()
        .unwrap();
        let parse = |bytes: &[u8]| {
            serde_json::from_slice::<Value>(
                &bytes[..bytes.len() - 1],
            )
            .unwrap()
        };
        assert_eq!(parse(&restored), parse(&original));
    }
    fn fixture(graphics: Value, perf: Value) -> Vec<u8> {
        let mut bytes = serde_json::to_vec(&json!({
            "graphicsData": json!({"customVolatileGrades": graphics, "untouched": "画质😀"}).to_string(),
            "globalPerfData": json!({"saveItems": perf}).to_string(),
            "other": [1, 2, 3],
        })).unwrap();
        bytes.push(0);
        bytes
    }
    #[test]
    fn dlss_routes_control_both_aa_records_without_touching_dynamic_resolution(
    ) {
        let source = fixture(
            json!([{"key":8,"value":3},{"key":21,"value":2},{"key":2,"value":8}]),
            json!([{"entryType":8,"index":1,"itemVersion":"CNRELWin7.1.0"},{"entryType":21,"index":1}]),
        );
        for fsr2 in [true, false] {
            let updated = update_graphics_settings(
                &source,
                false,
                Some(fsr2),
            )
            .unwrap()
            .unwrap();
            let root: Value = serde_json::from_slice(
                &updated[..updated.len() - 1],
            )
            .unwrap();
            let graphics: Value = serde_json::from_str(
                root["graphicsData"].as_str().unwrap(),
            )
            .unwrap();
            let perf: Value = serde_json::from_str(
                root["globalPerfData"].as_str().unwrap(),
            )
            .unwrap();
            assert_eq!(
                graphics["customVolatileGrades"][0]
                    ["value"],
                1 + u64::from(fsr2)
            );
            assert_eq!(
                perf["saveItems"][0]["index"],
                u64::from(fsr2)
            );
            assert_eq!(
                perf["saveItems"][0]["itemVersion"],
                "CNRELWin7.1.0"
            );
            assert_eq!(
                graphics["customVolatileGrades"][1]
                    ["value"],
                2
            );
            assert_eq!(
                graphics["customVolatileGrades"][2]
                    ["value"],
                8
            );
            assert_eq!(perf["saveItems"][1]["index"], 1);
            assert_eq!(
                update_graphics_settings(
                    &updated,
                    false,
                    Some(fsr2)
                )
                .unwrap(),
                None
            );
        }
        assert_eq!(
            update_graphics_settings(&source, false, None)
                .unwrap(),
            None
        );
    }

    #[test]
    fn aa_rejects_future_values_and_repairs_missing_record()
    {
        for (value, index) in [(4, 1), (2, 3)] {
            let source = fixture(
                json!([{"key":8,"value":value}]),
                json!([{"entryType":8,"index":index}]),
            );
            assert!(update_graphics_settings(
                &source,
                false,
                Some(true)
            )
            .is_err());
        }
        let updated = update_graphics_settings(
            &fixture(json!([]), json!([])),
            true,
            Some(true),
        )
        .unwrap()
        .unwrap();
        assert_eq!(
            update_graphics_settings(
                &updated,
                true,
                Some(true)
            )
            .unwrap(),
            None
        );
    }
    #[test]
    fn disables_both_records_and_preserves_other_settings()
    {
        let source = fixture(
            json!([{"key":21,"value":2},{"key":3,"value":4}]),
            json!([{"entryType":21,"index":1,"itemVersion":"CNRELWin7.1.0"}]),
        );
        let updated =
            disable_dynamic_character_resolution(&source)
                .unwrap()
                .unwrap();
        assert!(updated.is_ascii());
        assert_eq!(updated.last(), Some(&0));
        let root: Value = serde_json::from_slice(
            &updated[..updated.len() - 1],
        )
        .unwrap();
        let graphics: Value = serde_json::from_str(
            root["graphicsData"].as_str().unwrap(),
        )
        .unwrap();
        let perf: Value = serde_json::from_str(
            root["globalPerfData"].as_str().unwrap(),
        )
        .unwrap();
        assert_eq!(
            graphics["customVolatileGrades"],
            json!([{"key":21,"value":1},{"key":3,"value":4}])
        );
        assert_eq!(graphics["untouched"], "画质😀");
        assert_eq!(
            perf["saveItems"][0],
            json!({"entryType":21,"index":0,"itemVersion":"CNRELWin7.1.0"})
        );
        assert_eq!(root["other"], json!([1, 2, 3]));
        assert_eq!(
            disable_dynamic_character_resolution(&updated)
                .unwrap(),
            None
        );
    }
    #[test]
    fn adds_missing_entries_and_handles_unterminated_json()
    {
        let mut source = fixture(json!([]), json!([]));
        source.pop();
        let updated =
            disable_dynamic_character_resolution(&source)
                .unwrap()
                .unwrap();
        assert_eq!(
            disable_dynamic_character_resolution(&updated)
                .unwrap(),
            None
        );
    }
    #[test]
    fn repairs_inconsistent_records() {
        for (value, index) in [(1, 1), (2, 0)] {
            let source = fixture(
                json!([{"key":21,"value":value}]),
                json!([{"entryType":21,"index":index}]),
            );
            assert!(disable_dynamic_character_resolution(
                &source
            )
            .unwrap()
            .is_some());
        }
    }
    #[test]
    fn rejects_unknown_format_without_partial_output() {
        for source in [
            b"invalid".to_vec(),
            b"{}".to_vec(),
            fixture(
                json!([{"key":21,"value":3}]),
                json!([]),
            ),
            fixture(json!([]), json!({})),
        ] {
            assert!(disable_dynamic_character_resolution(
                &source
            )
            .is_err());
        }
    }
}
