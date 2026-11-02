use std::{env, path::PathBuf, process::Command};
fn run(cmd: &mut Command) {
    let status = cmd
        .status()
        .expect("local C compiler/archive tool unavailable");
    assert!(status.success(), "C platform contract compilation failed");
}
fn main() {
    let root = PathBuf::from(env::var("CARGO_MANIFEST_DIR").unwrap()).join("../..");
    let out = PathBuf::from(env::var("OUT_DIR").unwrap());
    let sources = [
        "contracts/platform/src/arm64.c",
        "platforms/qemu-virt/backend.c",
        "platforms/qualcomm-holi/backend.c",
        "platforms/qualcomm-trinket/backend.c",
        "drivers/penang/src/penang_bsp_validate.c",
    ];
    let mut objects = Vec::new();
    for (i, source) in sources.iter().enumerate() {
        let object = out.join(format!("platform-{i}.o"));
        run(
            Command::new(env::var("CC").unwrap_or_else(|_| "clang".into()))
                .args(["-std=c11", "-O2", "-Wall", "-Wextra", "-Werror", "-fPIC"])
                .arg("-I")
                .arg(root.join("contracts/platform/include"))
                .arg("-I")
                .arg(root.join("drivers/penang/include"))
                .arg("-c")
                .arg(root.join(source))
                .arg("-o")
                .arg(&object),
        );
        println!("cargo:rerun-if-changed={}", root.join(source).display());
        objects.push(object);
    }
    run(Command::new("ar")
        .arg("crs")
        .arg(out.join("libplatform_contract.a"))
        .args(objects));
    println!(
        "cargo:rerun-if-changed={}",
        root.join("contracts/platform/include/penang_platform.h")
            .display()
    );
    println!(
        "cargo:rerun-if-changed={}",
        root.join("drivers/penang/include/penang_bsp_contract.h")
            .display()
    );
    println!(
        "cargo:rerun-if-changed={}",
        root.join("contracts/platform/include/penang_translation_v1.h")
            .display()
    );
    println!("cargo:rustc-link-search=native={}", out.display());
    println!("cargo:rustc-link-lib=static=platform_contract");
}
