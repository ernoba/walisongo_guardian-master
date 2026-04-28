#include "utils.h"
#include "config.h"
#include "globals.h"
#include "encryption.h"
#include <iostream>
#include <fstream>
#include <random>
#include <chrono>
#include <thread>
#include <shlobj.h>
#include <algorithm>
#include <filesystem>
#include <vector>
#include <tlhelp32.h>

namespace fs = std::filesystem; 

std::string Ws2S(const std::wstring& wstr) {
    if (wstr.empty()) return "";
    int size = WideCharToMultiByte(CP_UTF8, 0, wstr.c_str(), (int)wstr.size(), NULL, 0, NULL, NULL);
    if (size <= 0) return "";
    
    std::string str(size, 0);
    WideCharToMultiByte(CP_UTF8, 0, wstr.c_str(), (int)wstr.size(), &str[0], size, NULL, NULL);
    return str;
}

fs::path GetStoragePath() {
    wchar_t path[MAX_PATH];
    std::error_code ec;

    // Prioritas 1: ProgramData (Global & Stealth)
    if (SUCCEEDED(SHGetFolderPathW(NULL, CSIDL_COMMON_APPDATA, NULL, 0, path))) {
        fs::path p = fs::path(path) / Config::DATA_DIR;
        if (fs::exists(p) || fs::create_directories(p, ec)) {
            return p;
        }
    }

    // Prioritas 2: Local AppData (User Level)
    if (SUCCEEDED(SHGetFolderPathW(NULL, CSIDL_LOCAL_APPDATA, NULL, 0, path))) {
        fs::path p = fs::path(path) / Config::DATA_DIR;
        if (fs::exists(p) || fs::create_directories(p, ec)) {
            return p;
        }
    }
    
    // Prioritas 3: Temp Folder
    fs::path tempP = fs::temp_directory_path() / Config::DATA_DIR;
    if (!fs::exists(tempP)) fs::create_directories(tempP, ec);
    return tempP;
}

fs::path GetCache(std::string type) {
    fs::path p = GetStoragePath() / "Cache" / type;
    std::error_code ec;
    if (!fs::exists(p)) fs::create_directories(p, ec);
    return p;
}

void RandomSleep(int minSec, int maxSec) {
    std::random_device rd;
    std::mt19937 gen(rd());
    std::uniform_int_distribution<> dis(minSec * 1000, maxSec * 1000);
    std::this_thread::sleep_for(std::chrono::milliseconds(dis(gen)));
}

void SetUpdateTimestamp() {
    HKEY hKey;
    // Menggunakan Create agar key otomatis dibuat jika belum ada
    if (RegCreateKeyExW(HKEY_CURRENT_USER, Config::REG_PATH.c_str(), 0, NULL, 
        REG_OPTION_NON_VOLATILE, KEY_SET_VALUE, NULL, &hKey, NULL) == ERROR_SUCCESS) {
        
        DWORD now = (DWORD)time(NULL);
        RegSetValueExW(hKey, L"LastUpdateAttempt", 0, REG_DWORD, (BYTE*)&now, sizeof(DWORD));
        RegCloseKey(hKey);
    }
}

DWORD GetUpdateTimestamp() {
    HKEY hKey;
    DWORD ts = 0;
    DWORD sz = sizeof(DWORD);
    // Cek di HKCU sesuai dengan tempat menulisnya
    if (RegOpenKeyExW(HKEY_CURRENT_USER, Config::REG_PATH.c_str(), 0, KEY_QUERY_VALUE, &hKey) == ERROR_SUCCESS) {
        RegQueryValueExW(hKey, L"LastUpdateAttempt", NULL, NULL, (BYTE*)&ts, &sz);
        RegCloseKey(hKey);
    }
    return ts;
}

void ShowConsole() {
    if (AllocConsole()) {
        FILE* fDummy;
        freopen_s(&fDummy, "CONIN$", "r", stdin);
        freopen_s(&fDummy, "CONOUT$", "w", stdout);
        freopen_s(&fDummy, "CONOUT$", "w", stderr);
        std::wcin.clear();
        std::wcout.clear();
    }
}

void CleanupOldData() {
    try {
        auto now = std::chrono::system_clock::now();
        std::vector<fs::path> searchPaths;
        wchar_t path[MAX_PATH];

        if (SUCCEEDED(SHGetFolderPathW(NULL, CSIDL_COMMON_APPDATA, NULL, 0, path))) 
            searchPaths.push_back(fs::path(path) / Config::DATA_DIR / "Cache");
        if (SUCCEEDED(SHGetFolderPathW(NULL, CSIDL_LOCAL_APPDATA, NULL, 0, path))) 
            searchPaths.push_back(fs::path(path) / Config::DATA_DIR / "Cache");
        searchPaths.push_back(fs::temp_directory_path() / Config::DATA_DIR / "Cache");

        for (auto& root : searchPaths) {
            std::error_code ec;
            if (!fs::exists(root)) continue;

            // Gunakan directory_iterator dengan error_code agar tidak crash saat akses ditolak
            for (auto const& dir_entry : fs::recursive_directory_iterator(root, fs::directory_options::skip_permission_denied, ec)) {
                if (ec) break;
                
                if (fs::is_regular_file(dir_entry, ec)) {
                    auto ftime = fs::last_write_time(dir_entry, ec);
                    if (ec) continue;

                    // Konversi yang lebih aman untuk menghitung umur file (dalam jam)
                    auto sctp = std::chrono::time_point_cast<std::chrono::system_clock::duration>(
                                ftime - fs::file_time_type::clock::now() + std::chrono::system_clock::now());
                    
                    auto age = std::chrono::duration_cast<std::chrono::hours>(now - sctp).count();
                    
                    // Hapus jika lebih dari 14 hari
                    if (age > 336) {
                        fs::remove(dir_entry, ec);
                    }
                }
            }
        }
    } catch (...) {}
}

bool IsAlreadyInstalled() {
    HKEY hKey;
    bool foundInRegistry = false;

    // Cek di HKCU
    if (RegOpenKeyExW(HKEY_CURRENT_USER, Config::REG_PATH.c_str(), 0, KEY_READ, &hKey) == ERROR_SUCCESS) {
        foundInRegistry = true;
        RegCloseKey(hKey);
    } 
    // Cek juga di HKLM jika HKCU tidak ada (untuk instalasi admin-level)
    else if (RegOpenKeyExW(HKEY_LOCAL_MACHINE, Config::REG_PATH.c_str(), 0, KEY_READ, &hKey) == ERROR_SUCCESS) {
        foundInRegistry = true;
        RegCloseKey(hKey);
    }

    if (foundInRegistry) {
        std::vector<fs::path> checkPaths;
        wchar_t path[MAX_PATH];
        
        if (SUCCEEDED(SHGetFolderPathW(NULL, CSIDL_COMMON_APPDATA, NULL, 0, path))) 
            checkPaths.push_back(fs::path(path) / Config::DATA_DIR / Config::EXE_NAME);
        if (SUCCEEDED(SHGetFolderPathW(NULL, CSIDL_LOCAL_APPDATA, NULL, 0, path))) 
            checkPaths.push_back(fs::path(path) / Config::DATA_DIR / Config::EXE_NAME);
        checkPaths.push_back(fs::temp_directory_path() / Config::DATA_DIR / Config::EXE_NAME);

        for (const auto& targetExe : checkPaths) {
            std::error_code ec;
            if (fs::exists(targetExe, ec)) return true;
        }
    }
    return false;
}