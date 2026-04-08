#ifndef SYSTEM_MONITOR_H
#define SYSTEM_MONITOR_H

#include <windows.h>
#include <vector>
#include <string>

class SystemMonitor {
public:
    static std::vector<BYTE> TakeScreenshot();
    static std::string GetActiveWindowTitle();
};

// Helper function untuk GDI+
int GetEncoderClsid(const WCHAR* format, CLSID* pClsid);

#endif // SYSTEM_MONITOR_H