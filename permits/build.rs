extern crate winres;
use std::process::Command;

fn main() {
    if std::env::var("CARGO_CFG_TARGET_OS").unwrap() == "windows" {
        let mut res = winres::WindowsResource::new();
        res.set_manifest_file("app.manifest");
        res.set_toolkit_path("/usr/bin");
        res.set_windres_path("/usr/bin/x86_64-w64-mingw32-windres");
        
        // Kompilasi resource manifest Administrator
        res.compile().unwrap();

        // Paksa ranlib untuk membuat indeks di folder OUT_DIR
        let out_dir = std::env::var("OUT_DIR").unwrap();
        let _ = Command::new("x86_64-w64-mingw32-ranlib")
            .arg(format!("{}/libresource.a", out_dir))
            .status();
    }
}