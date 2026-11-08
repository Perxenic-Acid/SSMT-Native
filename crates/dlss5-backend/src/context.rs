use serde_json::Value;
use std::{path::PathBuf, sync::OnceLock};

static CONTEXT: OnceLock<Value> = OnceLock::new();

pub fn initialize(value: Value) -> Result<(), String> {
    for key in [
        "pluginId",
        "packageRoot",
        "dataRoot",
        "assetRoot",
        "gameName",
    ] {
        if value[key].as_str().is_none_or(str::is_empty) {
            return Err(format!("缺少后台上下文：{key}"));
        }
    }
    CONTEXT
        .set(value)
        .map_err(|_| "后台上下文已初始化".into())
}

pub fn value() -> &'static Value {
    CONTEXT.get().expect("后台入口必须先初始化自身上下文")
}

pub fn data_root() -> PathBuf {
    PathBuf::from(value()["dataRoot"].as_str().unwrap())
}

pub fn asset_root() -> PathBuf {
    PathBuf::from(value()["assetRoot"].as_str().unwrap())
}

/// 仅供旧组件路径算法接管历史布局，不读取本体配置或其他插件数据。
pub fn legacy_root() -> PathBuf {
    #[cfg(test)]
    if CONTEXT.get().is_none() {
        return std::env::temp_dir().join(format!(
            "ssmt-dlss5-tests-{}",
            std::process::id()
        ));
    }
    asset_root()
        .parent()
        .and_then(|parent| parent.parent())
        .expect("插件资源目录必须有宿主根目录")
        .to_path_buf()
}
