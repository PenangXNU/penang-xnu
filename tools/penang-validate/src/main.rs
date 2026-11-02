// SPDX-License-Identifier: Apache-2.0
use serde_json::json;
use std::{path::PathBuf, process};
fn main() {
    let args: Vec<String> = std::env::args().skip(1).collect();
    let mut root = PathBuf::from(".");
    let mut platform = "qemu-virt".to_string();
    let mut profile = None;
    let mut observation = None;
    let mut contract = false;
    let mut i = 0;
    while i < args.len() {
        if args[i] == "--contract-only" {
            contract = true;
            i += 1;
            continue;
        }
        if args[i] == "--help" {
            println!("penang-validate --root REPO --platform ID [--profile RELATIVE_JSON] [--observation JSON --contract-only]");
            return;
        }
        if i + 1 >= args.len() {
            println!(
                "{}",
                json!({"status":"FAIL","checks":[{"status":"FAIL","code":"ARGUMENT","message":"missing argument value"}]})
            );
            process::exit(1);
        }
        match args[i].as_str() {
            "--root" => root = PathBuf::from(&args[i + 1]),
            "--platform" => platform = args[i + 1].clone(),
            "--profile" => profile = Some(args[i + 1].clone()),
            "--observation" => observation = Some(PathBuf::from(&args[i + 1])),
            _ => {
                println!(
                    "{}",
                    json!({"status":"FAIL","checks":[{"status":"FAIL","code":"ARGUMENT","message":"unknown argument"}]})
                );
                process::exit(1);
            }
        }
        i += 2;
    }
    let profile = profile.unwrap_or_else(|| format!("platforms/{platform}/profile.json"));
    let result =
        penang_validate::evaluate(&root, &platform, &profile, observation.as_deref(), contract);
    println!("{}", serde_json::to_string_pretty(&result).unwrap());
    process::exit(match result["status"].as_str() {
        Some("PASS") => 0,
        Some("BLOCKED") => 2,
        Some("NOT_RUN") => 3,
        _ => 1,
    });
}
