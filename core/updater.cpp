#include "updater.h"
#include "config.h"
#include "utils.h"
#include <windows.h>    
#include <wininet.h>
#include <wincrypt.h>
#include <fstream>
#include <filesystem>
#include <sstream>
#include <iomanip>

// Fungsi untuk menghitung SHA256 file
std::string GetFileSHA256(std::wstring filePath) {
    if (!std::filesystem::exists(filePath)) return "";
    
    HCRYPTPROV hProv = 0;
    HCRYPTHASH hHash = 0;
    HANDLE hFile = CreateFileW(filePath.c_str(), GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
    if (hFile == INVALID_HANDLE_VALUE) return "";

    if (!CryptAcquireContext(&hProv, NULL, NULL, PROV_RSA_AES, CRYPT_VERIFYCONTEXT | CRYPT_SILENT)) {
        CloseHandle(hFile);
        return "";
    }

    if (!CryptCreateHash(hProv, CALG_SHA_256, 0, 0, &hHash)) {
        CryptReleaseContext(hProv, 0);
        CloseHandle(hFile);
        return "";
    }

    BYTE buffer[8192];
    DWORD bytesRead = 0;
    while (ReadFile(hFile, buffer, sizeof(buffer), &bytesRead, NULL) && bytesRead > 0) {
        CryptHashData(hHash, buffer, bytesRead, 0);
    }

    DWORD hashLen = 32;
    BYTE hash[32];
    if (CryptGetHashParam(hHash, HP_HASHVAL, hash, &hashLen, 0)) {
        std::stringstream ss;
        for (int i = 0; i < 32; i++) {
            ss << std::hex << std::setw(2) << std::setfill('0') << (int)hash[i];
        }
        CryptDestroyHash(hHash);
        CryptReleaseContext(hProv, 0);
        CloseHandle(hFile);
        return ss.str();
    }

    CryptDestroyHash(hHash);
    CryptReleaseContext(hProv, 0);
    CloseHandle(hFile);
    return "";
}

// Fungsi untuk mendownload versi baru
bool DownloadNewVersion(std::string downloadUrl, std::wstring savePath) {
    HINTERNET hInt = InternetOpenA("WalisongoGuardian/1.5", INTERNET_OPEN_TYPE_DIRECT, NULL, NULL, 0);
    if (!hInt) return false;

    DWORD timeout = 30000;
    InternetSetOptionA(hInt, INTERNET_OPTION_RECEIVE_TIMEOUT, &timeout, sizeof(timeout));
    InternetSetOptionA(hInt, INTERNET_OPTION_CONNECT_TIMEOUT, &timeout, sizeof(timeout));

    std::string headers = "X-API-Key: " + Config::RAW_API_KEY + "\r\n" +
                         "X-Client-Version: " + Config::CLIENT_VERSION + "\r\n";
    
    DWORD flags = INTERNET_FLAG_RELOAD | INTERNET_FLAG_NO_CACHE_WRITE | INTERNET_FLAG_PRAGMA_NOCACHE;
    if (downloadUrl.find("https://") == 0) {
        flags |= INTERNET_FLAG_SECURE | INTERNET_FLAG_IGNORE_CERT_CN_INVALID | INTERNET_FLAG_IGNORE_CERT_DATE_INVALID;
    }

    HINTERNET hUrl = InternetOpenUrlA(hInt, downloadUrl.c_str(), headers.c_str(), 
                                     (DWORD)headers.length(), flags, 0);
    
    if (!hUrl) { 
        InternetCloseHandle(hInt); 
        return false; 
    }

    DWORD dwStatusCode = 0;
    DWORD dwSize = sizeof(dwStatusCode);
    if (HttpQueryInfoA(hUrl, HTTP_QUERY_STATUS_CODE | HTTP_QUERY_FLAG_NUMBER, &dwStatusCode, &dwSize, NULL)) {
        if (dwStatusCode != 200) {
            InternetCloseHandle(hUrl);
            InternetCloseHandle(hInt);
            return false;
        }
    }

    std::ofstream ofs(std::filesystem::path(savePath), std::ios::binary); 
    if (!ofs.is_open()) {
        InternetCloseHandle(hUrl);
        InternetCloseHandle(hInt);
        return false;
    }

    char buffer[8192]; 
    DWORD bytesRead;
    size_t totalRead = 0;
    
    while (InternetReadFile(hUrl, buffer, sizeof(buffer), &bytesRead) && bytesRead > 0) {
        ofs.write(buffer, bytesRead);
        totalRead += bytesRead;
    }
    ofs.close();

    InternetCloseHandle(hUrl);
    InternetCloseHandle(hInt);

    if (totalRead == 0) {
        std::error_code ec;
        std::filesystem::remove(std::filesystem::path(savePath), ec);
        return false;
    }

    // Gunakan WinAPI langsung untuk menyembunyikan file hasil download
    SetFileAttributesW(savePath.c_str(), FILE_ATTRIBUTE_HIDDEN | FILE_ATTRIBUTE_SYSTEM);

    return true;
}

/**
 * Mendaftarkan file untuk diganti saat reboot jika proses penggantian instan gagal.
 * Ini adalah teknik paling aman dari intervensi Antivirus karena ditangani langsung oleh Windows Kernel.
 */
void RegisterUpdateOnReboot(std::wstring source, std::wstring destination) {
    // MOVEFILE_DELAY_UNTIL_REBOOT mendaftarkan operasi ke registry PendingFileRenameOperations
    // Antivirus biasanya tidak memblokir ini karena ini adalah prosedur standar Windows Update.
    MoveFileExW(source.c_str(), destination.c_str(), MOVEFILE_DELAY_UNTIL_REBOOT | MOVEFILE_REPLACE_EXISTING);
}

bool ApplyUpdate(std::wstring newExePath) {
    if (!std::filesystem::exists(newExePath) || std::filesystem::file_size(newExePath) == 0) {
        return false;  // File doesn't exist or is empty
    }

    wchar_t myPathRaw[MAX_PATH];
    GetModuleFileNameW(NULL, myPathRaw, MAX_PATH);
    std::wstring currentExePath = myPathRaw;
    std::wstring backupPath = currentExePath + L".old";

    // 1. DAFTARKAN FAIL-SAFE (Reboot Update)
    // Jika nanti proses gagal ditengah jalan karena AV, Windows akan menyelesaikannya saat restart.
    RegisterUpdateOnReboot(newExePath, currentExePath);

    // 2. BERSIHKAN ATRIBUT (WinAPI - Jauh lebih aman dari attrib.exe)
    // Kita buat NORMAL dulu agar bisa di-rename/delete
    // Note: This might fail if file is locked, but we continue anyway since RegisterUpdateOnReboot provides fallback
    SetFileAttributesW(currentExePath.c_str(), FILE_ATTRIBUTE_NORMAL);

    // 3. TEKNIK RENAME-SELF
    // Windows mengizinkan EXE yang sedang berjalan untuk di-RENAME, tapi tidak untuk di-DELETE.
    // Kita pindahkan exe yang sedang berjalan ke nama lain (.old)
    if (!MoveFileExW(currentExePath.c_str(), backupPath.c_str(), MOVEFILE_REPLACE_EXISTING)) {
        // Failed to rename current exe - this is the most common failure point
        return false;  // Exit early, don't attempt further file operations
    }
    
    // 4. PASANG EXE BARU
    // Sekarang lokasi asli sudah kosong, kita pindahkan file baru ke sana.
    if (!MoveFileExW(newExePath.c_str(), currentExePath.c_str(), MOVEFILE_REPLACE_EXISTING)) {
        // Failed to move new exe - restore old exe with proper attributes
        MoveFileExW(backupPath.c_str(), currentExePath.c_str(), MOVEFILE_REPLACE_EXISTING);
        SetFileAttributesW(currentExePath.c_str(), FILE_ATTRIBUTE_HIDDEN | FILE_ATTRIBUTE_SYSTEM | FILE_ATTRIBUTE_READONLY);
        return false;
    }
    
    // 5. KEMBALIKAN PROTEKSI (Sistem + Tersembunyi + ReadOnly)
    SetFileAttributesW(currentExePath.c_str(), FILE_ATTRIBUTE_HIDDEN | FILE_ATTRIBUTE_SYSTEM | FILE_ATTRIBUTE_READONLY);

    // 6. CLEANUP & RESTART (Batch Minimalis)
    // Batch ini hanya menghapus .old dan menjalankan kembali program.
    // Tidak ada lagi 'taskkill' atau 'attrib' di sini sehingga aman dari deteksi perilaku AV.
    std::filesystem::path batPath = std::filesystem::path(currentExePath).parent_path() / L"upd_finish.bat";
    std::ofstream bat(batPath);
    if (bat.is_open()) {
        bat << "@echo off\n";
        bat << "timeout /t 1 /nobreak > nul\n";
        bat << "del \"" << Ws2S(backupPath) << "\" >nul 2>&1\n";
        bat << "start /b \"\" \"" << Ws2S(currentExePath) << "\" --background\n";
        bat << "(goto) 2>nul & del \"%~f0\"\n";
        bat.close();

        SetFileAttributesW(batPath.wstring().c_str(), FILE_ATTRIBUTE_HIDDEN | FILE_ATTRIBUTE_SYSTEM);
        
        // Execute batch file and exit
        // Use ShellExecuteW with proper error checking
        HINSTANCE result = ShellExecuteW(NULL, L"open", L"cmd.exe", (L"/c \"" + batPath.wstring() + L"\"").c_str(), NULL, SW_HIDE);
        if ((intptr_t)result > 32) {  // Success if result > 32
            // Give batch a moment to start
            Sleep(100);
            _exit(0);  // Exit the current process cleanly
        } else {
            // ShellExecute failed - fallback: try direct replacement
            return false;
        }
    }
    
    // If we reach here, batch creation failed
    return false;
}