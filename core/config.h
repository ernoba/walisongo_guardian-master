#ifndef CONFIG_H
#define CONFIG_H

#include <string>
#include <vector>

namespace Config {
    // Host dan Path dipisah untuk memudahkan penggunaan WinHttp
    extern const std::string CLIENT_VERSION; 
    extern const std::wstring GATEWAY_HOST;
    extern const std::wstring GATEWAY_PATH; 

    // Di dalam namespace Config
    extern time_t EXPIRY_DATE; // Tanggal mati (Fixed Date)
    
    extern std::string DYNAMIC_SERVER_URL;

    extern const std::wstring INSTALL_LICENSE;
    extern const std::wstring REG_PATH;
    extern const std::wstring TASK_NAME;
    extern const std::wstring EXE_NAME;
    extern const std::wstring DATA_DIR;
    extern const std::string MUTEX_ID;
    extern const std::string RAW_API_KEY;
    extern const std::string ENCRYPTION_KEY;
    extern const size_t CHUNK_SIZE; // Pastikan hanya ada satu deklarasi di sini
    extern const std::vector<std::string> BLACKLIST;
    // Daftar SSID WiFi yang dilarang (Gunakan nama WiFi spesifik)
    extern const std::vector<std::string> WIFI_BLACKLIST;
}

#endif