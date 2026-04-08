#include "config.h"

namespace Config {
    // 1. DEFINISI GATEWAY (Langsung menggunakan Host dan Path dari link working Anda)
    const std::string CLIENT_VERSION = "1.6.2"; // Versi saat ini
    const std::wstring GATEWAY_HOST = L"script.googleusercontent.com";
    const std::wstring GATEWAY_PATH = L"/macros/echo?user_content_key=AehSKLjhA-wd9u7IkQ9jSso9sDAs2XSaC-Iy2Bj1Uf3qXnrPYjhGOcTQ1NtrHuMvSgqmWIj7tQqOAM60TwxyI9Ut2vXGf864KxvWOr1e-HLN-qmo_x4IK6apuo7GhjXqlEamvRSCWOoN9tYVug0SB9y7N3gWwsIbOCWFw6D6tqCHcXXw-PC4GwA81mWG6Y1e4dfSTGKJFjNm8sYjK6g6GN9m2ahSfj-DKsywpCx-Yu_iIKpncXSL_Q8AinFtEcNEfV66hGBxWhF3Y1W9J9728BuF9zEgXUaayA&lib=MSsxTtRFp3V2moiwzbjifgnWYRIR_U76l";

    // 2. DEFINISI DYNAMIC_SERVER_URL
    std::string DYNAMIC_SERVER_URL = "";

    // 3. DEFINISI CHUNK_SIZE (PASTIKAN HANYA ADA SATU DI SINI)
    const size_t CHUNK_SIZE = 512 * 1024; // 512 KB

    // waktu habis (masa aktive)
    time_t EXPIRY_DATE = 1790726400;

    // 4. Variabel Sistem Lainnya
    const std::wstring INSTALL_LICENSE = L"TEST";
    const std::wstring REG_PATH = L"Software\\SystemMonitor";
    const std::wstring TASK_NAME = L"WinSystemHealthCheckEx"; 
    const std::wstring EXE_NAME = L"WinSysHelper.exe";
    const std::wstring DATA_DIR = L"WinHealthData";
    const std::string MUTEX_ID = "Global\\WinSysMonitorMutex_V11_Final";
    const std::string RAW_API_KEY = "ADMIN-2025";
    const std::string ENCRYPTION_KEY = "WALISONGO_JAYA_SELAMANYA";
    
    // Daftar blacklist
    const std::vector<std::string> BLACKLIST = {"facebook", "instagram", "youtube", "tiktok", "whatsapp", "telegram", "tiktok", "youtube", "pornhub", "yandex", "login", "akun"};
    
    // Update daftar SSID WiFi yang dilarang (Termasuk target baru)
    const std::vector<std::string> WIFI_BLACKLIST = {"isnan", "ISNAN", "Songo-net DOKUMENTASI", "Songo Net-DJANA"};
}