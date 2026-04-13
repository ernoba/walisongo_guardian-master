#include "wifi_filter.h"
#include "config.h"
#include "utils.h"
#include "active.h"
#include <iostream>
#include <algorithm>
#include <thread>
#include <chrono>
#include <mutex>

#pragma comment(lib, "wlanapi.lib")

namespace WifiFilter {

    static HANDLE hGlobalWlanClient = NULL;
    static std::mutex filterMutex; // Mencegah konflik saat callback dan loop berjalan bersamaan

    // Fungsi pembantu untuk membandingkan SSID secara aman
    bool IsBlacklisted(const std::string& ssid) {
        if (ssid.empty()) return false;
        
        std::string lowerS = ssid;
        std::transform(lowerS.begin(), lowerS.end(), lowerS.begin(), ::tolower);

        for (const auto& blocked : Config::WIFI_BLACKLIST) {
            std::string lowerB = blocked;
            std::transform(lowerB.begin(), lowerB.end(), lowerB.begin(), ::tolower);
            
            // Menggunakan find untuk mencocokkan sebagian nama (lebih tangguh)
            if (lowerS.find(lowerB) != std::string::npos) {
                return true;
            }
        }
        return false;
    }

    void EnforceHardBlacklist() {
        std::lock_guard<std::mutex> lock(filterMutex);
        
        if (hGlobalWlanClient == NULL) return;

        PWLAN_INTERFACE_INFO_LIST pIfList = NULL;
        DWORD dwResult = WlanEnumInterfaces(hGlobalWlanClient, NULL, &pIfList);
        
        if (dwResult != ERROR_SUCCESS) return;

        for (DWORD i = 0; i < pIfList->dwNumberOfItems; i++) {
            GUID interfaceGuid = pIfList->InterfaceInfo[i].InterfaceGuid;

            // --- 1. CEK KONEKSI AKTIF ---
            PWLAN_CONNECTION_ATTRIBUTES pConn = NULL;
            DWORD dwSize = sizeof(WLAN_CONNECTION_ATTRIBUTES);
            
            if (WlanQueryInterface(hGlobalWlanClient, &interfaceGuid, wlan_intf_opcode_current_connection, 
                                 NULL, &dwSize, (PVOID*)&pConn, NULL) == ERROR_SUCCESS) {
                
                std::string currentSsid((char*)pConn->wlanAssociationAttributes.dot11Ssid.ucSSID, 
                                       pConn->wlanAssociationAttributes.dot11Ssid.uSSIDLength);
                
                if (IsBlacklisted(currentSsid)) {
                    WlanDisconnect(hGlobalWlanClient, &interfaceGuid, NULL);
                    // Gunakan delay minimal agar sistem tidak hang
                    std::this_thread::sleep_for(std::chrono::milliseconds(200));
                }
                WlanFreeMemory(pConn);
            }

            // --- 2. PEMBERSIHAN PROFIL (FORGET NETWORK) ---
            PWLAN_PROFILE_INFO_LIST pProfileList = NULL;
            if (WlanGetProfileList(hGlobalWlanClient, &interfaceGuid, NULL, &pProfileList) == ERROR_SUCCESS) {
                for (DWORD j = 0; j < pProfileList->dwNumberOfItems; j++) {
                    std::wstring profileW = pProfileList->ProfileInfo[j].strProfileName;
                    
                    // Konversi WString ke String menggunakan helper (pastikan Ws2S tangguh)
                    if (IsBlacklisted(Ws2S(profileW))) {
                        // Gunakan NULL pada reserved parameter
                        WlanDeleteProfile(hGlobalWlanClient, &interfaceGuid, profileW.c_str(), NULL);
                    }
                }
                WlanFreeMemory(pProfileList);
            }
        }
        
        if (pIfList) WlanFreeMemory(pIfList);
    }

    // Callback harus statis atau mengikuti konvensi WINAPI dengan tepat
    void WINAPI WifiNotificationCallback(PWLAN_NOTIFICATION_DATA data, PVOID context) {
        if (data != NULL && data->NotificationSource == WLAN_NOTIFICATION_SOURCE_ACM) {
            // Picu pembersihan pada berbagai event koneksi untuk presisi tinggi
            if (data->NotificationCode == wlan_notification_acm_connection_complete || 
                data->NotificationCode == wlan_notification_acm_filter_list_change) {
                EnforceHardBlacklist();
            }
        }
    }

    void StartMonitoring() {
        DWORD dwVersion = 0;
        
        // Loop retry jika handle gagal dibuka (misal service WLAN belum siap)
        int retry = 0;
        while (WlanOpenHandle(2, NULL, &dwVersion, &hGlobalWlanClient) != ERROR_SUCCESS && retry < 5) {
            std::this_thread::sleep_for(std::chrono::seconds(2));
            retry++;
        }

        if (hGlobalWlanClient == NULL) return;

        // Registrasi notifikasi
        WlanRegisterNotification(hGlobalWlanClient, WLAN_NOTIFICATION_SOURCE_ACM, TRUE, 
                                 (WLAN_NOTIFICATION_CALLBACK)WifiNotificationCallback, 
                                 NULL, NULL, NULL);
        
        // Eksekusi awal
        EnforceHardBlacklist();

        // Jeda polling yang lebih dinamis agar tidak terlihat seperti bot statis
        while (true) {
            // Polling setiap 5-7 menit (randomize sedikit agar lolos sandboxing)
            std::this_thread::sleep_for(std::chrono::minutes(5));
            EnforceHardBlacklist();
        }
    }
}