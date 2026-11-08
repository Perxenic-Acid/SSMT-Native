mod policy;
mod runtime;

use serde_json::{json, Value};
use std::io::{Read, Write};

fn dispatch(request: &Value) -> Result<Value, String> {
    if request["protocolVersion"].as_u64() != Some(1) {
        return Err("不支持的后台协议".into());
    }
    let context =
        request.get("context").ok_or("缺少宿主上下文")?;
    let method =
        request["method"].as_str().ok_or("缺少后台方法")?;
    let payload = &request["payload"];
    match method {
        "status.read" => {
            let policy = policy::Policy::load(context)?;
            let dependency =
                policy.dependency_root(context);
            Ok(
                json!({"ready":dependency.is_ok(),"path":dependency.as_ref().ok(),"error":dependency.err(),"processes":policy.processes}),
            )
        }
        "launch.describe" => {
            let policy = policy::Policy::load(context)?;
            let executable =
                runtime::executable(context, payload)?;
            if !runtime::supports(&policy, &executable) {
                return Ok(
                    json!({"exports":{},"launch":null}),
                );
            }
            let contribution =
                policy.contribution(context)?;
            Ok(
                json!({"exports":{"ssmt.reshade.v1":contribution},"launch":{"priority":10,"resources":["graphics.reshade.host"]}}),
            )
        }
        "launch.prepare" => {
            runtime::prepare(context, payload)
        }
        // 热注入只在插件自有目录写配置；取消不修改游戏目录，也不清除用户设置。
        "launch.cancel" | "cancel" | "disabled"
        | "recover" => Ok(Value::Null),
        _ => Err(format!(
            "未知 HoYoShade 后台方法：{method}"
        )),
    }
}

fn main() {
    let result = (|| {
        let mut input = Vec::new();
        std::io::stdin()
            .take(4 * 1024 * 1024 + 1)
            .read_to_end(&mut input)
            .map_err(|error| error.to_string())?;
        if input.len() > 4 * 1024 * 1024 {
            return Err("请求过大".into());
        }
        let request: Value = serde_json::from_slice(&input)
            .map_err(|error| error.to_string())?;
        dispatch(&request)
    })();
    let output = match result {
        Ok(value) => json!({"ok":true,"value":value}),
        Err(error) => json!({"ok":false,"error":error}),
    };
    let _ = std::io::stdout()
        .write_all(output.to_string().as_bytes());
}
