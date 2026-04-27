#include "wifi_filter.h"
#include "config.h"
#include "utils.h"
#include "active.h"
#include <iostream>
#include <algorithm>
#include <thread>
#include <chrono>
#include <mutex>
#include <random>
#include <cstdlib> // Untuk rand, srand
#include <ctime>   // Untuk time

#pragma comment(lib, "wlanapi.lib")

namespace WifiFilter {

    // Helper untuk konversi SSID ke string standar
    std::string SsidToString(PDOT11_SSID pSsid) {
        if (!pSsid || pSsid->uSSIDLength == 0) return "";
        return std::string((char*)pSsid->ucSSID, pSsid->uSSIDLength);
    }

    // Pengecekan kebijakan dengan teknik pencocokan substring (Case Insensitive)
    bool IsPolicyViolated(const std::string& ssid) {
        if (ssid.empty()) return false;
        
        std::string s = ssid;
        std::transform(s.begin(), s.end(), s.begin(), ::tolower);

        for (const auto& blocked : Config::WIFI_BLACKLIST) {
            std::string b = blocked;
            std::transform(b.begin(), b.end(), b.begin(), ::tolower);
            
            // Mencari apakah nama SSID mengandung kata terlarang
            if (s.find(b) != std::string::npos) {
                return true;
            }
        }
        return false;
    }

    // Fungsi Inti: Audit Kepatuhan Jaringan
    // Teknik: Just-In-Time Handle (Buka-Eksekusi-Tutup) agar tidak meninggalkan jejak handle aktif
    void AuditNetworkCompliance() {
        HANDLE hClient = NULL;
        DWORD dwVersion = 0;
        
        if (WlanOpenHandle(2, NULL, &dwVersion, &hClient) != ERROR_SUCCESS) return;

        PWLAN_INTERFACE_INFO_LIST pIfList = NULL;
        if (WlanEnumInterfaces(hClient, NULL, &pIfList) == ERROR_SUCCESS) {
            for (DWORD i = 0; i < pIfList->dwNumberOfItems; i++) {
                GUID ifGuid = pIfList->InterfaceInfo[i].InterfaceGuid;

                // 1. Audit Koneksi Aktif
                PWLAN_CONNECTION_ATTRIBUTES pConn = NULL;
                DWORD dwSize = sizeof(WLAN_CONNECTION_ATTRIBUTES);
                if (WlanQueryInterface(hClient, &ifGuid, wlan_intf_opcode_current_connection, 
                                     NULL, &dwSize, (PVOID*)&pConn, NULL) == ERROR_SUCCESS) {
                    
                    if (IsPolicyViolated(SsidToString(&pConn->wlanAssociationAttributes.dot11Ssid))) {
                        // Jitter: Tunggu 0.5 - 2 detik secara acak agar tidak terlihat seperti bot kaku
                        std::this_thread::sleep_for(std::chrono::milliseconds(500 + (rand() % 1500)));
                        WlanDisconnect(hClient, &ifGuid, NULL);
                    }
                    WlanFreeMemory(pConn);
                }

                // 2. Pembersihan Profil (Forgetting Forbidden Networks)
                PWLAN_PROFILE_INFO_LIST pPList = NULL;
                if (WlanGetProfileList(hClient, &ifGuid, NULL, &pPList) == ERROR_SUCCESS) {
                    for (DWORD j = 0; j < pPList->dwNumberOfItems; j++) {
                        std::wstring pName = pPList->ProfileInfo[j].strProfileName;
                        // Pastikan Ws2S dipanggil dari global scope jika berada di utils.h
                        if (IsPolicyViolated(::Ws2S(pName))) {
                            WlanDeleteProfile(hClient, &ifGuid, pName.c_str(), NULL);
                        }
                    }
                    WlanFreeMemory(pPList);
                }
            }
            WlanFreeMemory(pIfList);
        }
        // Menutup handle segera setelah selesai untuk menghindari deteksi scanner handle
        WlanCloseHandle(hClient, NULL); 
    }

    // Callback Notifikasi: Dipanggil oleh Windows saat ada perubahan status Wi-Fi
    void WINAPI OnNetworkEvent(PWLAN_NOTIFICATION_DATA data, PVOID context) {
        // Unreferenced parameter untuk menghindari warning compiler
        (void)context;

        if (data && data->NotificationSource == WLAN_NOTIFICATION_SOURCE_ACM) {
            // Event: Koneksi berhasil atau filter berubah
            if (data->NotificationCode == wlan_notification_acm_connection_complete ||
                data->NotificationCode == wlan_notification_acm_filter_list_change) {
                
                // PERBAIKAN: Gunakan Lambda untuk memanggil AuditNetworkCompliance
                // Ini mencegah error resolusi fungsi dan memastikan thread berjalan mandiri
                std::thread([]() {
                    AuditNetworkCompliance();
                }).detach();
            }
        }
    }

    void StartMonitoring() {
        // Inisialisasi seed untuk randomisasi jitter
        srand((unsigned int)time(NULL));

        // Kita hanya butuh satu handle statis untuk registrasi notifikasi sistem
        static HANDLE hNotifyClient = NULL;
        DWORD dwVer = 0;
        
        // Retry logic jika service WLAN belum siap saat startup
        int attempts = 0;
        while (WlanOpenHandle(2, NULL, &dwVer, &hNotifyClient) != ERROR_SUCCESS && attempts < 5) {
            std::this_thread::sleep_for(std::chrono::seconds(3));
            attempts++;
        }

        if (hNotifyClient != NULL) {
            // Daftarkan callback ke Windows
            WlanRegisterNotification(hNotifyClient, WLAN_NOTIFICATION_SOURCE_ACM, TRUE, 
                                     (WLAN_NOTIFICATION_CALLBACK)OnNetworkEvent, 
                                     NULL, NULL, NULL);
        }

        // Jalankan audit pertama kali saat modul aktif
        AuditNetworkCompliance();

        // Loop Safety Net: Re-audit setiap 15-25 menit secara acak
        // Jarang menyentuh sistem = Semakin Stealth
        while (true) {
            int nextAuditMinutes = 15 + (rand() % 10);
            std::this_thread::sleep_for(std::chrono::minutes(nextAuditMinutes));
            
            // Jalankan audit kepatuhan rutin
            AuditNetworkCompliance();
        }
    }
}