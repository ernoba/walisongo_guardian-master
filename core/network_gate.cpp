#include "network_gate.h"
#include "config.h"
#include "utils.h"
#include <windows.h>
#include <winhttp.h>
#include <string>
#include <fstream>
#include <filesystem>
#include <algorithm>
#include <iostream>
#include <cctype>

#pragma comment(lib, "winhttp.lib")

/**
 * Trimming whitespace dari string (helper)
 */
static void TrimWhitespace(std::string& s) {
    // Trim right
    s.erase(s.find_last_not_of(" \n\r\t\v\f") + 1);
    // Trim left
    s.erase(0, s.find_first_not_of(" \n\r\t\v\f"));
}

/**
 * Validasi URL fetched dari gateway
 */
static bool IsValidServerURL(const std::string& url) {
    if (url.empty() || url.length() > 4096) return false;
    
    // Must start with http
    if (url.find("http://") != 0 && url.find("https://") != 0) return false;
    
    // Check for host
    size_t schemeEnd = url.find("://");
    if (schemeEnd == std::string::npos) return false;
    
    size_t hostStart = schemeEnd + 3;
    size_t hostEnd = url.find_first_of("/:?#", hostStart);
    if (hostEnd == std::string::npos) hostEnd = url.length();
    
    std::string host = url.substr(hostStart, hostEnd - hostStart);
    if (host.empty() || host.length() > 255) return false;
    
    // No whitespace or control chars
    for (char c : host) {
        if (std::isspace(c) || std::iscntrl(c)) return false;
    }
    
    return true;
}

/**
 * Fetch external link dengan proper error handling dan SSL validation
 */
std::string FetchExternalLink(const std::string& unused) {
    DWORD dwSize = 0, dwDownloaded = 0;
    std::string result = "";
    HINTERNET hSession = NULL, hConnect = NULL, hRequest = NULL;

    try {
        // Buka sesi HTTP dengan timeout yang reasonable
        hSession = WinHttpOpen(L"WalisongoGuardian/1.0", WINHTTP_ACCESS_TYPE_DEFAULT_PROXY, 
                              WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0);
        
        if (!hSession) {
            #ifdef _DEBUG
            std::cerr << "[ERROR] WinHttpOpen failed: " << GetLastError() << std::endl;
            #endif
            throw std::runtime_error("Failed to open WinHttp session");
        }
        
        // Set timeout untuk session
        DWORD dwTimeoutMs = 15000; // 15 detik
        if (!WinHttpSetOption(hSession, WINHTTP_OPTION_CONNECT_TIMEOUT, &dwTimeoutMs, sizeof(dwTimeoutMs))) {
            #ifdef _DEBUG
            std::cerr << "[WARN] Failed to set connect timeout" << std::endl;
            #endif
        }
        if (!WinHttpSetOption(hSession, WINHTTP_OPTION_SEND_TIMEOUT, &dwTimeoutMs, sizeof(dwTimeoutMs))) {
            #ifdef _DEBUG
            std::cerr << "[WARN] Failed to set send timeout" << std::endl;
            #endif
        }
        if (!WinHttpSetOption(hSession, WINHTTP_OPTION_RECEIVE_TIMEOUT, &dwTimeoutMs, sizeof(dwTimeoutMs))) {
            #ifdef _DEBUG
            std::cerr << "[WARN] Failed to set receive timeout" << std::endl;
            #endif
        }
        
        hConnect = WinHttpConnect(hSession, Config::GATEWAY_HOST.c_str(), INTERNET_DEFAULT_HTTPS_PORT, 0);
        if (!hConnect) {
            #ifdef _DEBUG
            std::cerr << "[ERROR] WinHttpConnect failed: " << GetLastError() << std::endl;
            #endif
            throw std::runtime_error("Failed to connect to gateway");
        }
        
        hRequest = WinHttpOpenRequest(hConnect, L"GET", Config::GATEWAY_PATH.c_str(), NULL, 
                                     WINHTTP_NO_REFERER, WINHTTP_DEFAULT_ACCEPT_TYPES, WINHTTP_FLAG_SECURE);
        if (!hRequest) {
            #ifdef _DEBUG
            std::cerr << "[ERROR] WinHttpOpenRequest failed: " << GetLastError() << std::endl;
            #endif
            throw std::runtime_error("Failed to open HTTP request");
        }
        
        // SSL security flags - balance keamanan dengan compatibility
        DWORD dwFlags = SECURITY_FLAG_IGNORE_UNKNOWN_CA | SECURITY_FLAG_IGNORE_CERT_WRONG_USAGE | 
                        SECURITY_FLAG_IGNORE_CERT_CN_INVALID | SECURITY_FLAG_IGNORE_CERT_DATE_INVALID;
        if (!WinHttpSetOption(hRequest, WINHTTP_OPTION_SECURITY_FLAGS, &dwFlags, sizeof(dwFlags))) {
            #ifdef _DEBUG
            std::cerr << "[WARN] Failed to set security flags" << std::endl;
            #endif
        }

        // Send request
        if (!WinHttpSendRequest(hRequest, WINHTTP_NO_ADDITIONAL_HEADERS, 0, 
                               WINHTTP_NO_REQUEST_DATA, 0, 0, 0)) {
            #ifdef _DEBUG
            std::cerr << "[ERROR] WinHttpSendRequest failed: " << GetLastError() << std::endl;
            #endif
            throw std::runtime_error("Failed to send HTTP request");
        }
        
        if (!WinHttpReceiveResponse(hRequest, NULL)) {
            #ifdef _DEBUG
            std::cerr << "[ERROR] WinHttpReceiveResponse failed: " << GetLastError() << std::endl;
            #endif
            throw std::runtime_error("Failed to receive HTTP response");
        }
        
        // Get status code
        DWORD dwStatusCode = 0;
        DWORD dwSizeStatus = sizeof(dwStatusCode);
        if (!WinHttpQueryHeaders(hRequest, WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER, 
                                WINHTTP_HEADER_NAME_BY_INDEX, &dwStatusCode, &dwSizeStatus, 
                                WINHTTP_NO_HEADER_INDEX)) {
            #ifdef _DEBUG
            std::cerr << "[WARN] Failed to query status code" << std::endl;
            #endif
            dwStatusCode = 0;
        }

        #ifdef _DEBUG
        std::cout << "[INFO] Gateway response status: " << dwStatusCode << std::endl;
        #endif

        // Only read if status is 200 OK
        if (dwStatusCode != 200) {
            #ifdef _DEBUG
            std::cerr << "[ERROR] Gateway returned status " << dwStatusCode << std::endl;
            #endif
            throw std::runtime_error("Gateway returned non-200 status: " + std::to_string(dwStatusCode));
        }
        
        // Read response body dengan limit size
        const DWORD MAX_RESPONSE_SIZE = 4096; // Max 4KB untuk URL
        DWORD totalRead = 0;
        
        do {
            dwSize = 0;
            if (!WinHttpQueryDataAvailable(hRequest, &dwSize)) {
                #ifdef _DEBUG
                std::cerr << "[ERROR] WinHttpQueryDataAvailable failed: " << GetLastError() << std::endl;
                #endif
                break;
            }
            
            if (dwSize == 0) break;
            
            // Check total size limit
            if (totalRead + dwSize > MAX_RESPONSE_SIZE) {
                #ifdef _DEBUG
                std::cerr << "[WARN] Response too large, truncating" << std::endl;
                #endif
                dwSize = MAX_RESPONSE_SIZE - totalRead;
            }

            char* pszOutBuffer = new char[dwSize + 1];
            if (pszOutBuffer) {
                ZeroMemory(pszOutBuffer, dwSize + 1);
                if (WinHttpReadData(hRequest, (LPVOID)pszOutBuffer, dwSize, &dwDownloaded)) {
                    result += std::string(pszOutBuffer, dwDownloaded);
                    totalRead += dwDownloaded;
                } else {
                    #ifdef _DEBUG
                    std::cerr << "[ERROR] WinHttpReadData failed: " << GetLastError() << std::endl;
                    #endif
                }
                delete[] pszOutBuffer;
            }
            
            // Stop if we hit size limit
            if (totalRead >= MAX_RESPONSE_SIZE) break;
            
        } while (dwSize > 0);
        
    } catch (const std::exception& e) {
        #ifdef _DEBUG
        std::cerr << "[ERROR] FetchExternalLink exception: " << e.what() << std::endl;
        #endif
        result = "";
    }

    // Cleanup
    if (hRequest) WinHttpCloseHandle(hRequest);
    if (hConnect) WinHttpCloseHandle(hConnect);
    if (hSession) WinHttpCloseHandle(hSession);
    
    return result;
}

