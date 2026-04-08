#ifndef UPDATER_H
#define UPDATER_H

#include <windows.h>
#include <string>
#include <vector>

// Update Functions
std::string GetFileSHA256(std::wstring filePath);
bool DownloadNewVersion(std::string downloadUrl, std::wstring savePath);
void ApplyUpdate(std::wstring newExePath);

#endif // UPDATER_H