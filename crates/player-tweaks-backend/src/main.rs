mod settings;
mod star_rail;

use serde_json::{json, Value};
use std::io::{Read, Write};

fn dispatch(request: &Value) -> Result<Value, String> {
    if request
        .get("protocolVersion")
        .and_then(Value::as_u64)
        != Some(1)
    {
        return Err("不支持的后台协议".into());
    }
    let method = request
        .get("method")
        .and_then(Value::as_str)
        .ok_or("缺少方法")?;
    let context =
        request.get("context").ok_or("缺少宿主上下文")?;
    match method {
        "settings.read" => settings::read(context),
        "settings.save" => {
            settings::save(context, &request["payload"])
        }
        "prepare" => {
            settings::prepare(context, &request["payload"])
        }
        _ => star_rail::execute(context, method),
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
        let request = serde_json::from_slice(&input)
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
