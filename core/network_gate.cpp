#include "network_gate.h"
#include "config.h"
#include "utils.h"
#include <windows.h>
#include <winhttp.h>
#include <string>
#include <fstream>
#include <filesystem>
#include <algorithm>

#pragma comment(lib, "winhttp.lib")

std::string FetchExternalLink(const std::string& unused) {
    DWORD dwSize = 0, dwDownloaded = 0;
    std::string result = "";
    HINTERNET hSession = NULL, hConnect = NULL, hRequest = NULL;

    // Buka sesi HTTP
    hSession = WinHttpOpen(L"WalisongoGuardian/1.0", WINHTTP_ACCESS_TYPE_DEFAULT_PROXY, WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0);
    
    if (hSession) {
        hConnect = WinHttpConnect(hSession, Config::GATEWAY_HOST.c_str(), INTERNET_DEFAULT_HTTPS_PORT, 0);
    }
    
    if (hConnect) {
        hRequest = WinHttpOpenRequest(hConnect, L"GET", Config::GATEWAY_PATH.c_str(), NULL, WINHTTP_NO_REFERER, WINHTTP_DEFAULT_ACCEPT_TYPES, WINHTTP_FLAG_SECURE);
    }

    if (hRequest) {
        // TAMBALAN: Abaikan kesalahan sertifikat agar koneksi ke gateway tidak mudah putus
        DWORD dwFlags = SECURITY_FLAG_IGNORE_UNKNOWN_CA | SECURITY_FLAG_IGNORE_CERT_WRONG_USAGE | 
                        SECURITY_FLAG_IGNORE_CERT_CN_INVALID | SECURITY_FLAG_IGNORE_CERT_DATE_INVALID;
        WinHttpSetOption(hRequest, WINHTTP_OPTION_SECURITY_FLAGS, &dwFlags, sizeof(dwFlags));

        if (WinHttpSendRequest(hRequest, WINHTTP_NO_ADDITIONAL_HEADERS, 0, WINHTTP_NO_REQUEST_DATA, 0, 0, 0)) {
            if (WinHttpReceiveResponse(hRequest, NULL)) {
                
                // TAMBALAN: Pastikan statusnya 200 OK sebelum membaca data
                DWORD dwStatusCode = 0;
                DWORD dwSizeStatus = sizeof(dwStatusCode);
                WinHttpQueryHeaders(hRequest, WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER, 
                                   WINHTTP_HEADER_NAME_BY_INDEX, &dwStatusCode, &dwSizeStatus, WINHTTP_NO_HEADER_INDEX);

                if (dwStatusCode == 200) {
                    do {
                        dwSize = 0;
                        if (!WinHttpQueryDataAvailable(hRequest, &dwSize)) break;
                        if (dwSize == 0) break;

                        char* pszOutBuffer = new char[dwSize + 1];
                        if (pszOutBuffer) {
                            ZeroMemory(pszOutBuffer, dwSize + 1);
                            if (WinHttpReadData(hRequest, (LPVOID)pszOutBuffer, dwSize, &dwDownloaded)) {
                                result += pszOutBuffer;
                            }
                            delete[] pszOutBuffer;
                        }
                    } while (dwSize > 0);
                }
            }
        }
    }

    if (hRequest) WinHttpCloseHandle(hRequest);
    if (hConnect) WinHttpCloseHandle(hConnect);
    if (hSession) WinHttpCloseHandle(hSession);
    return result;
}

bool RefreshDynamicLink() {
    // Pastikan folder cache ada
    std::filesystem::path cacheDir = GetCache("link");
    std::filesystem::path linkFile = cacheDir / "link.txt";
    
    std::string result = FetchExternalLink(""); 

    if (!result.empty()) {
        // Pembersihan karakter whitespace dan karakter sampah
        result.erase(result.find_last_not_of(" \n\r\t") + 1);
        result.erase(0, result.find_first_not_of(" \n\r\t"));

        // Validasi minimal: harus dimulai dengan http
        if (result.find("http") == 0) {
            Config::DYNAMIC_SERVER_URL = result;
            
            // Gunakan trunc untuk memastikan link lama terhapus bersih
            std::ofstream ofs(linkFile, std::ios::trunc);
            if (ofs.is_open()) { 
                ofs << result; 
                ofs.flush();
                ofs.close(); 
                return true;
            }
        }
    }
    return false;
}