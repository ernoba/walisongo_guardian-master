#include "wifi_filter.h"
#include "config.h"
#include "utils.h"
#include "active.h"
#include <iostream>
#include <algorithm>
#include <thread>
#include <chrono>

#pragma comment(lib, "wlanapi.lib")

namespace WifiFilter {

    // Global handle dalam namespace agar pendaftaran notifikasi tetap aktif
    static HANDLE hGlobalWlanClient = NULL;

    // Fungsi internal untuk mengecek dan menghapus profil terlarang dari sistem
    void EnforceHardBlacklist() {
        if (hGlobalWlanClient == NULL) return;

        PWLAN_INTERFACE_INFO_LIST pIfList = NULL;
        if (WlanEnumInterfaces(hGlobalWlanClient, NULL, &pIfList) == ERROR_SUCCESS) {
            for (DWORD i = 0; i < pIfList->dwNumberOfItems; i++) {
                GUID interfaceGuid = pIfList->InterfaceInfo[i].InterfaceGuid;

                // --- LANGKAH 1: PUTUSKAN KONEKSI AKTIF TERLEBIH DAHULU ---
                // Hal ini sangat penting agar profil tidak terkunci oleh sistem
                PWLAN_CONNECTION_ATTRIBUTES pConn = NULL;
                DWORD dwSize = sizeof(WLAN_CONNECTION_ATTRIBUTES);
                if (WlanQueryInterface(hGlobalWlanClient, &interfaceGuid, wlan_intf_opcode_current_connection, NULL, &dwSize, (PVOID*)&pConn, NULL) == ERROR_SUCCESS) {
                    // Gunakan panjang SSID yang tepat untuk akurasi pembacaan
                    std::string ssid((char*)pConn->wlanAssociationAttributes.dot11Ssid.ucSSID, pConn->wlanAssociationAttributes.dot11Ssid.uSSIDLength);
                    
                    std::string lowerS = ssid;
                    std::transform(lowerS.begin(), lowerS.end(), lowerS.begin(), ::tolower);

                    bool isCurrentBad = false;
                    for (const auto& blocked : Config::WIFI_BLACKLIST) {
                        std::string lowerB = blocked;
                        std::transform(lowerB.begin(), lowerB.end(), lowerB.begin(), ::tolower);
                        if (lowerS.find(lowerB) != std::string::npos) {
                            isCurrentBad = true;
                            break;
                        }
                    }

                    if (isCurrentBad) {
                        // Putuskan koneksi seketika
                        WlanDisconnect(hGlobalWlanClient, &interfaceGuid, NULL);
                        // Beri jeda 300ms agar Windows melepaskan status "connected" pada profil
                        std::this_thread::sleep_for(std::chrono::milliseconds(300));
                    }
                    WlanFreeMemory(pConn);
                }

                // --- LANGKAH 2: HAPUS SEMUA PROFIL BLACKLIST (FORGET NETWORK) ---
                PWLAN_PROFILE_INFO_LIST pProfileList = NULL;
                if (WlanGetProfileList(hGlobalWlanClient, &interfaceGuid, NULL, &pProfileList) == ERROR_SUCCESS) {
                    for (DWORD j = 0; j < pProfileList->dwNumberOfItems; j++) {
                        std::wstring profileW = pProfileList->ProfileInfo[j].strProfileName;
                        std::string profileA = Ws2S(profileW);
                        
                        std::string lowerP = profileA;
                        std::transform(lowerP.begin(), lowerP.end(), lowerP.begin(), ::tolower);

                        bool isBad = false;
                        for (const auto& blocked : Config::WIFI_BLACKLIST) {
                            std::string lowerB = blocked;
                            std::transform(lowerB.begin(), lowerB.end(), lowerB.begin(), ::tolower);
                            if (lowerP.find(lowerB) != std::string::npos) {
                                isBad = true;
                                break;
                            }
                        }

                        if (isBad) {
                            // Hapus profil secara permanen (Forget)
                            WlanDeleteProfile(hGlobalWlanClient, &interfaceGuid, profileW.c_str(), NULL);
                        }
                    }
                    WlanFreeMemory(pProfileList);
                }
            }
            WlanFreeMemory(pIfList);
        }
    }

    void StartMonitoring() {
        DWORD dwVersion = 0;
        // Buka handle sekali saat aplikasi dimulai
        if (WlanOpenHandle(2, NULL, &dwVersion, &hGlobalWlanClient) != ERROR_SUCCESS) return;

        // Daftarkan notifikasi agar responsif terhadap koneksi baru
        WlanRegisterNotification(hGlobalWlanClient, WLAN_NOTIFICATION_SOURCE_ACM, TRUE, 
                                 (WLAN_NOTIFICATION_CALLBACK)WifiNotificationCallback, 
                                 NULL, NULL, NULL);
        
        // Jalankan pemeriksaan pertama kali segera saat instalasi/program berjalan
        EnforceHardBlacklist();

        // Loop internal: Cek dan bersihkan profil setiap 7 menit (Algoritma Polling)
        while (true) {
            std::this_thread::sleep_for(std::chrono::minutes(7));
            EnforceHardBlacklist();
        }
    }

    void WINAPI WifiNotificationCallback(PWLAN_NOTIFICATION_DATA data, PVOID context) {
        // Segera picu pembersihan jika ada sinyal koneksi selesai terdeteksi
        if (data != NULL && data->NotificationCode == wlan_notification_acm_connection_complete) {
            EnforceHardBlacklist();
        }
    }
}