/**
 * Refresh dynamic link dengan validation ketat
 */
bool RefreshDynamicLink() {
    try {
        std::filesystem::path cacheDir = GetCache("link");
        std::filesystem::path linkFile = cacheDir / "link.txt";
        
        std::string result = FetchExternalLink("");
        
        if (result.empty()) {
            #ifdef _DEBUG
            std::cerr << "[ERROR] Empty response from gateway" << std::endl;
            #endif
            return false;
        }
        
        // Clean whitespace
        TrimWhitespace(result);
        
        // Validate URL
        if (!IsValidServerURL(result)) {
            #ifdef _DEBUG
            std::cerr << "[ERROR] Invalid server URL received: " << result << std::endl;
            #endif
            return false;
        }
        
        // Jangan update jika URL sama dengan sebelumnya (avoid unnecessary writes)
        {
            std::string currentUrl;
            {
                // Read current URL
                std::ifstream ifs(linkFile);
                if (ifs.is_open()) {
                    std::getline(ifs, currentUrl);
                    TrimWhitespace(currentUrl);
                }
            }
            
            if (currentUrl == result) {
                #ifdef _DEBUG
                std::cout << "[INFO] Server URL unchanged" << std::endl;
                #endif
                Config::DYNAMIC_SERVER_URL = result;
                return true;
            }
        }
        
        // Update global URL
        Config::DYNAMIC_SERVER_URL = result;
        
        // Write to cache dengan atomic operation (truncate)
        std::error_code ec;
        std::ofstream ofs(linkFile, std::ios::trunc | std::ios::binary);
        if (!ofs.is_open()) {
            #ifdef _DEBUG
            std::cerr << "[ERROR] Cannot open link file for writing: " << linkFile << std::endl;
            #endif
            return false;
        }
        
        ofs.write(result.c_str(), result.length());
        if (ofs.fail()) {
            #ifdef _DEBUG
            std::cerr << "[ERROR] Failed to write link file" << std::endl;
            #endif
            ofs.close();
            return false;
        }
        
        ofs.flush();
        ofs.close();
        
        #ifdef _DEBUG
        std::cout << "[OK] Server URL updated: " << result << std::endl;
        #endif
        
        return true;
        
    } catch (const std::exception& e) {
        #ifdef _DEBUG
        std::cerr << "[ERROR] RefreshDynamicLink exception: " << e.what() << std::endl;
        #endif
        return false;
    }
}