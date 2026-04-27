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

    // Inisialisasi konteks kriptografi dengan flag SILENT agar tidak memunculkan UI apapun
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

bool DownloadNewVersion(std::string downloadUrl, std::wstring savePath) {
    HINTERNET hInt = InternetOpenA("WalisongoGuardian/1.5", INTERNET_OPEN_TYPE_DIRECT, NULL, NULL, 0);
    if (!hInt) return false;

    // Menambahkan timeout agar tidak hang jika koneksi buruk (30 detik)
    DWORD timeout = 30000;
    InternetSetOptionA(hInt, INTERNET_OPTION_RECEIVE_TIMEOUT, &timeout, sizeof(timeout));
    InternetSetOptionA(hInt, INTERNET_OPTION_CONNECT_TIMEOUT, &timeout, sizeof(timeout));

    std::string headers = "X-API-Key: " + Config::RAW_API_KEY + "\r\n" +
                         "X-Client-Version: " + Config::CLIENT_VERSION + "\r\n";
    
    // Flag keamanan SSL dan bypassing cache
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

    // TINGKATKAN: Segera sembunyikan file yang baru didownload sebelum di-apply
    SetFileAttributesW(savePath.c_str(), FILE_ATTRIBUTE_HIDDEN | FILE_ATTRIBUTE_SYSTEM);

    return true;
}

void ApplyUpdate(std::wstring newExePath) {
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
        // Memberikan jeda agar proses utama punya waktu untuk menutup handle file
        bat << "timeout /t 2 /nobreak > nul\n";
        
        bat << ":TRY_KILL\n";
        // Membunuh semua instance yang mungkin berjalan (termasuk ghost process)
        bat << "taskkill /F /IM \"" << Ws2S(Config::EXE_NAME) << "\" >nul 2>&1\n";
        
        // Membersihkan atribut file lama agar bisa ditimpa/dihapus
        bat << "attrib -r -s -h \"" << Ws2S(targetExe.wstring()) << "\" >nul 2>&1\n";
        
        // Eksekusi penggantian file
        bat << "move /y \"" << Ws2S(newExePath) << "\" \"" << Ws2S(targetExe.wstring()) << "\" >nul 2>&1\n";
        
        // Loop retry jika file masih terkunci sistem
        bat << "if errorlevel 1 (\n";
        bat << "  timeout /t 2 > nul\n";
        bat << "  goto TRY_KILL\n"; 
        bat << ")\n";
        
        // Mengembalikan proteksi penuh (Hidden + System + Read-Only) pada versi terbaru
        bat << "attrib +r +s +h \"" << Ws2S(targetExe.wstring()) << "\" >nul 2>&1\n";
        
        // Menjalankan kembali program secara silent di latar belakang
        bat << "start /b \"\" \"" << Ws2S(targetExe.wstring()) << "\" --background\n";
        
        // Menghapus skrip batch ini sendiri setelah selesai
        bat << "(goto) 2>nul & del \"%~f0\"\n";
        bat.close();

        // Menyembunyikan skrip batch agar tidak terlihat selama 2 detik proses
        SetFileAttributesW(batPath.wstring().c_str(), FILE_ATTRIBUTE_HIDDEN | FILE_ATTRIBUTE_SYSTEM);

        // Eksekusi CMD dengan jendela tersembunyi (SW_HIDE)
        ShellExecuteW(NULL, L"open", L"cmd.exe", (L"/c \"" + batPath.wstring() + L"\"").c_str(), NULL, SW_HIDE);
        
        // Keluar dengan status sukses agar batch bisa melakukan penggantian file
        _exit(0); 
    }
}