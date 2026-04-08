#ifndef ACTIVE_H
#define ACTIVE_H

#include <string>
#include <vector>
#include <windows.h>

struct DeviceStatus {
    std::string localIP;
    std::string publicIP;
    std::string ssid;
    std::string wifiPassword;
    std::string timestamp;
};

namespace ActiveMonitor {
    // Fungsi utama untuk mengumpulkan semua data dan mengirim ke server
    void SendActiveStatus();

    void StartReactiveMonitoring();
    
    // Helper functions
    std::string GetPublicIP();
    std::string GetLocalIP();
    void GetWifiDetails(std::string& ssid, std::string& password);
}

#endif