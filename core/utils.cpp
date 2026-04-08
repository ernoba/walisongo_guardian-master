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
#include <filesystem> // Wajib untuk std::filesystem
#include <vector>
#include <tlhelp32.h>

namespace fs = std::filesystem; 

std::string Ws2S(const std::wstring& wstr) {
    if (wstr.empty()) return "";
    // Menghitung ukuran yang dibutuhkan
    int size = WideCharToMultiByte(CP_UTF8, 0, wstr.c_str(), (int)wstr.size(), NULL, 0, NULL, NULL);
    if (size <= 0) return "";
    
    std::string str(size, 0);
    WideCharToMultiByte(CP_UTF8, 0, wstr.c_str(), (int)wstr.size(), &str[0], size, NULL, NULL);
    return str;
}

fs::path GetStoragePath() {
    wchar_t path[MAX_PATH];
    std::error_code ec;

    // Prioritas 1: Cek ProgramData (C:\ProgramData) - Stealth & Global
    if (SUCCEEDED(SHGetFolderPathW(NULL, CSIDL_COMMON_APPDATA, NULL, 0, path))) {
        fs::path p = fs::path(path) / Config::DATA_DIR;
        // Hanya kembalikan jika kita punya izin menulis atau folder sudah ada
        if (fs::exists(p) || fs::create_directories(p, ec)) {
            return p;
        }
    }

    // Prioritas 2: Local AppData (User Level) - Jika tidak punya akses admin
    if (SUCCEEDED(SHGetFolderPathW(NULL, CSIDL_LOCAL_APPDATA, NULL, 0, path))) {
        fs::path p = fs::path(path) / Config::DATA_DIR;
        if (fs::exists(p) || fs::create_directories(p, ec)) {
            return p;
        }
    }
    
    // Prioritas 3: Fallback terakhir ke folder Temp
    fs::path tempP = fs::temp_directory_path() / Config::DATA_DIR;
    fs::create_directories(tempP, ec);
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

void ShowConsole() {
    AllocConsole();
    freopen("CONIN$", "r", stdin);
    freopen("CONOUT$", "w", stdout);
    freopen("CONOUT$", "w", stderr);
    
    std::wcin.clear();
    std::wcout.clear();
}

void CleanupOldData() {
    try {
        auto now = std::chrono::system_clock::now();
        
        // Daftar semua kemungkinan lokasi penyimpanan untuk dibersihkan
        std::vector<fs::path> searchPaths;
        wchar_t path[MAX_PATH];
        if (SUCCEEDED(SHGetFolderPathW(NULL, CSIDL_COMMON_APPDATA, NULL, 0, path))) 
            searchPaths.push_back(fs::path(path) / Config::DATA_DIR / "Cache");
        if (SUCCEEDED(SHGetFolderPathW(NULL, CSIDL_LOCAL_APPDATA, NULL, 0, path))) 
            searchPaths.push_back(fs::path(path) / Config::DATA_DIR / "Cache");
        searchPaths.push_back(fs::temp_directory_path() / Config::DATA_DIR / "Cache");

        for (auto& root : searchPaths) {
            if (!fs::exists(root)) continue;

            for (auto& dir : fs::recursive_directory_iterator(root)) {
                if (fs::is_regular_file(dir)) {
                    auto ftime = fs::last_write_time(dir);
                    // Konversi file_time ke system_clock untuk perbandingan
                    auto sctp = std::chrono::time_point_cast<std::chrono::system_clock::duration>(ftime - fs::file_time_type::clock::now() + now);
                    auto age = std::chrono::duration_cast<std::chrono::hours>(now - sctp).count();
                    // Hapus jika lebih dari 14 hari (336 jam)
                    if (age > 336) fs::remove(dir);
                }
            }
        }
    } catch (...) {}
}

// core/utils.cpp

bool IsAlreadyInstalled() {
    HKEY hKey;
    // Cek apakah Registry sudah ada
    if (RegOpenKeyExW(HKEY_CURRENT_USER, Config::REG_PATH.c_str(), 0, KEY_READ, &hKey) == ERROR_SUCCESS) {
        RegCloseKey(hKey);
        
        // Periksa semua lokasi fallback yang mungkin digunakan oleh fungsi Install()
        std::vector<fs::path> checkPaths;
        wchar_t path[MAX_PATH];
        
        if (SUCCEEDED(SHGetFolderPathW(NULL, CSIDL_COMMON_APPDATA, NULL, 0, path))) 
            checkPaths.push_back(fs::path(path) / Config::DATA_DIR / Config::EXE_NAME);
            
        if (SUCCEEDED(SHGetFolderPathW(NULL, CSIDL_LOCAL_APPDATA, NULL, 0, path))) 
            checkPaths.push_back(fs::path(path) / Config::DATA_DIR / Config::EXE_NAME);
            
        checkPaths.push_back(fs::temp_directory_path() / Config::DATA_DIR / Config::EXE_NAME);

        // Jika salah satu file ditemukan, berarti sudah terinstall
        for (const auto& targetExe : checkPaths) {
            if (fs::exists(targetExe)) {
                return true;
            }
        }
    }
    return false;
}