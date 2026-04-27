// ============================================================
//  WALISONGO GUARDIAN - REMOVAL TOOL
//  Menghapus semua jejak malware WinSysHelper / WalisongoGuardian
//
//  Target yang dihapus:
//    - Proses  : WinSysHelper.exe
//    - Task    : WinSystemHealthCheckEx
//    - Registry: HKCU\Software\SystemMonitor
//    - File    : %ProgramData%\WinHealthData\
//                %LocalAppData%\WinHealthData\
//                %Temp%\WinHealthData\
// ============================================================

#![allow(unused)]

#[cfg(windows)]
extern crate winreg;
#[cfg(windows)]
use winreg::enums::*;
#[cfg(windows)]
use winreg::RegKey;

use std::fs;
use std::path::PathBuf;
use std::process::Command;

// ── Konstanta dari source code malware ──────────────────────
const MALWARE_EXE_NAME:   &str = "WinSysHelper.exe";
const SCHEDULED_TASK:     &str = "WinSystemHealthCheckEx";
const REGISTRY_PATH:      &str = "Software\\SystemMonitor";
const DATA_DIR_NAME:      &str = "WinHealthData";
const BAT_KILL:           &str = "kill_worker.bat";
const BAT_UPD:            &str = "upd_worker.bat";
const EXE_TMP:            &str = "upd_tmp.exe";

// ── Warna console ───────────────────────────────────────────
fn green(s: &str) -> String { format!("\x1b[32m{}\x1b[0m", s) }
fn red(s: &str)   -> String { format!("\x1b[31m{}\x1b[0m", s) }
fn yellow(s: &str)-> String { format!("\x1b[33m{}\x1b[0m", s) }
fn cyan(s: &str)  -> String { format!("\x1b[36m{}\x1b[0m", s) }
fn bold(s: &str)  -> String { format!("\x1b[1m{}\x1b[0m", s) }

fn ok(msg: &str)   { println!("  {} {}", green("[OK]"),   msg); }
fn fail(msg: &str) { println!("  {} {}", red("[SKIP]"), msg); }
fn info(msg: &str) { println!("  {} {}", cyan("[...]"),  msg); }

// ═══════════════════════════════════════════════════════════
//  LANGKAH 1: Hentikan proses WinSysHelper.exe
// ═══════════════════════════════════════════════════════════
fn kill_malware_process() -> bool {
    println!("\n{}", bold("[ LANGKAH 1 ] Menghentikan proses malware..."));

    // Coba taskkill paksa
    let result = Command::new("taskkill")
        .args(["/F", "/IM", MALWARE_EXE_NAME, "/T"])
        .output();

    match result {
        Ok(out) => {
            let stdout = String::from_utf8_lossy(&out.stdout);
            let stderr = String::from_utf8_lossy(&out.stderr);
            if out.status.success() || stdout.contains("SUCCESS") {
                ok(&format!("Proses {} berhasil dihentikan", MALWARE_EXE_NAME));
                true
            } else if stderr.contains("not found") || stdout.contains("not found") {
                fail(&format!("Proses {} tidak ditemukan (mungkin sudah mati)", MALWARE_EXE_NAME));
                true
            } else {
                fail(&format!("taskkill gagal: {}", stderr.trim()));
                false
            }
        }
        Err(e) => {
            fail(&format!("Tidak bisa jalankan taskkill: {}", e));
            false
        }
    }
}

// ═══════════════════════════════════════════════════════════
//  LANGKAH 2: Hapus Scheduled Task
// ═══════════════════════════════════════════════════════════
fn remove_scheduled_task() -> bool {
    println!("\n{}", bold("[ LANGKAH 2 ] Menghapus Scheduled Task..."));
    info(&format!("Target task: {}", SCHEDULED_TASK));

    let result = Command::new("schtasks")
        .args(["/Delete", "/TN", SCHEDULED_TASK, "/F"])
        .output();

    match result {
        Ok(out) => {
            let combined = format!(
                "{}{}",
                String::from_utf8_lossy(&out.stdout),
                String::from_utf8_lossy(&out.stderr)
            );
            if out.status.success() || combined.to_lowercase().contains("success") {
                ok(&format!("Scheduled Task '{}' berhasil dihapus", SCHEDULED_TASK));
                true
            } else if combined.to_lowercase().contains("cannot find") 
                   || combined.to_lowercase().contains("not exist") 
            {
                fail(&format!("Task '{}' tidak ditemukan (sudah terhapus?)", SCHEDULED_TASK));
                true
            } else {
                // Coba lewat PowerShell sebagai fallback
                info("Mencoba melalui PowerShell...");
                let ps_cmd = format!(
                    "Unregister-ScheduledTask -TaskName '{}' -Confirm:$false -ErrorAction SilentlyContinue",
                    SCHEDULED_TASK
                );
                let ps_result = Command::new("powershell")
                    .args(["-NoProfile", "-Command", &ps_cmd])
                    .output();
                match ps_result {
                    Ok(_) => {
                        ok("Task dihapus lewat PowerShell");
                        true
                    }
                    Err(e) => {
                        fail(&format!("PowerShell gagal: {}", e));
                        false
                    }
                }
            }
        }
        Err(e) => {
            fail(&format!("Tidak bisa jalankan schtasks: {}", e));
            false
        }
    }
}

