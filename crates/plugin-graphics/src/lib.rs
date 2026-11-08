//! 插件侧共享图形实现；不依赖 SSMT 应用或其插件注册表。
pub mod managed_files;
pub mod managed_reshade;
pub mod paths;
pub mod reshade_state;

pub fn validate_package_relative_path(
    path: &str,
) -> Result<(), String> {
    if path.is_empty()
        || path.contains('\0')
        || path.starts_with('/')
        || path.starts_with('\\')
        || path.as_bytes().get(1) == Some(&b':')
    {
        return Err(format!("无效的插件相对路径：{path}"));
    }

    for segment in path.replace('\\', "/").split('/') {
        let stem = segment
            .split('.')
            .next()
            .unwrap_or_default()
            .to_ascii_uppercase();
        let reserved = matches!(
            stem.as_str(),
            "CON" | "PRN" | "AUX" | "NUL"
        ) || (stem.len() == 4
            && (stem.starts_with("COM")
                || stem.starts_with("LPT"))
            && matches!(stem.as_bytes()[3], b'1'..=b'9'));
        if segment.is_empty()
            || segment == "."
            || segment == ".."
            || segment.ends_with(['.', ' '])
            || segment.chars().any(|character| {
                character < ' '
                    || "<>:\"|?*".contains(character)
            })
            || reserved
        {
            return Err(format!(
                "无效的插件相对路径：{path}"
            ));
        }
    }
    Ok(())
}
