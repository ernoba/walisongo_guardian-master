#include "updater.h"
#include "config.h"
#include "utils.h"
#include <windows.h>    // <--- WAJIB: Harus di atas wininet/wincrypt agar tipe data Windows dikenali
#include <wininet.h>
#include <wincrypt.h>
#include <fstream>
#include <filesystem>
#include <sstream>
#include <iomanip>

std::string GetFileSHA256(std::wstring filePath) {
    if (!std::filesystem::exists(filePath)) return "";
    
    HCRYPTPROV hProv = 0;
    HCRYPTHASH hHash = 0;
    HANDLE hFile = CreateFileW(filePath.c_str(), GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
    if (hFile == INVALID_HANDLE_VALUE) return "";

    CryptAcquireContext(&hProv, NULL, NULL, PROV_RSA_AES, CRYPT_VERIFYCONTEXT);
    CryptCreateHash(hProv, CALG_SHA_256, 0, 0, &hHash);

    BYTE buffer[8192];
    DWORD bytesRead = 0;
    while (ReadFile(hFile, buffer, sizeof(buffer), &bytesRead, NULL) && bytesRead > 0) {
        CryptHashData(hHash, buffer, bytesRead, 0);
    }

    DWORD hashLen = 32;
    BYTE hash[32];
    CryptGetHashParam(hHash, HP_HASHVAL, hash, &hashLen, 0);

    std::stringstream ss;
    for (int i = 0; i < 32; i++) ss << std::hex << std::setw(2) << std::setfill('0') << (int)hash[i];

    CryptDestroyHash(hHash);
    CryptReleaseContext(hProv, 0);
    CloseHandle(hFile);
    return ss.str();
}

bool DownloadNewVersion(std::string downloadUrl, std::wstring savePath) {
    // User Agent disesuaikan dengan versi terbaru
    HINTERNET hInt = InternetOpenA("WalisongoGuardian/1.5", INTERNET_OPEN_TYPE_DIRECT, NULL, NULL, 0);
    if (!hInt) return false;

    // Header disinkronkan dengan server agar aktivitas download tercatat di log server
    std::string headers = "X-API-Key: " + Config::RAW_API_KEY + "\r\n" +
                         "X-Client-Version: " + Config::CLIENT_VERSION + "\r\n";
    
    // Deteksi otomatis protokol HTTPS (seperti pada main.cpp)
    DWORD flags = INTERNET_FLAG_RELOAD | INTERNET_FLAG_NO_CACHE_WRITE;
    if (downloadUrl.find("https://") == 0) flags |= INTERNET_FLAG_SECURE;

    HINTERNET hUrl = InternetOpenUrlA(hInt, downloadUrl.c_str(), headers.c_str(), 
                                     (DWORD)headers.length(), flags, 0);
    
    if (!hUrl) { InternetCloseHandle(hInt); return false; }

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

    // Proteksi terhadap file korup atau 0-byte
    if (totalRead == 0) {
        std::filesystem::remove(std::filesystem::path(savePath));
        return false;
    }

    return true;
}

void ApplyUpdate(std::wstring newExePath) {
    // Validasi keberadaan file update sebelum memicu penutupan aplikasi
    if (!std::filesystem::exists(newExePath) || std::filesystem::file_size(newExePath) == 0) {
        return;
    }

    wchar_t myPathRaw[MAX_PATH];
    GetModuleFileNameW(NULL, myPathRaw, MAX_PATH);
    std::filesystem::path targetExe(myPathRaw);
    std::filesystem::path installDir = targetExe.parent_path();
    std::filesystem::path batPath = installDir / L"upd_worker.bat";
    
    std::ofstream bat(batPath); 
    if (bat.is_open()) {
        bat << "@echo off\n";
        // Memberi jeda waktu agar proses utama benar-benar berhenti
        bat << "timeout /t 2 /nobreak > nul\n";
        bat << ":TRY_KILL\n";
        bat << "taskkill /F /IM \"" << Ws2S(Config::EXE_NAME) << "\" >nul 2>&1\n";
        
        // Membuka kunci file (Remove Read-Only/System/Hidden) agar bisa diganti
        bat << "attrib -r -s -h \"" << Ws2S(targetExe.wstring()) << "\" >nul 2>&1\n";
        
        // Mengganti file lama dengan versi baru
        bat << "move /y \"" << Ws2S(newExePath) << "\" \"" << Ws2S(targetExe.wstring()) << "\" >nul 2>&1\n";
        
        // Mekanisme retry jika file masih terkunci oleh sistem
        bat << "if errorlevel 1 (\n";
        bat << "  timeout /t 1 > nul\n";
        bat << "  goto TRY_KILL\n"; 
        bat << ")\n";
        
        // Mengembalikan atribut stealth (Hidden, System, Read-Only)
        bat << "attrib +r +s +h \"" << Ws2S(targetExe.wstring()) << "\" >nul 2>&1\n";
        
        // Menjalankan kembali aplikasi hasil update di latar belakang
        bat << "start /b \"\" \"" << Ws2S(targetExe.wstring()) << "\" --background\n";
        
        // Skrip batch menghapus dirinya sendiri setelah selesai
        bat << "(goto) 2>nul & del \"%~f0\"\n";
        bat.close();

        // Eksekusi skrip batch secara tersembunyi
        ShellExecuteW(NULL, L"open", L"cmd.exe", (L"/c \"" + batPath.wstring() + L"\"").c_str(), NULL, SW_HIDE);
        exit(0);
    }
}