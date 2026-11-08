use crate::{
    managed_reshade::{self, ManagedReShadeConfig},
    paths::{canonical_path, checked_path, read_text},
};
use std::{
    collections::BTreeMap,
    fs,
    path::{Path, PathBuf},
};
#[derive(Debug, Clone)]
pub struct PreparedConfig {
    pub game_ini: String,
    pub preset_ini: String,
    pub addon_sources: BTreeMap<String, PathBuf>,
    pub addon_directory: Option<PathBuf>,
    pub extra_files: BTreeMap<PathBuf, Vec<u8>>,
}

impl From<ManagedReShadeConfig> for PreparedConfig {
    fn from(config: ManagedReShadeConfig) -> Self {
        Self {
            game_ini: config.game_ini,
            preset_ini: config.preset_ini,
            addon_sources: BTreeMap::new(),
            addon_directory: None,
            extra_files: BTreeMap::new(),
        }
    }
}

/// 已解析的插件侧 ReShade 贡献；不包含 SSMT 注册表或内置插件身份。
#[derive(
    Debug, Clone, serde::Serialize, serde::Deserialize,
)]
#[serde(rename_all = "camelCase", deny_unknown_fields)]
pub struct Contribution {
    pub source: String,
    pub config: String,
    pub preset: String,
    pub addons: BTreeMap<String, PathBuf>,
}

/// 每条图形路线复用同一组合算法，统一处理多个贡献与文件所有权冲突。
pub fn compose(
    contributions: impl IntoIterator<Item = Contribution>,
    addon_directory: &Path,
    required: &[String],
    host_ini: &str,
    host_preset: &str,
) -> Result<PreparedConfig, String> {
    let mut base = String::new();
    let mut preset = String::new();
    let mut addons = required.to_vec();
    let mut addon_sources = BTreeMap::new();
    for contribution in contributions {
        let merged = managed_reshade::compose(
            &base,
            &contribution.config,
            &preset,
            &contribution.preset,
            &[],
        )
        .map_err(|e| {
            format!(
                "合并共享 ReShade 贡献失败（{}）：{e}",
                contribution.source
            )
        })?;
        base = merged.game_ini;
        preset = merged.preset_ini;
        for (name, source) in contribution.addons {
            if addons.iter().any(|other| {
                other.eq_ignore_ascii_case(&name)
            }) {
                return Err(format!(
                    "Add-on 资源所有权冲突：{name}"
                ));
            }
            addons.push(name.clone());
            addon_sources.insert(name, source);
        }
    }

    let mut prepared: PreparedConfig =
        managed_reshade::compose(&base, host_ini, &preset, host_preset, required)
            .map_err(|error| format!("合并共享 ReShade 与当前图形路线的配置失败：{error}"))?
            .into();
    prepared.addon_sources = addon_sources;
    prepared.addon_directory = Some(
        canonical_path(addon_directory)
            .map_err(|e| e.to_string())?,
    );
    Ok(prepared)
}

