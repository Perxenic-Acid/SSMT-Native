use std::{
    fs,
    path::{Path, PathBuf},
};
pub fn canonical_path(
    path: &Path,
) -> Result<PathBuf, std::io::Error> {
    let canonical = path.canonicalize()?;
    // canonicalize 在 Windows 返回 \\?\ 路径；第三方 INI/路径拼接可能含正斜杠，
    // 不满足 verbatim 路径规则。身份校验后以普通绝对路径传给组件。
    #[cfg(windows)]
    if let Some(text) = canonical.to_str() {
        if let Some(unc) = text.strip_prefix("\\\\?\\UNC\\")
        {
            return Ok(PathBuf::from(format!("\\\\{unc}")));
        }
        if let Some(disk) = text.strip_prefix("\\\\?\\") {
            return Ok(PathBuf::from(disk));
        }
    }
    Ok(canonical)
}

pub fn checked_path(
    root: &Path,
    relative: &str,
) -> Result<PathBuf, String> {
    let canonical = canonical_path(&root.join(relative))
        .map_err(|error| {
            format!("热注入组件缺失 {relative}：{error}")
        })?;
    if !canonical.starts_with(root) || !canonical.is_file()
    {
        return Err(format!(
            "热注入组件越出目录或不是文件：{relative}"
        ));
    }
    Ok(canonical)
}

pub fn read_text(path: &Path) -> Result<String, String> {
    if fs::metadata(path)
        .map_err(|error| error.to_string())?
        .len()
        > 4 * 1024 * 1024
    {
        return Err(format!(
            "配置文件过大：{}",
            path.display()
        ));
    }
    fs::read_to_string(path)
        .map(|text| {
            text.trim_start_matches('\u{feff}').to_string()
        })
        .map_err(|error| error.to_string())
}
