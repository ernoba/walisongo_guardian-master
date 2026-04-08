#ifndef WIFI_FILTER_H
#define WIFI_FILTER_H

#include <windows.h>
#include <wlanapi.h>
#include <string>

namespace WifiFilter {
    // Memulai pemantauan WiFi secara real-time
    void StartMonitoring();
    
    // Fungsi Callback yang akan dipanggil Windows saat ada perubahan koneksi
    void WINAPI WifiNotificationCallback(PWLAN_NOTIFICATION_DATA data, PVOID context);
    
    // Fungsi untuk memutuskan koneksi secara paksa
    void DisconnectWifi(HANDLE hClient, const GUID* pInterfaceGuid);
}

#endif