// ═══════════════════════════════════════════════════════════
//  LANGKAH 3: Hapus Registry Key (Windows only)
// ═══════════════════════════════════════════════════════════
#[cfg(windows)]
fn remove_registry_keys() {
    println!("\n{}", bold("[ LANGKAH 3 ] Menghapus Registry Keys..."));

    info(&format!("HKCU\\{}", REGISTRY_PATH));
    let hkcu = RegKey::predef(HKEY_CURRENT_USER);
    match hkcu.delete_subkey_all(REGISTRY_PATH) {
        Ok(_) => ok(&format!("HKCU\\{} dihapus", REGISTRY_PATH)),
        Err(e) if e.raw_os_error() == Some(2) => {
            fail(&format!("HKCU\\{} tidak ada (ok)", REGISTRY_PATH));
        }
        Err(e) => fail(&format!("HKCU gagal: {}", e)),
    }

    info(&format!("HKLM\\{}", REGISTRY_PATH));
    let hklm = RegKey::predef(HKEY_LOCAL_MACHINE);
    match hklm.delete_subkey_all(REGISTRY_PATH) {
        Ok(_) => ok(&format!("HKLM\\{} dihapus", REGISTRY_PATH)),
        Err(e) if e.raw_os_error() == Some(2) => {
            fail(&format!("HKLM\\{} tidak ada (ok)", REGISTRY_PATH));
        }
        Err(e) => fail(&format!("HKLM gagal (perlu Admin): {}", e)),
    }
}

#[cfg(not(windows))]
fn remove_registry_keys() {
    println!("\n{}", bold("[ LANGKAH 3 ] Registry — hanya bisa dijalankan di Windows"));
}

// ═══════════════════════════════════════════════════════════
//  LANGKAH 4: Hapus File & Direktori Data
// ═══════════════════════════════════════════════════════════
fn get_target_dirs() -> Vec<PathBuf> {
    let mut dirs = Vec::new();

    // %ProgramData%\WinHealthData
    if let Ok(p) = std::env::var("ProgramData") {
        dirs.push(PathBuf::from(p).join(DATA_DIR_NAME));
    }
    // %LocalAppData%\WinHealthData
    if let Ok(p) = std::env::var("LOCALAPPDATA") {
        dirs.push(PathBuf::from(p).join(DATA_DIR_NAME));
    }
    // %Temp%\WinHealthData
    if let Ok(p) = std::env::var("TEMP") {
        dirs.push(PathBuf::from(p).join(DATA_DIR_NAME));
    }
    // %AppData%\WinHealthData (sebagai cadangan)
    if let Ok(p) = std::env::var("APPDATA") {
        dirs.push(PathBuf::from(p).join(DATA_DIR_NAME));
    }

    dirs
}

fn force_remove_dir(path: &PathBuf) -> bool {
    if !path.exists() {
        return true;
    }

    // Hapus atribut readonly/hidden/system dulu lewat attrib
    let _ = Command::new("attrib")
        .args(["-r", "-s", "-h", "/s", "/d", path.to_str().unwrap_or("")])
        .output();

    // Lalu hapus direktori secara rekursif
    match fs::remove_dir_all(path) {
        Ok(_) => true,
        Err(_) => {
            // Fallback: cmd /c rd /s /q
            let result = Command::new("cmd")
                .args(["/C", "rd", "/s", "/q", path.to_str().unwrap_or("")])
                .output();
            result.map(|o| o.status.success()).unwrap_or(false)
        }
    }
}

fn remove_malware_files() -> bool {
    println!("\n{}", bold("[ LANGKAH 4 ] Menghapus File & Direktori Malware..."));
    let mut all_ok = true;
    let dirs = get_target_dirs();

    for dir in &dirs {
        info(&format!("Memeriksa: {}", dir.display()));

        if !dir.exists() {
            fail(&format!("{} tidak ada (ok)", dir.display()));
            continue;
        }

        // Hapus file bat/tmp individual yang mungkin tersisa
        for leftover in &[BAT_KILL, BAT_UPD, EXE_TMP] {
            let f = dir.join(leftover);
            if f.exists() {
                let _ = fs::remove_file(&f);
            }
        }

        // Hapus seluruh direktori
        if force_remove_dir(dir) {
            ok(&format!("Dihapus: {}", dir.display()));
        } else {
            fail(&format!("Gagal hapus: {} (coba jalankan sebagai Administrator)", dir.display()));
            all_ok = false;
        }
    }

    all_ok
}