/// 配置与默认值分开保存，避免每次启动重置 ReShade 的教程、按键和效果参数。
pub fn save_config(
    directory: &Path,
    defaults: impl Into<PreparedConfig>,
    required_preset: &str,
    addons: &[String],
) -> Result<(), String> {
    let root = canonical_path(directory)
        .map_err(|error| error.to_string())?;
    let prepared = defaults.into();
    let addon_root = prepared
        .addon_directory
        .as_deref()
        .unwrap_or(&root);
    let addon_root = canonical_path(addon_root)
        .map_err(|e| e.to_string())?;
    let mut transaction_root = root.clone();
    for directory in std::iter::once(addon_root.as_path())
        .chain(
            prepared
                .extra_files
                .keys()
                .filter_map(|path| path.parent()),
        )
    {
        while !directory.starts_with(&transaction_root) {
            if !transaction_root.pop() {
                return Err("配置目录不在同一安装中".into());
            }
        }
    }
    let transaction =
        super::managed_files::DirectoryTransaction::open(
            &transaction_root,
        )?;
    let key = |directory: &Path,
               name: &str|
     -> Result<String, String> {
        directory
            .join(name)
            .strip_prefix(&transaction_root)
            .map(|path| {
                path.to_string_lossy().replace('\\', "/")
            })
            .map_err(|e| e.to_string())
    };
    let defaults = ManagedReShadeConfig {
        game_ini: prepared.game_ini,
        preset_ini: prepared.preset_ini,
    };
    let read =
        |name: &str| -> Result<Option<String>, String> {
            if root.join(name).exists() {
                read_text(&checked_path(&root, name)?)
                    .map(Some)
            } else {
                Ok(None)
            }
        };
    let previous = read("ReShade.ssmt-defaults.json")?
        .and_then(|text| {
            serde_json::from_str::<ManagedReShadeConfig>(
                &text,
            )
            .ok()
        });
    let current = ManagedReShadeConfig {
        game_ini: read("ReShade.ini")?
            .unwrap_or_else(|| defaults.game_ini.clone()),
        preset_ini: read("ReShadePreset.ini")?
            .unwrap_or_else(|| defaults.preset_ini.clone()),
    };
    let composed = managed_reshade::retain_user_state(
        &defaults,
        previous.as_ref(),
        &current,
        required_preset,
        addons,
    );
    let baseline = serde_json::to_string(&defaults)
        .map_err(|error| error.to_string())?;
    let ownership_name = "ReShade.ssmt-addons.json";
    let previous_addons: BTreeMap<String, String> =
        read(ownership_name)?
            .map(|text| {
                serde_json::from_str(&text).map_err(|e| {
                    format!("Add-on 所有权记录无效：{e}")
                })
            })
            .transpose()?
            .unwrap_or_default();
    let mut changes = BTreeMap::new();
    let mut ownership = BTreeMap::new();
    // 旧配置尚无所有权记录时不能悄悄加载来源不明的遗留 Add-on。
    for entry in fs::read_dir(&addon_root)
        .map_err(|e| e.to_string())?
    {
        let path = entry.map_err(|e| e.to_string())?.path();
        let name = path
            .file_name()
            .unwrap_or_default()
            .to_string_lossy();
        if name.to_ascii_lowercase().ends_with(".addon64")
            && !addons.iter().any(|item| {
                item.eq_ignore_ascii_case(&name)
            })
            && !prepared.addon_sources.keys().any(|item| {
                item.eq_ignore_ascii_case(&name)
            })
            && !previous_addons.keys().any(|item| {
                item.eq_ignore_ascii_case(&name)
            })
        {
            return Err(format!(
                "组件目录存在未登记的 Add-on {name}，请重新配置组件；已保留原文件"
            ));
        }
    }
    for (name, source) in prepared.addon_sources {
        super::validate_package_relative_path(&name)
            .map_err(|e| e.to_string())?;
        if name.contains(['/', '\\']) {
            return Err("Add-on 文件名无效".into());
        }
        let bytes =
            fs::read(&source).map_err(|e| e.to_string())?;
        use sha2::{Digest, Sha256};
        let applied =
            format!("{:x}", Sha256::digest(&bytes));
        let current = super::managed_files::hash(
            &addon_root.join(&name),
        )?;
        if current.is_some()
            && current.as_ref() != Some(&applied)
            && current.as_ref()
                != previous_addons.get(&name)
        {
            return Err(format!(
                "Add-on {name} 不属于当前贡献，已保留文件"
            ));
        }
        ownership.insert(name.clone(), applied);
        changes
            .insert(key(&addon_root, &name)?, Some(bytes));
    }
    for (name, applied) in previous_addons {
        super::validate_package_relative_path(&name)
            .map_err(|e| e.to_string())?;
        if name.contains(['/', '\\']) {
            return Err(
                "Add-on 所有权记录文件名无效".into()
            );
        }
        if ownership.contains_key(&name) {
            continue;
        }
        if addons.iter().any(|required| {
            required.eq_ignore_ascii_case(&name)
        }) {
            return Err(format!(
                "Add-on 所有权与必要组件冲突：{name}"
            ));
        }
        let current = super::managed_files::hash(
            &addon_root.join(&name),
        )?;
        if current.is_some()
            && current.as_deref() != Some(&applied)
        {
            return Err(format!(
                "Add-on {name} 已被修改，已保留文件"
            ));
        }
        changes.insert(key(&addon_root, &name)?, None);
    }
    changes.insert(
        key(&root, ownership_name)?,
        Some(
            serde_json::to_vec(&ownership)
                .map_err(|e| e.to_string())?,
        ),
    );
    for (name, content) in [
        ("ReShade.ini", composed.game_ini),
        ("ReShadePreset.ini", composed.preset_ini),
        ("ReShade.ssmt-defaults.json", baseline),
    ] {
        changes.insert(
            key(&root, name)?,
            Some(content.into_bytes()),
        );
    }
    for (path, bytes) in prepared.extra_files {
        let relative = path
            .strip_prefix(&transaction_root)
            .map_err(|e| e.to_string())?
            .to_string_lossy()
            .replace('\\', "/");
        if changes.insert(relative, Some(bytes)).is_some() {
            return Err("配置文件所有权冲突".into());
        }
    }
    transaction.commit(changes)?;
    Ok(())
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn empty_single_and_multiple_contributions_share_one_configuration(
    ) {
        let root = std::env::temp_dir().join(format!(
            "ssmt-reshade-combinations-{}",
            std::time::SystemTime::now()
                .duration_since(std::time::UNIX_EPOCH)
                .unwrap()
                .as_nanos()
        ));
        fs::create_dir(&root).unwrap();
        for count in 0..=3 {
            let contributions = (0..count).map(|index| Contribution {
                source: format!("plugin-{index}"),
                config: format!("[GENERAL]\nEffectSearchPaths=C:/Plugin{index}/FX\n"),
                preset: format!("Techniques=Effect{index}@effect{index}.fx\n"),
                addons: BTreeMap::from([(format!("plugin-{index}.addon64"), root.join(format!("source-{index}")))]),
            });
            let prepared =
                compose(contributions, &root, &[], "", "")
                    .unwrap();
            assert_eq!(prepared.addon_sources.len(), count);
            for index in 0..count {
                assert!(prepared.game_ini.contains(
                    &format!("C:/Plugin{index}/FX")
                ));
                assert!(prepared.preset_ini.contains(
                    &format!(
                        "Effect{index}@effect{index}.fx"
                    )
                ));
            }
        }
        fs::remove_dir(root).unwrap();
    }

    #[test]
    fn addon_claims_conflict_case_insensitively_including_host_requirements(
    ) {
        let contribution = |name: &str| Contribution {
            source: name.into(),
            config: String::new(),
            preset: String::new(),
            addons: BTreeMap::from([(
                name.into(),
                PathBuf::from("source"),
            )]),
        };
        let root = std::env::temp_dir();
        assert!(compose(
            [
                contribution("Effect.addon64"),
                contribution("effect.addon64")
            ],
            &root,
            &[],
            "",
            ""
        )
        .unwrap_err()
        .contains("所有权冲突"));
        assert!(compose(
            [contribution("Effect.addon64")],
            &root,
            &["effect.addon64".into()],
            "",
            ""
        )
        .unwrap_err()
        .contains("所有权冲突"));
    }
    #[test]
    fn separate_config_and_addon_directories_commit_together_without_loading_unknown_addons(
    ) {
        let root = std::env::temp_dir().join(format!(
            "ssmt-separated-config-{}",
            std::time::SystemTime::now()
                .duration_since(std::time::UNIX_EPOCH)
                .unwrap()
                .as_nanos()
        ));
        let config = root.join("state");
        let addons = root.join("addons");
        fs::create_dir_all(&config).unwrap();
        fs::create_dir(&addons).unwrap();
        let source = root.join("source.dll");
        fs::write(&source, b"optional addon").unwrap();
        let mut prepared: PreparedConfig =
            managed_reshade::compose("", "", "", "", &[])
                .unwrap()
                .into();
        prepared.addon_directory = Some(addons.clone());
        prepared
            .addon_sources
            .insert("effect.addon64".into(), source);
        prepared.extra_files.insert(
            root.join("backend.ini"),
            b"configured".to_vec(),
        );
        save_config(&config, prepared.clone(), "", &[])
            .unwrap();
        assert!(addons.join("effect.addon64").is_file());
        assert!(!config.join("effect.addon64").exists());
        assert_eq!(
            fs::read(root.join("backend.ini")).unwrap(),
            b"configured"
        );
        fs::write(
            addons.join("unknown.addon64"),
            b"unknown",
        )
        .unwrap();
        assert!(save_config(&config, prepared, "", &[])
            .unwrap_err()
            .contains("未登记"));
        assert_eq!(
            fs::read(addons.join("unknown.addon64"))
                .unwrap(),
            b"unknown"
        );
        fs::remove_dir_all(root).unwrap();
    }

    #[test]
    fn changed_addon_is_preserved_and_failed_prepare_keeps_all_configs(
    ) {
        let root = std::env::temp_dir().join(format!(
            "ssmt-addon-ownership-{}",
            std::time::SystemTime::now()
                .duration_since(std::time::UNIX_EPOCH)
                .unwrap()
                .as_nanos()
        ));
        fs::create_dir(&root).unwrap();
        let source = root.join("source.dll");
        fs::write(&source, b"original addon").unwrap();
        let defaults = managed_reshade::compose(
            "[OVERLAY]\nTutorialProgress=0\n",
            "",
            "",
            "",
            &[],
        )
        .unwrap();
        let mut prepared: PreparedConfig =
            defaults.clone().into();
        prepared
            .addon_sources
            .insert("player.addon64".into(), source);
        save_config(&root, prepared, "", &[]).unwrap();
        let ini =
            fs::read(root.join("ReShade.ini")).unwrap();
        fs::write(
            root.join("player.addon64"),
            b"player updated addon",
        )
        .unwrap();
        assert!(save_config(
            &root,
            defaults.clone(),
            "",
            &[]
        )
        .unwrap_err()
        .contains("已被修改"));
        assert_eq!(
            fs::read(root.join("ReShade.ini")).unwrap(),
            ini
        );
        assert_eq!(
            fs::read(root.join("player.addon64")).unwrap(),
            b"player updated addon"
        );
        fs::write(
            root.join("player.addon64"),
            b"original addon",
        )
        .unwrap();
        save_config(&root, defaults, "", &[]).unwrap();
        assert!(!root.join("player.addon64").exists());
        fs::remove_dir_all(root).unwrap();
    }

    #[test]
    fn repeated_launch_preserves_tutorial_controls_and_uniforms_but_removes_disabled_contributions(
    ) {
        let root = std::env::temp_dir().join(format!(
            "ssmt-reshade-state-{}",
            std::time::SystemTime::now()
                .duration_since(std::time::UNIX_EPOCH)
                .unwrap()
                .as_nanos()
        ));
        fs::create_dir(&root).unwrap();
        let required = vec!["feed.addon64".into()];
        let feeder = "Techniques=MV@mv.fx,Feed@feed.fx\nTechniqueSorting=MV@mv.fx,Feed@feed.fx\n[feed.fx]\nPreprocessorDefinitions=PROVIDER=3\nStrength=1\n";
        let mut defaults = managed_reshade::compose(
            "[GENERAL]\nEffectSearchPaths=C:/Shade/FX\n[OVERLAY]\nTutorialProgress=0\n[ADDON]\nDisabledAddons=unrelated\n",
            "", "Techniques=Shade@shade.fx\n[shade.fx]\nBrightness=1\n", feeder, &required,
        ).unwrap();
        save_config(
            &root,
            defaults.clone(),
            feeder,
            &required,
        )
        .unwrap();
        let mut current =
            read_text(&root.join("ReShade.ini")).unwrap();
        for (section, key, value) in [
            ("OVERLAY", "TutorialProgress", "4"),
            ("INPUT", "KeyOverlay", "36,0,0,0"),
            (
                "GENERAL",
                "EffectSearchPaths",
                "C:/Shade/FX,C:/User/FX",
            ),
            (
                "ADDON",
                "DisabledAddons",
                "feed,unrelated,user-addon",
            ),
            ("ADDON", "AddonPath", "C:/wrong-host"),
        ] {
            managed_reshade::set_entry(
                &mut current,
                section,
                key,
                value,
            );
        }
        fs::write(root.join("ReShade.ini"), current)
            .unwrap();
        let mut preset =
            read_text(&root.join("ReShadePreset.ini"))
                .unwrap();
        managed_reshade::set_entry(
            &mut preset,
            "",
            "Techniques",
            "User@user.fx,Feed@feed.fx",
        );
        managed_reshade::set_entry(
            &mut preset,
            "feed.fx",
            "Strength",
            "0.75",
        );
        managed_reshade::set_entry(
            &mut preset,
            "feed.fx",
            "PreprocessorDefinitions",
            "PROVIDER=0",
        );
        managed_reshade::set_entry(
            &mut preset,
            "user.fx",
            "Color",
            "0.5",
        );
        fs::write(root.join("ReShadePreset.ini"), preset)
            .unwrap();
        for _ in 0..2 {
            save_config(
                &root,
                defaults.clone(),
                feeder,
                &required,
            )
            .unwrap();
            let ini = read_text(&root.join("ReShade.ini"))
                .unwrap();
            assert!(ini.contains("TutorialProgress=4"));
            assert!(ini.contains("KeyOverlay=36,0,0,0"));
            assert!(ini.contains(
                "DisabledAddons=unrelated,user-addon"
            ));
            assert!(!ini.contains("C:/wrong-host"));
            let preset =
                read_text(&root.join("ReShadePreset.ini"))
                    .unwrap();
            assert!(preset.contains("Techniques=MV@mv.fx,Feed@feed.fx,User@user.fx"));
            assert!(
                !preset
                    .lines()
                    .find(|line| line
                        .starts_with("Techniques="))
                    .unwrap()
                    .contains("Shade@")
            );
            assert!(preset.contains("Strength=0.75"));
            assert!(preset.contains("PROVIDER=3"));
        }
        defaults = managed_reshade::compose(
            "", "", "", feeder, &required,
        )
        .unwrap();
        save_config(&root, defaults, feeder, &required)
            .unwrap();
        let ini =
            read_text(&root.join("ReShade.ini")).unwrap();
        let preset =
            read_text(&root.join("ReShadePreset.ini"))
                .unwrap();
        assert!(ini.contains("TutorialProgress=4"));
        assert!(!ini.contains("C:/Shade/FX"));
        assert!(ini.contains("C:/User/FX"));
        assert!(!preset.contains("shade.fx"));
        assert!(preset.contains("Color=0.5"));
        fs::remove_dir_all(&root).unwrap();
    }

    #[test]
    fn existing_gimi_configuration_migrates_without_resetting_tutorial(
    ) {
        let root = std::env::temp_dir().join(format!(
            "ssmt-reshade-migrate-{}",
            std::time::SystemTime::now()
                .duration_since(std::time::UNIX_EPOCH)
                .unwrap()
                .as_nanos()
        ));
        fs::create_dir(&root).unwrap();
        fs::write(
            root.join("ReShade.ini"),
            "[OVERLAY]\nTutorialProgress=4\n[INPUT]\nKeyOverlay=35,0,0,0\n",
        )
        .unwrap();
        let defaults =
            managed_reshade::compose("", "", "", "", &[])
                .unwrap();
        for _ in 0..2 {
            save_config(&root, defaults.clone(), "", &[])
                .unwrap();
            let ini = read_text(&root.join("ReShade.ini"))
                .unwrap();
            assert!(ini.contains("TutorialProgress=4"));
            assert!(ini.contains("KeyOverlay=35,0,0,0"));
        }
        fs::remove_dir_all(root).unwrap();
    }
}