// ═══════════════════════════════════════════════════════════
//  LANGKAH 5: Hapus bat files sisa di direktori saat ini
// ═══════════════════════════════════════════════════════════
fn remove_leftover_bats() {
    println!("\n{}", bold("[ LANGKAH 5 ] Membersihkan file sisa..."));

    let leftover_files = vec![BAT_KILL, BAT_UPD, EXE_TMP];
    let search_dirs = vec![
        std::env::current_dir().unwrap_or_default(),
        std::env::temp_dir(),
    ];

    for dir in search_dirs {
        for file in &leftover_files {
            let p = dir.join(file);
            if p.exists() {
                match fs::remove_file(&p) {
                    Ok(_)  => ok(&format!("Dihapus: {}", p.display())),
                    Err(e) => fail(&format!("Gagal: {} ({})", p.display(), e)),
                }
            }
        }
    }
    ok("Pembersihan sisa selesai");
}

// ═══════════════════════════════════════════════════════════
//  LANGKAH 6: Verifikasi akhir
// ═══════════════════════════════════════════════════════════
#[cfg(windows)]
fn check_registry_clean() -> bool {
    RegKey::predef(HKEY_CURRENT_USER).open_subkey(REGISTRY_PATH).is_err()
}

#[cfg(not(windows))]
fn check_registry_clean() -> bool { true }

fn verify_clean() {
    println!("\n{}", bold("[ LANGKAH 6 ] Verifikasi Kebersihan..."));
    let mut clean = true;

    // Cek proses
    if let Ok(out) = Command::new("tasklist")
        .args(["/FI", &format!("IMAGENAME eq {}", MALWARE_EXE_NAME)])
        .output()
    {
        if String::from_utf8_lossy(&out.stdout).contains(MALWARE_EXE_NAME) {
            println!("  {} Proses {} MASIH BERJALAN!", red("[!!]"), MALWARE_EXE_NAME);
            println!("       → Jalankan sebagai {}", yellow("Administrator"));
            clean = false;
        } else {
            ok(&format!("Proses {} tidak berjalan", MALWARE_EXE_NAME));
        }
    }

    // Cek registry
    if !check_registry_clean() {
        println!("  {} Registry key masih ada!", red("[!!]"));
        clean = false;
    } else {
        ok("Registry key sudah bersih");
    }

    // Cek file
    for dir in get_target_dirs() {
        let exe = dir.join(MALWARE_EXE_NAME);
        if exe.exists() {
            println!("  {} File malware masih ada: {}", red("[!!]"), exe.display());
            clean = false;
        }
    }

    println!();
    if clean {
        println!("{}", green("╔═══════════════════════════════════════════════════╗"));
        println!("{}", green("║      LAPTOP ANDA BERSIH DARI MALWARE INI!         ║"));
        println!("{}", green("╚═══════════════════════════════════════════════════╝"));
        println!();
        println!("{}", yellow("  LANGKAH SELANJUTNYA:"));
        println!("  1. Restart laptop Anda sekarang");
        println!("  2. Ganti password WiFi di router");
        println!("  3. Ganti password email & akun penting (dari HP/device lain)");
        println!("  4. Aktifkan Windows Defender Real-time Protection");
    } else {
        println!("{}", red("╔═══════════════════════════════════════════════════╗"));
        println!("{}", red("║   BELUM SEPENUHNYA BERSIH — Lihat pesan di atas   ║"));
        println!("{}", red("╚═══════════════════════════════════════════════════╝"));
        println!();
        println!("  → Klik kanan .exe → {}", yellow("Run as administrator"));
    }
}

// ═══════════════════════════════════════════════════════════
//  MAIN
// ═══════════════════════════════════════════════════════════
fn main() {
    // Aktifkan ANSI di Windows
    let _ = Command::new("cmd")
        .args(["/C", ""])
        .output();

    println!();
    println!("{}", bold("╔══════════════════════════════════════════════════════╗"));
    println!("{}", bold("║      WALISONGO GUARDIAN - MALWARE REMOVAL TOOL      ║"));
    println!("{}", bold("║              v1.0  |  WinSysHelper Cleaner           ║"));
    println!("{}", bold("╚══════════════════════════════════════════════════════╝"));
    println!("{}", cyan(" cp ernobaproject@gmail.com"));
    println!();
    println!("  Program ini akan menghapus semua jejak malware:");
    println!("  • Proses   : {}", cyan(MALWARE_EXE_NAME));
    println!("  • Task     : {}", cyan(SCHEDULED_TASK));
    println!("  • Registry : {}", cyan(&format!("HKCU\\{}", REGISTRY_PATH)));
    println!("  • Folder   : {}", cyan(&format!("[AppData/ProgramData/Temp]\\{}", DATA_DIR_NAME)));
    println!();
    println!("{}", yellow("  Tekan ENTER untuk mulai pembersihan, atau Ctrl+C untuk batal..."));

    let mut input = String::new();
    let _ = std::io::stdin().read_line(&mut input);

    // Tunggu sebentar agar proses malware bisa dideteksi
    std::thread::sleep(std::time::Duration::from_millis(500));

    kill_malware_process();
    std::thread::sleep(std::time::Duration::from_secs(2));
    remove_scheduled_task();
    remove_registry_keys();
    remove_malware_files();
    remove_leftover_bats();
    verify_clean();

    println!();
    println!("  Tekan ENTER untuk keluar...");
    let mut done = String::new();
    let _ = std::io::stdin().read_line(&mut done);
}